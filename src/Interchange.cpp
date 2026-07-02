// ─────────────────────────────────────────────────────────────────────────────
// Folio — Interchange.cpp  (s103)
//
// The export half of the editorial round-trip. Pure glue over the folioedit
// engine — no gtkmm. See Interchange.hpp for the contract.
// ─────────────────────────────────────────────────────────────────────────────
#include "Interchange.hpp"

#include "folioedit/Format.hpp"     // Document / Scene / Pass / Annotation
#include "folioedit/Archive.hpp"    // save_document_pw / open_document_pw / body_hash / annotations_hash
#include "folioedit/Anchor.hpp"     // reanchor / AnchorResult / AnchorMethod
#include "folioedit/Custody.hpp"    // CustodyEvent / append_event / verify_chain
// Identity.hpp comes in via Interchange.hpp (fingerprint / sign_event / keypair I/O)

#include <cstdlib>       // std::getenv
#include <ctime>         // std::time / gmtime_r
#include <filesystem>
#include <random>
#include <stdexcept>

namespace fs = std::filesystem;
namespace fe = folioedit;

namespace Folio {
namespace Interchange {
namespace {

// ISO-8601 UTC, matching the CLI's now_iso() so timestamps read consistently
// across the author face and the editor face.
std::string now_iso() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
    gmtime_r(&t, &tmv);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tmv);
    return std::string(buf);
}

// A short random suffix (Crockford-ish base32, no vowels-collision worries here —
// this is an opaque id, not an iid). Mirrors the iid entropy grain (8 chars) so
// pass ids read like the rest of the cross-layer tokens: "pass_k3f9a2b7".
std::string rand_suffix(int n = 8) {
    static const char* kAlpha = "0123456789abcdefghjkmnpqrstvwxyz";  // 32 symbols
    std::random_device rd;
    std::string out;
    out.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) out += kAlpha[rd() % 32u];
    return out;
}

// $XDG_DATA_HOME, else $HOME/.local/share. Empty only if neither is set.
fs::path data_home() {
    if (const char* x = std::getenv("XDG_DATA_HOME"); x && *x) return fs::path(x);
    if (const char* h = std::getenv("HOME");          h && *h) return fs::path(h) / ".local" / "share";
    return {};
}

}  // namespace

std::string default_rules() {
    // The surface-not-verdict law, written into the carrier (§6). Bounds any
    // editor — especially an AI — to SURFACING FACTS the author can act on, never
    // issuing judgments. Kept plain-text and self-contained so it travels intact.
    return
        "You are marking up a manuscript for its author. File your notes as "
        "range-anchored annotations only; never rewrite the prose.\n"
        "\n"
        "The one law: SURFACE FACTS, DO NOT ISSUE VERDICTS.\n"
        "  Allowed  (facts the author can check): \"Duncan has brown eyes in ch. 3 "
        "and blue in ch. 9.\"  \"'suddenly' appears 11 times in this scene.\"  "
        "\"This reveal has no earlier plant.\"\n"
        "  Refused  (judgments): \"Weak scene.\"  \"Flat dialogue.\"  \"The climax is "
        "underwhelming.\"  If a verdict is tempting, surface the structural fact "
        "beneath it and let the author judge.\n"
        "\n"
        "Stay inside the hats this pass allows:\n"
        "  Proofreader — echoes, filter words, repetition, dialogue-tag overuse, "
        "grammar. Range-anchored, checkable.\n"
        "  Editor (continuity) — contradictions in character, object, place, or "
        "timeline across scenes; a thing set up and never paid off.\n"
        "  Writer (taste) — be nearly silent. The taste is the author's; do not "
        "borrow their authority.\n"
        "\n"
        "Anchor every note to the exact words it is about. Your notes are "
        "proposals the author accepts or discards — they inform; the author decides.";
}

std::string identity_path() {
    fs::path base = data_home();
    if (base.empty()) return "identity.key";           // last-ditch: cwd
    return (base / "folio" / "identity.key").string();
}

fe::KeyPair load_or_create_identity() {
    const std::string path = identity_path();
    std::error_code ec;
    if (fs::exists(path, ec) && !ec)
        return fe::load_keypair(path);

    // First interchange export on this machine: mint + persist the author's key.
    fs::path p(path);
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    fe::KeyPair kp = fe::generate_keypair();
    fe::save_keypair(kp, path);                         // throws on I/O failure
    return kp;
}

LedgerEntry write_pass(const std::string&           path,
                       const PassSpec&              spec,
                       const std::vector<OutScene>& scenes,
                       const std::string&           passphrase,
                       const fe::KeyPair&           identity) {
    const std::string pass_id = "pass_" + rand_suffix();
    const std::string stamp   = now_iso();

    // ── build the briefing Document ──────────────────────────────────────────
    fe::Document doc;
    doc.project_id    = spec.project_id;
    doc.project_title = spec.project_title;
    doc.version_stamp = stamp;                          // human "which version";
                                                        //   real drift check is body_hash below
    doc.pass.id     = pass_id;
    doc.pass.source = spec.source;                      // stamped on the editor's annotations
    doc.pass.kinds  = spec.kinds;
    doc.pass.rules  = spec.rules;

    for (const OutScene& s : scenes) {
        fe::Scene sc;
        sc.iid   = s.iid;
        sc.title = s.title;
        sc.order = s.order;
        sc.text  = s.html;                              // HTML — offsets match the buffer
        doc.scenes.push_back(std::move(sc));
    }

    // ── bind + sign the `issued` custody event ─────────────────────────────────
    const std::string body = fe::body_hash(doc);        // over the sent scene versions
    fe::CustodyEvent e;
    e.kind     = fe::CustodyEvent_Kind::Issued;
    e.actor    = spec.author_label.empty() ? "author" : spec.author_label;
    e.actor_id = fe::fingerprint(identity.public_key);  // MUST be set before finalize
    e.at       = stamp;
    e.binds    = body;
    fe::sign_event(fe::append_event(doc.custody, e), identity);

    // ── seal + write ───────────────────────────────────────────────────────────
    fe::save_document_pw(path, doc, passphrase);        // PBKDF2 + AES-256-GCM

    // ── the ledger record (the caller files it into the project) ──────────────
    LedgerEntry le;
    le.id         = pass_id;
    le.recipient  = spec.source;
    le.phrase     = passphrase;                         // display form (seal canonicalizes)
    le.created_at = stamp;
    le.body_hash  = body;
    for (const OutScene& s : scenes)
        le.inventory.push_back({ s.iid, s.title });
    le.status = PassStatus::Sent;
    return le;
}

// ─── import / re-absorb ───────────────────────────────────────────────────────

LedgerEntry write_pass_plain(const std::string&           path,
                             const PassSpec&              spec,
                             const std::vector<OutScene>& scenes) {
    const std::string pass_id = "pass_" + rand_suffix();
    const std::string stamp   = now_iso();

    // ── build the briefing Document (identical body to the sealed path) ───────
    fe::Document doc;
    doc.project_id    = spec.project_id;
    doc.project_title = spec.project_title;
    doc.version_stamp = stamp;

    doc.pass.id     = pass_id;
    doc.pass.source = spec.source;
    doc.pass.kinds  = spec.kinds;
    doc.pass.rules  = spec.rules;

    for (const OutScene& s : scenes) {
        fe::Scene sc;
        sc.iid   = s.iid;
        sc.title = s.title;
        sc.order = s.order;
        sc.text  = s.html;
        doc.scenes.push_back(std::move(sc));
    }

    // ── bind an UNSIGNED `issued` event (integrity without encryption, §18.5) ──
    // No Ed25519: the plain face is for a trusted AI the author drives directly,
    // so possession-of-passphrase and whose-hand signatures add nothing. The
    // body_hash bind is what matters — Absorb recomputes it to prove only-append.
    const std::string body = fe::body_hash(doc);
    fe::CustodyEvent e;
    e.kind     = fe::CustodyEvent_Kind::Issued;
    e.actor    = spec.author_label.empty() ? "author" : spec.author_label;
    e.actor_id = "tofu:local";          // no signing key in the plain face
    e.at       = stamp;
    e.binds    = body;
    fe::append_event(doc.custody, e);   // finalized, UNSIGNED

    // ── write readable JSON (no envelope, no crypto) ──────────────────────────
    fe::save_document_plain(path, doc);

    // ── ledger record: phrase empty marks an unsealed pass ────────────────────
    LedgerEntry le;
    le.id         = pass_id;
    le.recipient  = spec.source;
    le.phrase     = "";                 // unsealed — no passphrase (surface shows "—")
    le.created_at = stamp;
    le.body_hash  = body;
    for (const OutScene& s : scenes)
        le.inventory.push_back({ s.iid, s.title });
    le.status = PassStatus::Sent;
    return le;
}

Carrier sniff(const std::string& path) {
    switch (fe::peek_file_face(path)) {
        case fe::FileFace::Plain:  return Carrier::Plain;
        case fe::FileFace::Sealed: return Carrier::Sealed;
        case fe::FileFace::Unknown: break;
    }
    return Carrier::Unknown;
}

fe::Document open_plain(const std::string& path) {
    return fe::open_document_plain(path);
}

fe::Document open_pass(const std::string&              path,
                       const std::vector<std::string>& candidate_phrases,
                       std::string*                    which) {
    for (const std::string& p : candidate_phrases) {
        if (p.empty()) continue;
        try {
            fe::Document doc = fe::open_document_pw(path, p);   // GCM tag validates the key
            if (which) *which = p;
            return doc;
        } catch (...) {
            // wrong phrase for this file (tag failure) — try the next
        }
    }
    throw std::runtime_error(
        "folioedit: none of the known passphrases opened this file");
}

AbsorbResult absorb(const fe::Document& doc,
                    const std::function<std::string(const std::string&)>& current_text) {
    AbsorbResult r;
    r.pass_id = doc.pass.id;
    r.source  = doc.pass.source;
    r.chain_ok = fe::verify_chain(doc.custody);

    // Drift: recompute the content hashes and compare to what the custody chain
    // bound. `issued` bound the sent body; `sealed` bound the returned block. A
    // mismatch means the file's own content differs from what an event committed
    // to (an edit/tamper in transit) — distinct from author-side drift, which
    // the per-annotation re-anchor handles via the carried quote.
    const std::string body = fe::body_hash(doc);
    const std::string anns = fe::annotations_hash(doc);
    for (const fe::CustodyEvent& e : doc.custody) {
        if (e.kind == fe::CustodyEvent_Kind::Issued && !e.binds.empty() && e.binds != body)
            r.body_drift = true;
        if (e.kind == fe::CustodyEvent_Kind::Sealed && !e.binds.empty() && e.binds != anns)
            r.annotations_drift = true;
    }

    r.total = static_cast<int>(doc.annotations.size());
    for (const fe::Annotation& a : doc.annotations) {
        if (a.withdrawn) { ++r.withdrawn; continue; }   // tombstone — not filed

        const std::string cur = current_text(a.scene_iid);
        fe::AnchorResult ar = fe::reanchor(cur, a.range_start, a.range_end,
                                           a.quote, !r.body_drift);

        InAnnotation ia;
        ia.scene_iid   = a.scene_iid;
        ia.range_start = ar.range_start;
        ia.range_end   = ar.range_end;
        ia.kind        = a.kind;
        ia.text        = a.text;
        ia.source      = doc.pass.source;   // identity is the pass's, shared by all its notes
        ia.method      = ar.method;
        ia.ambiguous   = ar.ambiguous;

        switch (ar.method) {
            case fe::AnchorMethod::Offset:   ++r.exact;    break;
            case fe::AnchorMethod::Quote:    ++r.requoted; break;
            case fe::AnchorMethod::Floating: ++r.floating; break;
        }
        if (ar.ambiguous) ++r.ambiguous_count;

        r.annotations.push_back(std::move(ia));
    }
    return r;
}

}  // namespace Interchange
}  // namespace Folio
