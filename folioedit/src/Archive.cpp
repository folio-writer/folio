//
// folioedit :: Archive -- whole-file open/save orchestration. Pure STL + engine
// calls; NO <openssl/*> here (the crypto lives behind Seal). The `binds` content
// hashes use sha256_hex from Custody.
//
#include "folioedit/Archive.hpp"
#include "folioedit/Custody.hpp"   // sha256_hex
#include "folioedit/Seal.hpp"      // seal / unseal / *_with_passphrase

#include <fstream>
#include <stdexcept>

namespace folioedit {
namespace {

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

bytes doc_to_plaintext(const Document& doc) {
    const std::string s = to_json(doc).dump();
    return bytes(s.begin(), s.end());
}
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

// ── raw-key open/save ────────────────────────────────────────────────────────
void save_document(const std::string& path, const Document& doc, const bytes& key) {
    write_envelope_file(path, seal(doc_to_plaintext(doc), key));
}
Document open_document(const std::string& path, const bytes& key) {
    return plaintext_to_doc(unseal(read_envelope_file(path), key));
}

// ── passphrase open/save ─────────────────────────────────────────────────────
void save_document_pw(const std::string& path, const Document& doc,
                      const std::string& passphrase) {
    write_envelope_file(path, seal_with_passphrase(doc_to_plaintext(doc), passphrase));
}
Document open_document_pw(const std::string& path, const std::string& passphrase) {
    return plaintext_to_doc(unseal_with_passphrase(read_envelope_file(path), passphrase));
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
