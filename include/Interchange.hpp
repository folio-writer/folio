#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — Interchange.hpp  (s103 — the Folio-side editorial-interchange glue)
//
// The seam between the studio and the folioedit engine for EXPORT: take the
// scenes the author picked + the pass parameters, build a folioedit::Document,
// bind + sign an `issued` custody event with the author's TOFU identity, seal it
// under the generated passphrase, and write the .folioedit briefing to disk. The
// caller (ExportDialog) then files the returned LedgerEntry into the project's
// InterchangeLedger.
//
// LAYERING: deliberately gtk-free (like DocumentModel and the pure engine). It
// links folioedit (hence libcrypto) but NOT gtkmm, and it does NOT include
// DocumentModel — the caller extracts plain scene data (iid/title/html/order)
// from BinderNode and hands it here. So this layer is a thin, testable glue over
// the engine, and the GTK dialog stays a face on it.
//
// The path resolution uses only the environment ($XDG_DATA_HOME / $HOME), no
// glib, so the layer carries no toolkit dependency at all.
//
// (DESIGN_editorialization §4.1 briefing-out, §7 anchoring, §16 custody /
//  §16.5 Interchange; s102 pure bricks: Format / Archive / Seal / Custody /
//  Identity / Passphrase + Folio::InterchangeLedger.)
// ─────────────────────────────────────────────────────────────────────────────
#include <functional>
#include <string>
#include <vector>

#include "folioedit/Anchor.hpp"     // folioedit::AnchorMethod (in InAnnotation)
#include "folioedit/Format.hpp"     // folioedit::Document (open_pass / absorb signatures)
#include "folioedit/Identity.hpp"   // folioedit::KeyPair
#include "InterchangeLedger.hpp"    // Folio::LedgerEntry

namespace Folio {
namespace Interchange {

// One scene going out in the briefing. The caller pulls these off the selected
// BinderNodes: `html` is node.content (the editor's HTML, so anchor offsets
// match the in-app buffer exactly — §7 / §14 offset-unit fork resolved).
struct OutScene {
    std::string iid;
    std::string title;
    std::string html;
    int         order = 0;
};

// What the author chose in the Export Interchange section.
struct PassSpec {
    std::string              project_id;      // informational (CLI `info` prints it)
    std::string              project_title;
    std::string              source;          // editor/recipient label — stamped on
                                              //   every annotation this pass makes
    std::vector<std::string> kinds;           // hats this pass may use
    std::string              rules;           // the briefing (default_rules() unless authored)
    std::string              author_label;    // custody actor label (DocumentModel.author)
};

// The fixed surface-not-verdict briefing shipped with every pass until an
// authoring UI lands (§6 / §17 fork 3 — "ship a sensible default now"). Pure
// data; safe to embed.
std::string default_rules();

// The author's TOFU identity keyfile:  <data>/folio/identity.key
//   <data> = $XDG_DATA_HOME, else $HOME/.local/share  (BackupManager precedent).
std::string identity_path();

// Load the identity, or generate + persist a fresh Ed25519 keypair on first use
// (creating the parent directory as needed). Throws on an I/O or crypto failure.
folioedit::KeyPair load_or_create_identity();

// Write a sealed .folioedit briefing to `path` and return the ledger record for
// it (status Sent). Builds the Document from `spec` + `scenes`, appends an
// `issued` custody event binding body_hash and signs it with `identity`, then
// seal_with_passphrase under `passphrase` (the generated display phrase — the
// seal canonicalizes at the door, so spacing/case never reach the key). Throws
// on any failure (I/O, crypto). The caller records the entry + marks the model
// modified so the ledger persists on the next project save.
LedgerEntry write_pass(const std::string&           path,
                       const PassSpec&              spec,
                       const std::vector<OutScene>& scenes,
                       const std::string&           passphrase,
                       const folioedit::KeyPair&    identity);

// Write an UNSEALED (plain, readable-JSON) .folioedit briefing to `path` and
// return its ledger record (status Sent, phrase empty). The trusted-AI carrier
// (§18.2): same Document as write_pass, but NO passphrase and NO Ed25519 signing
// — instead an UNSIGNED `issued` custody event binds body_hash, so Absorb can
// still prove the AI only appended annotations and never rewrote the prose
// (§18.5). No identity keypair is needed. Written via save_document_plain (an
// `instructions` block + pass.rules + scenes + an empty annotations array), so a
// chat can read/append it directly and Claude Code can open it with the plain
// CLI. Throws on I/O failure.
LedgerEntry write_pass_plain(const std::string&           path,
                             const PassSpec&              spec,
                             const std::vector<OutScene>& scenes);

// ─── the verdict RETURN (s107 §20.7) ──────────────────────────────────────────

// One annotation going BACK to the editor, carrying the author's verdict. The
// caller builds these from the model's current annotations for the pass (the same
// scene_iid/range/quote/kind/text it filed on import, plus the recorded verdict).
struct ReturnAnnotation {
    std::string scene_iid;
    int         range_start = 0;
    int         range_end   = 0;
    std::string quote;
    std::string kind;
    std::string text;
    std::string verdict;              // "proposed" | "accepted" | "declined"
    bool        withdrawn = false;
    // s108 §25 — the author's resolution half rides back so the editor sees it and
    // can add their key. Engine events (Resolver/bool/at); the caller maps the
    // model's string-keyed events into these.
    std::vector<folioedit::ResolutionEvent> resolution_log;
};

// Build + sign + seal the author's verdict return and write it to `path`. `sent`
// is the stashed carrier the author absorbed (ProjectBundle::read_carrier ->
// folioedit::from_json); its scenes + whole custody chain are kept and continued.
// `returned` replaces the annotations block (verdicts folded in); an author
// `sealed` event is appended (make_return_document), signed with `identity`, and
// the file is sealed under `passphrase` (reuse the pass's phrase — the editor
// already has it). Court-grade-continuous per §20.7. Throws on I/O / crypto error.
void write_return(const std::string&                   path,
                  const folioedit::Document&           sent,
                  const std::vector<ReturnAnnotation>& returned,
                  const std::string&                   author_label,
                  const std::string&                   passphrase,
                  const folioedit::KeyPair&            identity);

// s108 §21.2/§25.5 — "Send back to editor": like write_return, but the return
// carries the author's CURRENT (revised) prose so the fix travels. `current_scenes`
// is the manuscript as it stands now (the caller rebuilds it from the pass's
// inventory, keeping iid/title/order and swapping in live content). Builds via
// make_sendback_document (two authored links: re-issue the revised body + seal the
// verdict/resolution block), signs BOTH tail links, seals under `passphrase`. The
// editor's side gets revised prose + verdicts + the author's resolution half, and a
// signed body_v2-vs-body_v1 change indicator. Throws on I/O / crypto error.
void write_sendback(const std::string&                     path,
                    const folioedit::Document&             sent,
                    const std::vector<folioedit::Scene>&   current_scenes,
                    const std::vector<ReturnAnnotation>&   returned,
                    const std::string&                     author_label,
                    const std::string&                     passphrase,
                    const folioedit::KeyPair&              identity);

// ─── carrier sniff + plain open (§18.7 content-sniff) ─────────────────────────

// Which face a file on disk is, decided by its leading bytes (no key needed):
// Sealed = the AES envelope (leading MAGIC); Plain = readable JSON ('{'); Unknown
// = neither. Thin, testable wrapper over folioedit::peek_file_face so the GTK
// Absorb path dispatches without reaching into the engine directly.
enum class Carrier { Plain, Sealed, Unknown };
Carrier sniff(const std::string& path);

// Open an UNSEALED pass (no phrase). Throws if the file is not readable plain
// JSON. Pair with sniff(): Plain -> open_plain (no prompt); Sealed -> open_pass.
folioedit::Document open_plain(const std::string& path);

// ─── import / re-absorb (slice-2) ─────────────────────────────────────────────

// One returned annotation, re-anchored against the CURRENT manuscript and ready
// for the caller to file as a proposal onto its scene node.
struct InAnnotation {
    std::string          scene_iid;
    int                  range_start = 0;   // resolved codepoint offsets (0..0 if floating)
    int                  range_end   = 0;
    std::string          kind;              // Proofreader / Editor / Writer
    std::string          text;             // the comment
    std::string          source;           // = doc.pass.source (the editor's identity)
    std::string          verdict;          // s107 — the author's recorded verdict carried in the
                                           //   file ("proposed" default). Faithful to the engine's
                                           //   Annotation.verdict so a verdict-bearing RETURN pass
                                           //   surfaces each note's fate; the caller files it.
    folioedit::AnchorMethod method = folioedit::AnchorMethod::Floating;
    bool                 ambiguous  = false;  // quote matched >1 place; nearest chosen
};

// The outcome of re-absorbing a returned .folioedit.
struct AbsorbResult {
    std::string pass_id;                    // doc.pass.id — to mark_returned in the ledger
    std::string source;                     // doc.pass.source — the editor

    std::vector<InAnnotation> annotations;  // non-withdrawn, in file order

    bool chain_ok           = false;        // verify_chain(doc.custody)
    bool body_drift         = false;        // returned prose != what an Issued event bound
    bool annotations_drift  = false;        // returned block != what a Sealed event bound

    int exact = 0, requoted = 0, floating = 0, ambiguous_count = 0;
    int withdrawn = 0;                      // tombstones skipped
    int total     = 0;                      // annotations in the file (incl. withdrawn)
};

// Open a .folioedit by trying each candidate passphrase in order — the AES-GCM
// tag validates the right one. Returns the opened Document and (via `which`, if
// non-null) the phrase that worked. Throws std::runtime_error if NONE open
// (wrong phrases / not a .folioedit / corrupt); the GTK layer prompts for a
// phrase and retries by appending it to the candidate list.
folioedit::Document open_pass(const std::string&              path,
                              const std::vector<std::string>& candidate_phrases,
                              std::string*                    which = nullptr);

// Re-anchor + tally every (non-withdrawn) annotation in `doc` against the CURRENT
// scene text. `current_text(scene_iid)` returns the scene's live HTML now (the
// same buffer offsets), or "" if the scene is gone — a gone scene re-anchors to
// floating, and the caller drops it when the node can't be found. Also recomputes
// body/annotations hashes and compares them to the custody `binds` (the drift
// check the format carried but nothing checked until now). Pure; no filing.
AbsorbResult absorb(const folioedit::Document& doc,
                    const std::function<std::string(const std::string&)>& current_text);

}  // namespace Interchange
}  // namespace Folio
