#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// CustomMindMap.hpp — the OWNED mind map: a Reference *form*, a document (s50)
//
// The other half of the mind-map story. Two kinds of map, one rendering:
//   • MindMap     (s47/48) = the project LENS — generated from the binder, edges
//                  READ not owned, layout in-session. "The map can lie to itself,
//                  the manuscript can't be made to lie."
//   • CustomMindMap (here)  = an owned DOCUMENT — a fragment whose form is "Mind
//                  Map". It OWNS its nodes, its edges, its categories, its layout,
//                  and SERIALISES them with the fragment. This is the one place a
//                  free-form diagram (a "Who" label, a snippet, a "this explains
//                  that" link) has to live, because none of it is story truth.
//
// THE LINE (why this is not a second truth): a lens never stores; a document
// always does. Owned content here is addendum data — it rides ON a Reference and
// never enters the story graph (StoryGraph reads the s20-link + s44-relation
// indices; this is neither). The ONLY pointers at truth are read-only and never
// written back:
//   • subject_iids  — the project objects this whole map is ABOUT (many-to-many;
//                     the reverse "maps about this scene" is COMPUTED, not stored)
//   • a CMMNode of kind Anchor — a canvas node that POINTS AT a real object's iid
//   • (later) promotion of a link to a real story relation — the one ceremony,
//     not the default stroke. NOT in this slice.
// Everything else — text bodies, edges, edge categories, positions — is owned.
//
// SHARED WITH THE LENS: only PRESENTATION — MapGlyph (+ map_glyph_for), the
// MapViewport transforms, and the MindMapFrame data. NOT shared: the model. The
// owned node/edge types below are deliberately their OWN species so the two can
// never quietly merge into one store.
//
// PURE / GTK-free (nlohmann::json like ObjectIO/ModuleIO) — g++-compilable and
// unit-checkable in the sandbox. The GTK surface (toolbar + canvas) is a later,
// separate cut that CALLS this; no geometry or I/O policy lives in the widget.
// ─────────────────────────────────────────────────────────────────────────────

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Iid.hpp"        // IidKind, iid_kind_of — to glyph an Anchor by its target
#include "MindMap.hpp"    // MapGlyph, map_glyph_for, MapViewport, MindMapFrame (shared presentation)

namespace Folio {

using json = nlohmann::json;

// ── Node species — kept to TWO on purpose (the Scapple lesson) ───────────────
// A Text node is the workhorse: the centre topic, the W-labels, the snippets —
// all one kind, differing only by size/emphasis the surface chooses, never by
// type. An Anchor is the one special kind: it points at a real project object
// (its `iid`), so activating it opens that node and it glyphs as that kind.
enum class CMMNodeKind { Text, Anchor };

// ── CMMNode — a node OWNED by this document ──────────────────────────────────
// `id` is DOC-LOCAL (a "cmn_…" token minted here, NOT an IidKind — these are not
// project parts). Edges reference nodes by this id. For an Anchor, `iid` holds
// the real project iid it bridges to (and `title` may cache the object's name
// for display); for a Text node `iid` is empty and the content is `title`/`body`.
struct CMMNode {
    std::string id;                       // doc-local "cmn_…" (edge endpoints use this)
    CMMNodeKind kind = CMMNodeKind::Text;

    double x = 0.0, y = 0.0;              // authored canvas position (owned; the floor is free-flow)

    std::string title;                    // Text: the label; Anchor: cached object name (display)
    std::string body;                     // Text: optional inline prose (snippet); empty for Anchor

    std::string iid;                      // Anchor ONLY: the real object this points at ("" for Text)

    bool pinned    = false;               // reserved for a later rule layer (no rule owns position yet)
    bool collapsed = false;               // title-only vs title+body
};

// The glyph a node wears: a Text node is a Card; an Anchor borrows its target's
// kind glyph (Scene→Square, Character→Circle, Place→Pin, …). Presentation only.
MapGlyph cmm_node_glyph(const CMMNode& n);

// ── CMMEdge — a link OWNED by this document, carrying a typed category ────────
// `category` is FREE-TEXT (lore / skills / is-a / …), Scapple-loose — nothing
// compiled. The surface colours the edge by the label and surfaces recently-used
// ones (see CMMDoc::recent_categories). `directed` is a render hint (most links
// are associative/headless; an Anchor link reads gently directed toward truth).
struct CMMEdge {
    std::string id;                       // doc-local "cme_…"
    std::string from;                     // a CMMNode id
    std::string to;                       // a CMMNode id
    std::string category;                 // typed free-text ("" = uncategorised/structural)
    bool        directed = false;
};

// ── CMMDoc — the whole owned map (one Reference fragment's addendum) ──────────
struct CMMDoc {
    std::string id;                       // the fragment's iid ("ref_…") — the doc identity
    std::string name;                     // the map's title (mirrors the fragment title)

    // MANY-TO-MANY subjects: the project objects this map is ABOUT. Owned forward
    // refs to truth (read-only — opening navigates, never writes back). The
    // reverse ("which maps are about this object") is COMPUTED by a scan, never
    // stored here. Empty is normal (a pure research map like "Bene Gesserit").
    std::vector<std::string> subject_iids;

    std::vector<CMMNode> nodes;           // owned
    std::vector<CMMEdge> edges;           // owned

    std::vector<std::string> recent_categories;   // most-recent-first, deduped, capped
    MapViewport viewport;                          // restored on open (presentation)
};

// ── Doc-local id minting (NOT IidKinds — these never leave the document) ─────
std::string cmm_make_node_id();           // "cmn_…"
std::string cmm_make_edge_id();           // "cme_…"

// ── Construction helpers (pure; mutate the doc, return what was made) ─────────
// add_text/add_anchor mint an id, place at (x,y), append, and return the new id.
std::string cmm_add_text  (CMMDoc& d, const std::string& title, double x, double y,
                           const std::string& body = "");
std::string cmm_add_anchor(CMMDoc& d, const std::string& target_iid,
                           const std::string& cached_title, double x, double y);

// Add a categorised edge between two existing nodes; notes the category as a
// recent (so the surface's recents list stays warm). Returns the new edge id, or
// "" if an endpoint id is unknown or from==to.
std::string cmm_add_edge(CMMDoc& d, const std::string& from, const std::string& to,
                         const std::string& category, bool directed = false);

const CMMNode* cmm_find_node(const CMMDoc& d, const std::string& id);   // nullptr if absent

// ── Subjects (many-to-many; the forward side this doc owns) ──────────────────
bool cmm_has_subject   (const CMMDoc& d, const std::string& iid);
bool cmm_add_subject   (CMMDoc& d, const std::string& iid);   // dedup; false if already present
bool cmm_remove_subject(CMMDoc& d, const std::string& iid);   // false if not present

// ── Category recents (mirrors the colour-recents idiom: front, dedupe, cap) ──
constexpr int kCmmRecentCap = 12;
void cmm_note_category(CMMDoc& d, const std::string& category);

// ── Frame stamp — the HAND version of five_ws_frame() ────────────────────────
// The computed lens frame places edge-bound slots around a focal node; HERE the
// same MindMapFrame data is used as a STAMP: it drops a radial fan of labelled
// Text nodes (one per slot) around a centre and links each to the centre with a
// structural (uncategorised) edge — a starting skeleton the author then hangs
// snippets off. The centre is an Anchor on `subject_iid` when one is given (so
// "map a scene → fan the five W's off it" is one motion, and the subject is
// registered on the doc); otherwise a Text node named `center_label`.
//
// Owned, free-flow: the nodes are placed at authored coords once; no rule drives
// them afterward (unlike the live lens). Returns the centre node's id.
std::string cmm_stamp_frame(CMMDoc& d, const MindMapFrame& frame,
                            double cx, double cy, double ring,
                            const std::string& subject_iid = "",
                            const std::string& center_label = "What");

// ── Round-trip (json-native, like ObjectIO) ──────────────────────────────────
json   cmm_to_json(const CMMDoc& d);
CMMDoc cmm_from_json(const json& j);
std::string cmm_to_string(const CMMDoc& d, bool pretty = true);
CMMDoc      cmm_from_string(const std::string& text);

}  // namespace Folio
