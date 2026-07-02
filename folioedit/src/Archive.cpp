//
// folioedit :: Archive -- whole-file open/save orchestration. Pure STL + engine
// calls; NO <openssl/*> here (the crypto lives behind Seal). The `binds` content
// hashes use sha256_hex from Custody. Two faces (s18): the SEALED read/save are
// guarded behind FOLIOEDIT_NO_CRYPTO; the plain read/save + content sniff + the
// hashes are always present and pure.
//
#include "folioedit/Archive.hpp"
#include "folioedit/Custody.hpp"   // sha256_hex
#ifndef FOLIOEDIT_NO_CRYPTO
#include "folioedit/Seal.hpp"      // seal / unseal / *_with_passphrase
#endif

#include <cctype>
#include <fstream>
#include <stdexcept>

namespace folioedit {
namespace {

// A short, format-level briefing baked at the top of every PLAIN export so the
// file is self-describing to a chat/AI that opens it directly (s18.2). It is a
// top-level `instructions` string; from_json ignores it on read.
constexpr const char* PLAIN_INSTRUCTIONS =
    "This is an unsealed Folio editorial pass. To review it: read `pass.rules` "
    "and the `scenes` (each scene's `text` is HTML; character offsets are within "
    "that scene). Add your notes by APPENDING objects to the `annotations` array "
    "-- each with scene_iid, quote (the exact spanned text; this is how Folio "
    "re-anchors your note), kind (a hat from pass.kinds), and text (your comment). "
    "Do NOT edit the scenes, the custody chain, or anyone else's annotations -- "
    "Folio files everything you add as an accept-or-delete PROPOSAL beside the "
    "prose and verifies (via body_hash in custody) that the manuscript was left "
    "untouched. Return this file only.";


bytes read_file_bytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("folioedit: cannot open file for reading: " + path);
    const std::streamsize n = f.tellg();
    if (n < 0) throw std::runtime_error("folioedit: cannot size file: " + path);
    bytes buf(static_cast<std::size_t>(n));
    f.seekg(0);
    if (n > 0 && !f.read(reinterpret_cast<char*>(buf.data()), n))
        throw std::runtime_error("folioedit: short read: " + path);
    return buf;
}

void write_file_bytes(const std::string& path, const bytes& data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("folioedit: cannot open file for writing: " + path);
    if (!data.empty())
        f.write(reinterpret_cast<const char*>(data.data()),
                static_cast<std::streamsize>(data.size()));
    if (!f) throw std::runtime_error("folioedit: failed writing file: " + path);
}

#ifndef FOLIOEDIT_NO_CRYPTO
bytes doc_to_plaintext(const Document& doc) {
    const std::string s = to_json(doc).dump();
    return bytes(s.begin(), s.end());
}
#endif
Document plaintext_to_doc(const bytes& pt) {
    return from_json(json::parse(std::string(pt.begin(), pt.end())));
}

// Length-prefixed field appender (mirrors canonical_contents' discipline).
void add_str(std::string& c, const char* key, const std::string& val) {
    c += '\n'; c += key; c += '='; c += std::to_string(val.size()); c += ':'; c += val;
}
void add_int(std::string& c, const char* key, long long val) {
    c += '\n'; c += key; c += '='; c += std::to_string(val);
}

}  // namespace

// ── envelope frame on disk ───────────────────────────────────────────────────
void write_envelope_file(const std::string& path, const Envelope& env) {
    write_file_bytes(path, envelope_to_bytes(env));
}
Envelope read_envelope_file(const std::string& path) {
    return envelope_from_bytes(read_file_bytes(path));
}

// ── raw-key open/save (SEALED face) ──────────────────────────────────────────
#ifndef FOLIOEDIT_NO_CRYPTO
void save_document(const std::string& path, const Document& doc, const bytes& key) {
    write_envelope_file(path, seal(doc_to_plaintext(doc), key));
}
Document open_document(const std::string& path, const bytes& key) {
    return plaintext_to_doc(unseal(read_envelope_file(path), key));
}

// ── passphrase open/save (SEALED face) ───────────────────────────────────────
void save_document_pw(const std::string& path, const Document& doc,
                      const std::string& passphrase) {
    write_envelope_file(path, seal_with_passphrase(doc_to_plaintext(doc), passphrase));
}
Document open_document_pw(const std::string& path, const std::string& passphrase) {
    return plaintext_to_doc(unseal_with_passphrase(read_envelope_file(path), passphrase));
}
#endif  // FOLIOEDIT_NO_CRYPTO

// ── plain (unsealed) open/save (PLAIN face -- pure, both builds) ─────────────
void save_document_plain(const std::string& path, const Document& doc) {
    json j = to_json(doc);
    j["instructions"] = PLAIN_INSTRUCTIONS;      // ignored on read; self-describing
    const std::string s = j.dump(2);             // readable for a human/AI
    write_file_bytes(path, bytes(s.begin(), s.end()));
}
Document open_document_plain(const std::string& path) {
    return plaintext_to_doc(read_file_bytes(path));
}

// ── content sniff (pure, no key) ─────────────────────────────────────────────
FileFace peek_file_face(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return FileFace::Unknown;
    // MAGIC is "FOLIOEDIT" (9 bytes); a plain file is JSON -> first non-space '{'.
    char c = 0;
    while (f.get(c)) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isspace(uc)) continue;          // JSON may lead with whitespace
        if (c == MAGIC[0]) {                     // 'F' -- check the full magic
            std::string head(1, c);
            char rest[8] = {};
            f.read(rest, 8);                     // exact/short read; gcount = actual
            head.append(rest, static_cast<std::size_t>(f.gcount()));
            return head == MAGIC ? FileFace::Sealed : FileFace::Unknown;
        }
        if (c == '{') return FileFace::Plain;
        return FileFace::Unknown;                // neither
    }
    return FileFace::Unknown;                     // empty
}

// ── content hashes for custody `binds` ───────────────────────────────────────
std::string body_hash(const Document& doc) {
    std::string c = "folioedit.body.v1";
    add_int(c, "scenes", static_cast<long long>(doc.scenes.size()));
    for (const Scene& s : doc.scenes) {
        add_str(c, "iid",  s.iid);
        add_int(c, "order", s.order);
        add_str(c, "text", s.text);
    }
    return sha256_hex(c);
}

std::string annotations_hash(const Document& doc) {
    std::string c = "folioedit.annotations.v1";
    add_int(c, "count", static_cast<long long>(doc.annotations.size()));
    for (const Annotation& a : doc.annotations) {
        add_str(c, "scene_iid",   a.scene_iid);
        add_int(c, "range_start", a.range_start);
        add_int(c, "range_end",   a.range_end);
        add_str(c, "quote",       a.quote);
        add_str(c, "kind",        a.kind);
        add_str(c, "text",        a.text);
        add_int(c, "withdrawn",   a.withdrawn ? 1 : 0);
    }
    return sha256_hex(c);
}

}  // namespace folioedit
