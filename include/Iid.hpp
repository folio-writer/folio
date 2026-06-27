#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Iid.hpp — stable cross-layer part identity (s19 block-in)
//
// DESIGN BLOCK-IN — not yet wired to DocumentModel, the project format, GTK
// naming, or FolioLog. Defines the *shape* of an iid so we can react to it
// before it threads through everything. See DESIGN_s19 §4.
//
// THE ONE IDEA: a single immutable token per part (scene, character, place,
// reference, asset, snapshot) that is the SAME string across every layer —
//   • model : the BinderNode/Snapshot/asset carries its iid
//   • disk  : content/<iid>.md, meta/<iid>.json, assets/<iid>.<ext>
//   • GTK   : widget named with the iid  (scene-row-<iid>)
//   • log   : FolioLog lines carry the iid / widget name
// so one log line ties widget ↔ model node ↔ file ↔ log entry into one thread.
// This replaces today's POSITIONAL identity (sidebar_selected_path = child
// indices), where reordering renames everything and nothing has a durable
// handle. The iid survives rename, reorder, and moving between sections.
//
// SCHEME: <type>_<base32 random>   e.g.  scn_k3f9a2b7
//   • The TYPE PREFIX makes a filename / log line / widget name self-describing
//     at a glance — the single most useful debugging property (you see "scn_…"
//     and know it's a scene without a lookup).
//   • The RANDOM SUFFIX (not a monotonic counter) avoids collisions across
//     INDEPENDENTLY-EDITED copies — load-bearing for the future handoff/merge
//     world, where two people both adding a node must not both mint the same id.
//   • Lowercase Crockford base32 (no i/l/o/u) keeps ids case-insensitive,
//     filename-safe, and unambiguous to read aloud / in a log.
//
// ONE GENERATOR, ONE FORMAT — defined here and reused for disk names, widget
// names, and log tokens. There is deliberately NO separate "runtime id": a
// second id system would sever the cross-layer link that is the entire point.
//
// GTK NAMING RULE (stated here so it rides with the identity it depends on):
//   • every widget gets a GTK name (set_name) — traceable in the inspector/CSS/log
//   • every widget that REPRESENTS a model object is named by that object's iid
//     via widget_name(kind, iid)  →  "scene-row-scn_k3f9a2b7"
//   • structural chrome gets a plain descriptive name ("export-format-dd")
//   Name everything; iid-name the model-bound things. Not 4,000 pointless iids.
//
// PURE / dependency-free (<string>, <random>, <cstdint>) so it is
// g++-compilable and unit-checkable in the Claude sandbox. No GTK, no GLib.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>
#include <string>

namespace Folio {

// ── Part kinds ───────────────────────────────────────────────────────────────
// The vocabulary of things that get an iid. Prefix tokens are 3 chars, stable,
// and MUST NOT be renumbered/reused (saved ids embed them). Extend by appending.
enum class IidKind {
    Scene,        // scn — a manuscript document/scene leaf
    Group,        // grp — a binder group (Book/Part/Chapter/folder)
    Character,    // chr
    Place,        // plc
    Reference,    // ref
    Template,     // tpl
    Asset,        // ast — image / cover / research binary
    Snapshot,     // snp — a node's version-history entry
    KeyPoint,     // kp  — a Key-Point palette swatch (a named/coloured beat id)
    Unknown,      // unk — fallback / migration placeholder
};

// type token ⇄ kind (stable strings; the saved id embeds the token)
const char* iid_prefix(IidKind k);          // IidKind::Scene -> "scn"
IidKind     iid_kind_of(const std::string& iid); // parses the prefix; Unknown if unrecognised

// ── Generation ───────────────────────────────────────────────────────────────
// Mint a fresh, unique iid for a part of the given kind:  "scn_k3f9a2b7".
// `entropy_chars` controls the random-suffix length (default 8 → 40 bits, ample
// for a single project and small enough to read in a log). Thread-unaware by
// design: callers mint on the UI thread.
std::string make_iid(IidKind kind, int entropy_chars = 8);

// ── Validation / parsing ─────────────────────────────────────────────────────
// True iff `s` is a well-formed iid: "<prefix>_<base32...>" with a known prefix
// and a non-empty lowercase-base32 suffix. Used at load to detect pre-iid
// (positional) data that needs an id minted during v4→v5 migration.
bool is_iid(const std::string& s);

// The random suffix without the prefix ("scn_k3f9a2b7" -> "k3f9a2b7"), or "".
std::string iid_suffix(const std::string& iid);

// ── GTK widget naming (the rule above, as a helper) ──────────────────────────
// Compose a widget name for a model-bound widget:
//   widget_name("scene-row", "scn_k3f9a2b7") -> "scene-row-scn_k3f9a2b7"
// `kind_label` is the widget role ("scene-row", "character-card", …). When the
// iid is empty this returns just the role (so the same helper serves structural
// chrome: widget_name("export-format-dd", "") -> "export-format-dd").
std::string widget_name(const std::string& role, const std::string& iid = "");

// Recover the iid embedded in a widget name, or "" if none.
//   iid_from_widget_name("scene-row-scn_k3f9a2b7") -> "scn_k3f9a2b7"
std::string iid_from_widget_name(const std::string& name);

}  // namespace Folio
