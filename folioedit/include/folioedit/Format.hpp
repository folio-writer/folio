#pragma once
//
// folioedit :: Format -- the plaintext that gets sealed: the three top-level
// parts (body + annotations + custody), and their JSON round-trip.
//
//   body        -- selected scenes' text (HTML for offset fidelity) + the
//                  AI-only `pass` briefing (rules the human CLI never renders).
//   annotations -- the appended return block; ONE shape whether AI or CLI wrote
//                  it; each entry carries BOTH range and quote so import can
//                  re-anchor offset -> quote -> floating (s7).
//   custody     -- the hash-chained trail (Custody.hpp).
//
// This is the engine's OWN annotation shape -- deliberately not Folio's
// DocumentModel::Annotation. The engine never links DocumentModel; Folio's
// import glue maps between the two. (DESIGN_editorialization s4 / s16.7.)
//
// nlohmann-only + STL. Pure, sandbox-testable end to end.
//
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

#include "folioedit/Custody.hpp"

namespace folioedit {

using json = nlohmann::json;

// -- body ---------------------------------------------------------------------
struct Scene {
    std::string iid;      // stable scene id (anchors are local to this)
    std::string title;
    int         order = 0;
    std::string text;     // HTML (offsets match Folio's in-app buffer)
};

struct Pass {
    std::string              id;      // this pass's id
    std::string              source;  // editor identity stamped on every annotation
    std::vector<std::string> kinds;   // hats this pass may use
    std::string              rules;   // AI-only briefing (surface-not-verdict law)
};

// The author's verdict on a proposal (s107). A proposal has a LIFECYCLE, not a
// binary: it never disappears, it transitions and renders differently per face.
// `Proposed` is the default and the wire-omitted state (a fresh editor pass is
// all-proposed); `Accepted`/`Declined` are the author's recorded decisions that
// ride the return pass back to the editor. The state WORD is stored; the glyph
// is a per-face concern (GTK: balloon / green-check / red-cross; TUI stand-ins).
// (DESIGN_editorialization s20.)
enum class Verdict {
    Proposed,   // default -- awaiting the author's decision (omit-when-default)
    Accepted,   // author will address it (prose edit is the author's own next work)
    Declined,   // author rejects the criticism -- prose unchanged, the "no" is on record
};

// enum <-> string (single source of truth; Format reuses these for JSON).
std::string verdict_to_str(Verdict v);
Verdict     verdict_from_str(const std::string& s);

// §22 — the note's RESOLUTION: the terminal "this thread is finished" state, kept
// ORTHOGONAL to the verdict (a note is resolved BY being accepted or declined; the
// verdict records why, the resolution records done-ness). It is NOT a lossy scalar:
// each Resolve/Reopen is an append-only, court-visible event (actor + timestamp),
// and the effective open/resolved state is DERIVED from the log so the argument
// (resolved -> reopened -> resolved) is never forgotten. (DESIGN_editorialization §22.)
enum class Resolver { Author, Editor };   // who acted; the AUTHOR is authoritative (§22.2)

std::string resolver_to_str(Resolver r);
Resolver    resolver_from_str(const std::string& s);   // unknown/absent -> Editor (least authority)

struct ResolutionEvent {
    Resolver    by;                // author | editor
    bool        resolved = true;   // true = resolved, false = reopened
    std::string at;                // ISO-8601 (UTC, fixed width) -- lexical order == time order
};

// The §25.1 effective state of a note's two-key handshake, derived from the
// append-only log. Resolved requires BOTH sides; a half is shown but does NOT
// archive; the author's reopen is authoritative (§25.2).
enum class ResolutionState { Open, HalfAuthor, HalfEditor, Resolved };

// The §25.3 rule, pure: author reopen -> Open (authoritative override); both
// resolve -> Resolved; author-only -> HalfAuthor (waiting on editor); editor-only
// -> HalfEditor (author's turn); else Open. "Latest" per side is by `at` (ties ->
// later in log). This replaces the old boolean.
ResolutionState resolution_state(const std::vector<ResolutionEvent>& log);

// Convenience: fully resolved (both keys) -- the only state that recedes/archives.
inline bool resolution_resolved(const std::vector<ResolutionEvent>& log) {
    return resolution_state(log) == ResolutionState::Resolved;
}

// -- annotations (the return block) -------------------------------------------
struct Annotation {
    std::string scene_iid;
    int         range_start = 0;   // char offsets WITHIN that scene
    int         range_end   = 0;
    std::string quote;             // exact spanned text -- re-anchor fallback
    std::string kind;              // hat: Proofreader / Editor / Writer
    std::string text;              // the comment
    bool        withdrawn = false; // a `del`'d annotation is a TOMBSTONE, not a
                                   // deletion: it stays in the block (so the seal
                                   // commits that it existed and was withdrawn --
                                   // a court-visible trace), just marked withdrawn.
    Verdict     verdict = Verdict::Proposed;  // s107 -- the author's recorded decision.
                                   // Bound into annotations_hash (only when non-default)
                                   // the way `withdrawn` is: an accept/decline is
                                   // court-visible history once it rides the return.
    std::vector<ResolutionEvent> resolution_log{};  // §22 -- the note's resolution trail.
                                   // Empty == open (the default). Omit-when-empty in JSON
                                   // AND in annotations_hash, so a pre-§22 block hashes
                                   // byte-identically -- no format bump. Effective
                                   // open/resolved is resolution_resolved(resolution_log).
};

// -- the whole sealed document ------------------------------------------------
struct Document {
    std::string project_id;
    std::string project_title;
    std::string version_stamp;              // drift detection on import

    Pass                      pass;
    std::vector<Scene>        scenes;        // body
    std::vector<Annotation>   annotations;   // appended return block
    std::vector<CustodyEvent> custody;       // hash-chained trail
};

// JSON round-trip (defined in Format.cpp). Byte-stable field order so a resealed
// file diffs cleanly.
json     to_json(const Document& doc);
Document from_json(const json& j);

}  // namespace folioedit
