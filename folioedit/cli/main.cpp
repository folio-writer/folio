//
// folioedit CLI -- the step-6 face. Thin: it renders the shared Vocabulary and
// calls the engine. This first cut carries only the settled read-side verbs
// (help / keygen / info); the mutate verbs (annotate / seal) land once their
// custody semantics are decided. Pure STL + libfolioedit (libcrypto); no gtk.
//
#include "folioedit/Vocabulary.hpp"
#include "folioedit/Archive.hpp"
#include "folioedit/Custody.hpp"
#include "folioedit/Format.hpp"
#include "folioedit/Seal.hpp"      // hex_to_key (pure; declared here)
#ifndef FOLIOEDIT_NO_CRYPTO
#include "folioedit/Identity.hpp"  // keypairs + signing (sealed face only)
#endif
#ifdef FOLIOEDIT_HAVE_TUI
#include "Tui.hpp"                 // the interactive human face (bare `folioedit`)
#endif

#include <iostream>
#include <ctime>
#include <stdexcept>
#include <string>
#include <vector>

namespace fe = folioedit;

namespace {

#ifndef FOLIOEDIT_NO_CRYPTO
std::string cipher_str(fe::CipherId c) {
    return c == fe::CipherId::AesGcm256 ? "AES-256-GCM" : "unknown";
}
std::string kdf_str(fe::KdfId k) {
    switch (k) {
        case fe::KdfId::None:             return "none (raw key)";
        case fe::KdfId::Pbkdf2HmacSha256: return "PBKDF2-HMAC-SHA256";
    }
    return "unknown";
}
#endif

// find "--flag value" in args; returns true + sets out if present.
bool opt(const std::vector<std::string>& a, const std::string& flag, std::string& out) {
    for (std::size_t i = 0; i + 1 < a.size(); ++i)
        if (a[i] == flag) { out = a[i + 1]; return true; }
    return false;
}

// How to open + re-save a file: a raw hex key, or a passphrase.
struct Access { bool have = false; bool pw = false; std::string secret; };
Access resolve_access(const std::vector<std::string>& rest) {
    Access a; std::string v;
    if (opt(rest, "--pass", v))      { a.have = true; a.pw = true;  a.secret = v; }
    else if (opt(rest, "--key", v))  { a.have = true; a.pw = false; a.secret = v; }
    return a;
}

#ifndef FOLIOEDIT_NO_CRYPTO
fe::Document open_sealed(const Access& a, const std::string& path) {
    return a.pw ? fe::open_document_pw(path, a.secret)
                : fe::open_document(path, fe::hex_to_key(a.secret));
}
void save_sealed(const Access& a, const std::string& path, const fe::Document& doc) {
    if (a.pw) fe::save_document_pw(path, doc, a.secret);
    else      fe::save_document(path, doc, fe::hex_to_key(a.secret));
}
#endif

// Face-aware open: content-sniff the file, then read it the right way. A plain
// file needs no key; a sealed file needs --key/--pass (and, in a plain build,
// cannot be opened at all). Remembers the face so a mutating verb saves it back
// in the same shape it arrived.
struct Opened { fe::Document doc; fe::FileFace face; };
Opened open_any(const Access& acc, const std::string& path) {
    const fe::FileFace face = fe::peek_file_face(path);
    if (face == fe::FileFace::Plain)
        return { fe::open_document_plain(path), face };
    if (face == fe::FileFace::Sealed) {
#ifndef FOLIOEDIT_NO_CRYPTO
        if (!acc.have)
            throw std::runtime_error("folioedit: sealed file -- pass --key <hex> or --pass <phrase>");
        return { open_sealed(acc, path), face };
#else
        (void)acc;
        throw std::runtime_error("folioedit: this is a sealed file; this build "
            "(folioedit-plain) has no crypto -- use the full folioedit to open it");
#endif
    }
    throw std::runtime_error("folioedit: not a .folioedit file (neither sealed "
        "envelope nor plain JSON): " + path);
}
void save_any(const Access& acc, const std::string& path, const fe::Document& doc,
              fe::FileFace face) {
    if (face == fe::FileFace::Plain) { fe::save_document_plain(path, doc); return; }
#ifndef FOLIOEDIT_NO_CRYPTO
    save_sealed(acc, path, doc);
#else
    (void)acc;
    throw std::runtime_error("folioedit: cannot re-save a sealed file in a plain build");
#endif
}

#ifndef FOLIOEDIT_NO_CRYPTO
std::string now_iso() {
    std::time_t t = std::time(nullptr);
    std::tm tmv;
    gmtime_r(&t, &tmv);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tmv);
    return std::string(buf);
}
#endif

int cmd_help(const std::vector<std::string>& rest) {
    if (!rest.empty()) {
        if (const fe::Command* c = fe::find_command(rest[0])) {
            std::cout << fe::render_command_help(*c);
            return 0;
        }
        std::cerr << "folioedit: unknown command '" << rest[0] << "'\n";
        return 1;
    }
    std::cout << fe::render_help();
    return 0;
}

#ifndef FOLIOEDIT_NO_CRYPTO
int cmd_keygen(const std::vector<std::string>& rest) {
    if (rest.empty()) { std::cerr << "usage: folioedit keygen <keyfile>\n"; return 1; }
    fe::KeyPair kp = fe::generate_keypair();
    fe::save_keypair(kp, rest[0]);
    std::cout << "wrote identity to " << rest[0] << "\n"
              << "actor_id (fingerprint): " << fe::fingerprint(kp.public_key) << "\n";
    return 0;
}
#endif

// Print the opened document's contents (shared by both faces).
void print_doc(const fe::Document& doc) {
    std::size_t withdrawn = 0;
    for (const fe::Annotation& a : doc.annotations) if (a.withdrawn) ++withdrawn;
    std::cout << "\nproject: " << doc.project_title << " (" << doc.project_id << ")\n"
              << "scenes:  " << doc.scenes.size()
              << "   annotations: " << doc.annotations.size();
    if (withdrawn) std::cout << " (" << withdrawn << " withdrawn)";
    std::cout << "\n"
              << "custody: " << (fe::verify_chain(doc.custody) ? "VERIFIED" : "BROKEN")
              << " (" << doc.custody.size() << " events)\n";
    for (const fe::CustodyEvent& e : doc.custody) {
        std::cout << "  #" << e.seq << " " << fe::kind_to_str(e.kind)
                  << " by " << (e.actor.empty() ? "?" : e.actor)
                  << " at " << e.at
                  << " [" << fe::time_to_str(e.time_source) << "]"
                  << (e.signature.empty()       ? "" : " signed")
                  << (e.timestamp_token.empty() ? "" : "+stamped")
                  << "\n";
    }
    if (!doc.annotations.empty()) {
        std::cout << "annotations:\n";
        for (std::size_t i = 0; i < doc.annotations.size(); ++i) {
            const fe::Annotation& a = doc.annotations[i];
            std::cout << "  [" << i << "] " << a.scene_iid
                      << " " << a.range_start << "-" << a.range_end
                      << " " << (a.kind.empty() ? "?" : a.kind)
                      << (a.withdrawn ? " (withdrawn)" : "")
                      << ": " << a.text << "\n";
        }
    }
}

int cmd_info(const std::vector<std::string>& rest) {
    if (rest.empty()) { std::cerr << "usage: folioedit info <file> [--key hex | --pass pw]\n"; return 1; }
    const std::string& path = rest[0];
    const fe::FileFace face = fe::peek_file_face(path);

    // ── plain (unsealed) file: readable JSON, no key needed ──────────────────
    if (face == fe::FileFace::Plain) {
        std::cout << "file:   " << path << "\n"
                  << "face:   plain (unsealed -- for a chat / AI)\n";
        print_doc(fe::open_document_plain(path));
        return 0;
    }
    if (face != fe::FileFace::Sealed) {
        std::cerr << "folioedit: not a .folioedit file (neither sealed envelope "
                     "nor plain JSON): " << path << "\n";
        return 1;
    }

    // ── sealed file ──────────────────────────────────────────────────────────
#ifdef FOLIOEDIT_NO_CRYPTO
    std::cout << "file:   " << path << "\n"
              << "face:   sealed\n"
              << "\n(this build -- folioedit-plain -- has no crypto; cannot open a sealed file)\n";
    return 0;
#else
    // print the frame, then open + verify if a key is given.
    fe::Envelope env = fe::read_envelope_file(path);
    std::cout << "file:   " << path << "\n"
              << "face:   sealed\n"
              << "schema: " << env.schema << "\n"
              << "cipher: " << cipher_str(env.cipher) << "\n"
              << "kdf:    " << kdf_str(env.kdf_id);
    if (env.kdf_id == fe::KdfId::Pbkdf2HmacSha256) std::cout << " (" << env.kdf_iters << " iters)";
    std::cout << "\n";

    std::string keyhex, pass;
    const bool have_key  = opt(rest, "--key",  keyhex);
    const bool have_pass = opt(rest, "--pass", pass);
    if (!have_key && !have_pass) {
        std::cout << "\n(no key given -- pass --key <hex> or --pass <phrase> to open + verify)\n";
        return 0;
    }
    fe::Document doc = have_key ? fe::open_document(path, fe::hex_to_key(keyhex))
                                : fe::open_document_pw(path, pass);
    print_doc(doc);
    return 0;
#endif
}

int cmd_annotate(const std::vector<std::string>& rest) {
    if (rest.empty()) { std::cerr << "usage: folioedit annotate <file> [--key|--pass ... if sealed] --scene <iid> --kind <hat> --text <t> [--start N --end N --quote Q]\n"; return 1; }
    const std::string& path = rest[0];
    Access acc = resolve_access(rest);

    std::string scene, kind, text, quote, sS, sE;
    opt(rest, "--scene", scene); opt(rest, "--kind", kind); opt(rest, "--text", text);
    opt(rest, "--quote", quote); opt(rest, "--start", sS);  opt(rest, "--end", sE);
    if (scene.empty() || kind.empty() || text.empty()) {
        std::cerr << "annotate: --scene, --kind and --text are required\n"; return 1;
    }

    Opened o = open_any(acc, path);
    fe::Document& doc = o.doc;

    bool scene_ok = false;
    for (const fe::Scene& s : doc.scenes) if (s.iid == scene) scene_ok = true;
    if (!scene_ok) { std::cerr << "annotate: no scene '" << scene << "' in this file\n"; return 1; }

    if (!doc.pass.kinds.empty()) {
        bool kind_ok = false;
        for (const std::string& k : doc.pass.kinds) if (k == kind) kind_ok = true;
        if (!kind_ok) {
            std::cerr << "annotate: kind '" << kind << "' is not allowed by this pass. allowed:";
            for (const std::string& k : doc.pass.kinds) std::cerr << " " << k;
            std::cerr << "\n"; return 1;
        }
    }

    fe::Annotation a;
    a.scene_iid = scene; a.kind = kind; a.text = text; a.quote = quote;
    a.range_start = sS.empty() ? 0 : std::stoi(sS);
    a.range_end   = sE.empty() ? 0 : std::stoi(sE);
    doc.annotations.push_back(a);
    save_any(acc, path, doc, o.face);

    std::cout << "added annotation [" << (doc.annotations.size() - 1) << "] "
              << kind << " on " << scene << "\n";
    return 0;
}

int cmd_del(const std::vector<std::string>& rest) {
    if (rest.empty()) { std::cerr << "usage: folioedit del <file> [--key|--pass ... if sealed] --index <n>\n"; return 1; }
    const std::string& path = rest[0];
    Access acc = resolve_access(rest);
    std::string idxs;
    if (!opt(rest, "--index", idxs)) { std::cerr << "del: --index <n> is required\n"; return 1; }

    Opened o = open_any(acc, path);
    fe::Document& doc = o.doc;
    const std::size_t idx = static_cast<std::size_t>(std::stoul(idxs));
    if (idx >= doc.annotations.size()) { std::cerr << "del: index out of range\n"; return 1; }
    if (doc.annotations[idx].withdrawn) { std::cout << "annotation [" << idx << "] is already withdrawn\n"; return 0; }

    doc.annotations[idx].withdrawn = true;   // tombstone -- kept in the record
    save_any(acc, path, doc, o.face);
    std::cout << "withdrew annotation [" << idx << "] (kept as a tombstone in the record)\n";
    return 0;
}

#ifndef FOLIOEDIT_NO_CRYPTO
int cmd_seal(const std::vector<std::string>& rest) {
    if (rest.empty()) { std::cerr << "usage: folioedit seal <file> --key|--pass ... --identity <keyfile> --actor <name>\n"; return 1; }
    const std::string& path = rest[0];
    Access acc = resolve_access(rest);
    if (!acc.have) { std::cerr << "seal: need --key <hex> or --pass <phrase>\n"; return 1; }
    std::string idpath, actor;
    opt(rest, "--identity", idpath); opt(rest, "--actor", actor);
    if (idpath.empty() || actor.empty()) { std::cerr << "seal: --identity <keyfile> and --actor <name> are required\n"; return 1; }

    fe::Document doc = open_sealed(acc, path);
    fe::KeyPair kp   = fe::load_keypair(idpath);

    fe::CustodyEvent e;
    e.kind = fe::CustodyEvent_Kind::Sealed;
    e.actor = actor;
    e.actor_id = fe::fingerprint(kp.public_key);   // set before finalize (bound)
    e.at = now_iso();
    e.binds = fe::annotations_hash(doc);
    fe::sign_event(fe::append_event(doc.custody, e), kp);
    save_sealed(acc, path, doc);

    std::cout << "sealed: appended custody event #" << (doc.custody.size() - 1)
              << " by " << actor << " (" << e.actor_id << ")\n"
              << "        binds " << doc.annotations.size() << " annotations\n";
    return 0;
}
#endif  // FOLIOEDIT_NO_CRYPTO

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
#ifdef FOLIOEDIT_HAVE_TUI
        return folioedit::run_tui();   // bare launch → the interactive human face
#else
        std::cout << fe::render_help();
        return 0;
#endif
    }

    const std::string verb = args[0];
    const std::vector<std::string> rest(args.begin() + 1, args.end());

    try {
        if (verb == "help" || verb == "--help" || verb == "-h") return cmd_help(rest);
        if (verb == "info")                                      return cmd_info(rest);
        if (verb == "annotate")                                  return cmd_annotate(rest);
        if (verb == "del")                                       return cmd_del(rest);
#ifndef FOLIOEDIT_NO_CRYPTO
        if (verb == "keygen")                                    return cmd_keygen(rest);
        if (verb == "seal")                                      return cmd_seal(rest);
#else
        if (verb == "keygen" || verb == "seal") {
            std::cerr << "folioedit: '" << verb << "' needs crypto; this build "
                         "(folioedit-plain) has none -- use the full folioedit\n";
            return 1;
        }
#endif
        std::cerr << "folioedit: unknown command '" << verb << "'\n"
                  << "try: folioedit help\n";
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << "\n";   // engine messages already carry the prefix
        return 2;
    }
}
