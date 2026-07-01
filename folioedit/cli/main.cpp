//
// folioedit CLI -- the step-6 face. Thin: it renders the shared Vocabulary and
// calls the engine. This first cut carries only the settled read-side verbs
// (help / keygen / info); the mutate verbs (annotate / seal) land once their
// custody semantics are decided. Pure STL + libfolioedit (libcrypto); no gtk.
//
#include "folioedit/Vocabulary.hpp"
#include "folioedit/Archive.hpp"
#include "folioedit/Identity.hpp"
#include "folioedit/Custody.hpp"
#include "folioedit/Format.hpp"
#include "folioedit/Seal.hpp"      // hex_to_key

#include <iostream>
#include <ctime>
#include <stdexcept>
#include <string>
#include <vector>

namespace fe = folioedit;

namespace {

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
fe::Document open_with(const Access& a, const std::string& path) {
    return a.pw ? fe::open_document_pw(path, a.secret)
                : fe::open_document(path, fe::hex_to_key(a.secret));
}
void save_with(const Access& a, const std::string& path, const fe::Document& doc) {
    if (a.pw) fe::save_document_pw(path, doc, a.secret);
    else      fe::save_document(path, doc, fe::hex_to_key(a.secret));
}

std::string now_iso() {
    std::time_t t = std::time(nullptr);
    std::tm tmv;
    gmtime_r(&t, &tmv);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tmv);
    return std::string(buf);
}

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

int cmd_keygen(const std::vector<std::string>& rest) {
    if (rest.empty()) { std::cerr << "usage: folioedit keygen <keyfile>\n"; return 1; }
    fe::KeyPair kp = fe::generate_keypair();
    fe::save_keypair(kp, rest[0]);
    std::cout << "wrote identity to " << rest[0] << "\n"
              << "actor_id (fingerprint): " << fe::fingerprint(kp.public_key) << "\n";
    return 0;
}

int cmd_info(const std::vector<std::string>& rest) {
    if (rest.empty()) { std::cerr << "usage: folioedit info <file> [--key hex | --pass pw]\n"; return 1; }
    const std::string& path = rest[0];

    fe::Envelope env = fe::read_envelope_file(path);
    std::cout << "file:   " << path << "\n"
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
    return 0;
}

int cmd_annotate(const std::vector<std::string>& rest) {
    if (rest.empty()) { std::cerr << "usage: folioedit annotate <file> --key|--pass ... --scene <iid> --kind <hat> --text <t> [--start N --end N --quote Q]\n"; return 1; }
    const std::string& path = rest[0];
    Access acc = resolve_access(rest);
    if (!acc.have) { std::cerr << "annotate: need --key <hex> or --pass <phrase>\n"; return 1; }

    std::string scene, kind, text, quote, sS, sE;
    opt(rest, "--scene", scene); opt(rest, "--kind", kind); opt(rest, "--text", text);
    opt(rest, "--quote", quote); opt(rest, "--start", sS);  opt(rest, "--end", sE);
    if (scene.empty() || kind.empty() || text.empty()) {
        std::cerr << "annotate: --scene, --kind and --text are required\n"; return 1;
    }

    fe::Document doc = open_with(acc, path);

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
    save_with(acc, path, doc);

    std::cout << "added annotation [" << (doc.annotations.size() - 1) << "] "
              << kind << " on " << scene << "\n";
    return 0;
}

int cmd_del(const std::vector<std::string>& rest) {
    if (rest.empty()) { std::cerr << "usage: folioedit del <file> --key|--pass ... --index <n>\n"; return 1; }
    const std::string& path = rest[0];
    Access acc = resolve_access(rest);
    if (!acc.have) { std::cerr << "del: need --key <hex> or --pass <phrase>\n"; return 1; }
    std::string idxs;
    if (!opt(rest, "--index", idxs)) { std::cerr << "del: --index <n> is required\n"; return 1; }

    fe::Document doc = open_with(acc, path);
    const std::size_t idx = static_cast<std::size_t>(std::stoul(idxs));
    if (idx >= doc.annotations.size()) { std::cerr << "del: index out of range\n"; return 1; }
    if (doc.annotations[idx].withdrawn) { std::cout << "annotation [" << idx << "] is already withdrawn\n"; return 0; }

    doc.annotations[idx].withdrawn = true;   // tombstone -- kept in the record
    save_with(acc, path, doc);
    std::cout << "withdrew annotation [" << idx << "] (kept as a tombstone in the record)\n";
    return 0;
}

int cmd_seal(const std::vector<std::string>& rest) {
    if (rest.empty()) { std::cerr << "usage: folioedit seal <file> --key|--pass ... --identity <keyfile> --actor <name>\n"; return 1; }
    const std::string& path = rest[0];
    Access acc = resolve_access(rest);
    if (!acc.have) { std::cerr << "seal: need --key <hex> or --pass <phrase>\n"; return 1; }
    std::string idpath, actor;
    opt(rest, "--identity", idpath); opt(rest, "--actor", actor);
    if (idpath.empty() || actor.empty()) { std::cerr << "seal: --identity <keyfile> and --actor <name> are required\n"; return 1; }

    fe::Document doc = open_with(acc, path);
    fe::KeyPair kp   = fe::load_keypair(idpath);

    fe::CustodyEvent e;
    e.kind = fe::CustodyEvent_Kind::Sealed;
    e.actor = actor;
    e.actor_id = fe::fingerprint(kp.public_key);   // set before finalize (bound)
    e.at = now_iso();
    e.binds = fe::annotations_hash(doc);
    fe::sign_event(fe::append_event(doc.custody, e), kp);
    save_with(acc, path, doc);

    std::cout << "sealed: appended custody event #" << (doc.custody.size() - 1)
              << " by " << actor << " (" << e.actor_id << ")\n"
              << "        binds " << doc.annotations.size() << " annotations\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) { std::cout << fe::render_help(); return 0; }

    const std::string verb = args[0];
    const std::vector<std::string> rest(args.begin() + 1, args.end());

    try {
        if (verb == "help" || verb == "--help" || verb == "-h") return cmd_help(rest);
        if (verb == "keygen")                                    return cmd_keygen(rest);
        if (verb == "info")                                      return cmd_info(rest);
        if (verb == "annotate")                                  return cmd_annotate(rest);
        if (verb == "del")                                       return cmd_del(rest);
        if (verb == "seal")                                      return cmd_seal(rest);
        std::cerr << "folioedit: unknown command '" << verb << "'\n"
                  << "try: folioedit help\n";
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << "\n";   // engine messages already carry the prefix
        return 2;
    }
}
