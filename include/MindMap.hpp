#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// MindMap.hpp — the canvas lens: nodes placed in space, reflowed by opt-in rules
//                                                        (s47 block-in, header-first)
//
// DESIGN BLOCK-IN — not yet wired. Defines the *shape* of the mind-map lens so we
// can react before it threads through the editor. Companion to StoryGraph.hpp:
//   • StoryGraph = the graph READING (typed edges + scene marks; what's related).
//   • MindMap    = the canvas LENS over that graph (where things sit, how they're
//                  grouped, which shape they wear). It owns PRESENTATION, never
//                  truth. See DESIGN_scrapbook.md (the relief) and the s46/s47
//                  conversation that worked out the rule block and the W-frame.
//
// THE ONE DISCIPLINE, restated for this layer (node-is-truth, lens-is-projection):
// A MindMapItem does NOT hold a fragment's content, its edges, or its identity —
// it holds an `iid` POINTING AT the real node plus where the author put it on a
// canvas. Content lives on the Reference (or Scene/Character/Place) node; edges
// live on the nodes and in the s20+s44 index (read via StoryGraph). Draw a line
// on this canvas and you author a real one-directional relation THERE, not here —
// the backlink is computed (the relief), never a second stored edge. This is what
// lets a map be saved, filtered, reflowed, or thrown away without ever risking the
// story. The map can lie to itself all day; the manuscript can't be made to lie.
//
// WHY REFERENCES FIRST (Scott, s47): the scrapbook is the catch-all the canvas was
// built for — a loose fragment with no connection yet is the normal case, and the
// map is where it gets connected. So the item is blocked in iid-GENERAL (any node
// can be placed — the map spans manuscript objects AND loose scraps, DESIGN R30),
// with the Reference fragment as the driving first case.
// ─────────────────────────────────────────────────────────────────────────────

#include <string>
#include <utility>
#include <vector>

#include "Iid.hpp"          // IidKind, iid_kind_of, widget_name
#include "StoryGraph.hpp"   // EdgeKind / StoryEdge — the connection layer (reused, not redefined)

namespace Folio {

// ── The glyph — shape carries kind, vocabulary kept deliberately small ───────
// At-a-glance encoding so the author reads the web without reading labels. The
// Scapple lesson, taken on purpose: a SMALL set earns its keep; a large one is
// procrastination bait. Five shapes, no more, until a real need forces a sixth.
//
// Shape is DERIVED, never stored on the item (presentation computed from truth):
//   • Scene/Character/Place resolve straight from IidKind (square / circle / pin).
//   • A Reference resolves from its FRAGMENT FORM, not just "it's a reference" —
//     a text note is a Card, a captured source/link is a Clip, an image is a
//     Thumb. That finer mapping is per-form work (the form→glyph table lands with
//     the capture/forms slice); until then a Reference defaults to Card.
enum class MapGlyph {
    Square,   // Scene      — the prose, the fired clay
    Circle,   // Character  — a person/agent
    Pin,      // Place      — a location
    Card,     // Note / loose scrap — the associative play-dough (Reference default)
    Clip,     // Reference source / captured link — a thing with provenance
    Thumb,    // Image fragment — its own thousand-word story
};

// Pure, GTK-free. For References the caller passes the fragment form (later); the
// IidKind-only overload is the manuscript-object path and the safe default.
MapGlyph map_glyph_for(IidKind kind);                       // Scene→Square, etc.

// ── MindMapItem — THE FLAGSHIP: one node placed on one canvas ────────────────
// A reference fragment (or any node) as it appears on a map. Pure data; keyed by
// the target node's iid so activating the rendered glyph opens the SAME file the
// editor/sidebar would (widget_name("map-node", iid) — the one Iid naming rule).
struct MindMapItem {
    std::string iid;            // the real node this item SHOWS (ref_… first case)

    // AUTHORED position — the hand-placed spot, in canvas units. Persisted, and
    // ALWAYS kept even while a reflow rule is driving the visible layout, so that
    // turning rules off restores exactly where the author left things (lossless —
    // store the authored, compute the projected; same move as the relief).
    double x = 0.0;
    double y = 0.0;

    // PINNED — the manual exception to reflow. A pinned item is a fact the rule
    // routes AROUND: rules arrange everything else, this one stays where the hand
    // put it. Keeps Scapple-style placement available INSIDE a structured map,
    // not only with all rules off. (Default false: an unpinned item flows.)
    bool pinned = false;

    // Per-map display state (presentation only; never touches the node). Collapsed
    // shows title-only; expanded shows the fragment's body/preview inline.
    bool collapsed = false;

    // s48 — CONTAINMENT for the balloon (radial) layout. The iid of the node this
    // item nests INSIDE on the map: a Scene's Chapter, a Chapter's Part, a Part's
    // Project (or a character's role-group). "" = a top-level node (a root cloud /
    // a loose scrap floating in the centre). This is a LAYOUT hint the canvas fills
    // from the binder tree (and, for the centre clusters, from a synthetic hub /
    // role grouping) — it is presentation, not a second copy of binder truth: the
    // tree is read each rebuild, never stored here as authority.
    std::string parent_iid;

    // A loose scrap with no edges is allowed and normal — it just floats. There is
    // deliberately no "connect me" prompt and no unfiled-count nag (DESIGN §5):
    // latency is a feature, the float is where what-ifs are born.
};

// ── The reflow rule block — structure is OPT-IN, free-flow is the floor ──────
// Scott's resolution to the layout fork: free-flow (every node equal, place
// anything anywhere) is the default; switch rules ON and the map snaps into a
// shape; switch them all OFF and you're back to the blank infinite sheet. The
// rule stack is the DIAL between hand-placed and auto, so nobody must choose one.
//
// This also collapses the StoryGraph "four lenses": manuscript-order, lane-by-
// kind, radial-from-focus are PRESETS of one canvas, not four hard-coded views.
//
// COMPOSITION (so rules never fight): a rule has a ROLE. Exactly one Position and
// one Lane rule may be active (they own the axes — radio); Pull and Style rules
// stack freely (checkbox). Any combination is then predictable, not a tug-of-war.
enum class RuleRole {
    Position,   // owns the main layout axis  (manuscript order / chronology / radial / hierarchy)
    Lane,       // clusters/bands on a second axis (by kind / thread / timeframe / signpost)
    Pull,       // soft gravity along an edge kind (Involves pulls chars to their scenes…)
    Style,      // re-skins only — color/size/glyph; moves NOTHING
};

struct ReflowRule {
    std::string id;                 // stable key ("pos.manuscript", "lane.kind", …)
    RuleRole    role = RuleRole::Style;
    bool        enabled = false;
    std::string param;              // role-specific (e.g. Lane→"thread", Pull→edge kind)
};

// THE SAFETY LINE (what makes the block a lens and not a second truth): a rule may
// touch ONLY position, color, size, glyph, and visibility. It may HIDE a node
// (filter) but never delete it; it never edits an edge, a field, or binder order.
// LOCKED (s47, "continue"): LIVE constraint — an enabled rule re-snaps the flow
// continuously, so while it's on the hand is free only on PINNED items (above);
// the master off-switch drops the whole map back to free-flow instantly. A
// one-shot "tidy once, then hand-edit" mode may come later as a convenience, but
// the live engine is the model the canvas is built on.

// ── The W-frame — a map that restructures INTO a form (s47 keystone) ─────────
// Scott's old habit, generalized: a per-scene who/what/where/when/why map. Apply
// the frame to a focal node and the canvas becomes a FORM whose fields are placed
// in space instead of stacked in rows — position-encodes-ROLE, the literal "map
// restructures into a form." Each slot binds to an edge kind (or field), so the
// frame is computed from edges that already exist (always true, self-updating —
// strictly better than the hand-drawn maps that drifted on a rename).
//
// The empty slot is the point: a blank "Why" zone is a surfaced FACT (this scene
// has no motivation linked yet) — never a verdict — and double-clicking it
// AUTHORS the missing edge. You read the core and build the graph in one motion.
// A slot is filled one of two ways: by NEIGHBOURS reached over an edge kind
// (Who/Where/Why), or by a VALUE read off the focal node itself (When ← a Date
// field). What is neither — it's the focal node at the centre.
enum class SlotSource { Edge, Field };
struct FrameSlot {
    std::string label;                          // "Who", "Where", "When", "Why"
    SlotSource  source = SlotSource::Edge;
    EdgeKind    edge   = EdgeKind::Reference;    // when source == Edge (Who←Involves, …)
    std::string field_id;                        // when source == Field (a field on the focal node)
};
struct MindMapFrame {
    std::string name;                 // "Five W's" (shipped default), "Goal/Conflict/Disaster", …
    std::vector<FrameSlot> slots;     // ordered → zone placement
};

// The shipped default frame (data, not code — the author may keep others). The full
// five W's: What = the focal node at the centre (not a slot); Who←Involves,
// Where←SetIn (edge-bound); When ← the focal node's Date field "date" (field-bound);
// Why←Foreshadow (edge-bound; gains `motivation` once the scrapbook edge kinds are
// added to EdgeKind).
MindMapFrame five_ws_frame();
// LOCKED (s47, "continue"): author-definable frame KIT — the Five W's ships as
// the default frame, but a frame is data (a named slot set bound to edge kinds),
// so the author can keep a "Goal/Conflict/Disaster" or "POV/stakes/turn" frame and
// apply whichever fits the scene in front of them. Consistent with the spine:
// types are data, authored not compiled.

// ── MindMap — a saved view: a VIEWPORT over the one graph, never an edge store ─
// A named, kept arrangement (Obsidian saves a .canvas as its own file of nodes AND
// edges — Folio must NOT: the edges live on the nodes). A saved map stores the
// author's lens — which nodes, where, at what zoom, under which rules/frame — so a
// line drawn in one map shows in the editor and every other map at once (R21).
struct MindMap {
    std::string id;                   // map_… (a kept lens, not a story part)
    std::string name;                 // "Mara's web", "Act II foreshadows"

    std::vector<MindMapItem> items;   // the placed nodes (layout, not content)
    std::vector<ReflowRule>  rules;   // the active rule block (may be all-off = free-flow)
    std::string active_frame;         // a MindMapFrame name, or "" for no frame

    // Scope filter — what's eligible to appear (by node kind / edge kind). The
    // hairball cure: open FOCUSED, expand on demand, never "everything at once".
    std::vector<IidKind> show_kinds;  // empty = all kinds
    std::string focus_iid;            // the stood-on node for a radial/W-frame view ("" = none)

    // Viewport (presentation; restored on open).
    double zoom = 1.0;
    double pan_x = 0.0, pan_y = 0.0;
};

// ── The reflow engine (pure; g++/sandbox-testable; GTK-free) ─────────────────
// Same discipline as StoryGraph / Iid: the novel logic is a pure unit, the GTK
// canvas is a thin renderer. Inputs are plain data (the items, the active rules/
// frame, the typed edges from StoryGraph). Output is the PROJECTED positions to
// draw — the authored x/y on each item is left untouched (lossless toggle-off).
class MindMapLayout {
public:
    // A Placement is either a NODE (iid + glyph) or a field-value CHIP (field_chip
    // = true, `text` = the value, no iid) — the latter is how a field-bound slot
    // (When ← Date) renders its value in its zone.
    struct Placement {
        std::string iid;                 // the node shown ("" for a field chip)
        double      x = 0.0, y = 0.0;
        MapGlyph    glyph = MapGlyph::Card;
        std::string text;                // field-chip value ("" for node placements)
        bool        field_chip = false;
    };

    // Pure: (items + rules + edges) → where to draw each node THIS frame. Pinned
    // items resolve to their authored x/y; everything else flows per the rules. No
    // I/O, no mutation of the items, no verdict.
    static std::vector<Placement>
    reflow(const std::vector<MindMapItem>& items,
           const std::vector<ReflowRule>&  rules,
           const std::vector<StoryEdge>&   edges);

    // Apply a W-frame around a focal node. Edge-bound slots collect neighbours by
    // edge kind; field-bound slots read a value off the focal node (passed in as
    // `focus_fields`: (field_id, value) pairs). A slot that catches nothing is
    // reported in out_empty_slots as a surfaced fact (NOT a defect).
    static std::vector<Placement>
    frame(const MindMapFrame& f, const std::string& focus_iid,
          const std::vector<StoryEdge>& edges,
          const std::vector<std::pair<std::string, std::string>>& focus_fields,
          std::vector<std::string>& out_empty_slots /* labels of unfilled zones */);
};

// ── Viewport + hit-testing (pure; the GTK canvas is a thin shell over these) ──
// The renderer needs two things beyond the layout: a transform between WORLD space
// (the layout/authored coordinates reflow & frame emit) and SCREEN space (widget
// pixels under the current pan/zoom), and a hit test (which node is under the
// cursor) for click-to-open, drag-to-connect, and double-click-empty. All pure, so
// the GTK widget stays a dumb painter + input forwarder.
//
// Convention: screen = world * zoom + pan.  (pan is the screen-space offset of the
// world origin; zoom > 0 scales.)
struct MapViewport { double pan_x = 0.0, pan_y = 0.0, zoom = 1.0; };
struct ScreenPt    { double x = 0.0, y = 0.0; };
struct WorldPt     { double x = 0.0, y = 0.0; };

ScreenPt world_to_screen(const MapViewport& vp, double wx, double wy);
WorldPt  screen_to_world(const MapViewport& vp, double sx, double sy);

// The TOPMOST node placement whose centre is within `radius_px` SCREEN pixels of
// (screen_x, screen_y), or "" if none. The radius is a fixed pixel target (a node
// stays equally clickable at any zoom). Field chips (no iid) are not hit targets —
// they aren't navigable. "Topmost" = last in draw order (later placements paint
// over earlier ones).
std::string hit_test(const std::vector<MindMapLayout::Placement>& placements,
                     const MapViewport& vp, double screen_x, double screen_y,
                     double radius_px);

}  // namespace Folio
