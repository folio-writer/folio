#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// StoryGraph.hpp — the manuscript as ONE related body (s21 block-in, header-first)
//
// DESIGN BLOCK-IN — not yet wired. Defines the *shape* of the relationship layer
// so we can react before it threads through the lenses. Supersedes the earlier
// StructureBoard.hpp draft: Scott clarified the real model is not a weight-curve
// chart but a RELATED WEB — scenes, characters, places, signposts, and
// plant→payoff foreshadow links, all over the one iid body, read through four
// lenses (editor, timeline, mind map, index cards). See DESIGN_modules §4/§6a
// and the conversation that worked the "everything is related" graph out.
//
// THE REFRAME (Scott's words): Scrivener files characters / places / scenes /
// structure into separate BINS. They are not bins — they are NODES IN ONE GRAPH,
// and the writing lives in the EDGES between them. A scene happens at a place,
// involves characters, carries a signpost, sits at a told-order position, and
// PLANTS something that PAYS OFF in another scene. Four lenses are four
// projections of this one graph; they are not four features.
//
// IT IS ALREADY HALF-BUILT. s20 shipped the edge substrate under the name
// "links": the `folio-link` tag is a scene→iid edge in prose, and
// DocumentModel::m_backlinks is an edge index keyed by target iid
// (BacklinkEntry = one edge). This layer is a TYPED READING of that existing
// graph + marks on the nodes — NOT a new model. We reuse rebuild_backlink_index
// / update_backlinks_for_node; we add edge MEANING and node MARKS.
//
// THE TWO BODIES, ONE IID SET (the distinction the docs missed — they framed the
// body as a tree; Scott named it a graph):
//   • TREE  = the binder. Containment: Book ▸ Part ▸ Chapter ▸ Scene. (Exists:
//             BinderNode.children.) This is STRUCTURE-as-hierarchy.
//   • GRAPH = everything else. Relationship: scene↔character, scene↔place,
//             scene↔signpost, scene→scene (foreshadow). (Exists as links;
//             typed here.) This is STRUCTURE-as-web.
//   Both over the same iid parts. Tree for containment, graph for relationship.
//
// THE GOVERNING DISCIPLINE (DESIGN_modules §0/§5 — baked in, not just noted):
//   The graph SURFACES relationships the author drew; it never invents one and
//   never verdicts. It will say "this reveal has no plant linked to it" (a
//   FACT — a dangling typed edge) but never "you forgot to foreshadow" (a
//   verdict). The author plants, the author links; the tool makes the author's
//   own web VISIBLE so they can bounce it, see its reach, and judge. There is
//   deliberately no is_valid()/score()/"missing relationships" defect list.
// ─────────────────────────────────────────────────────────────────────────────

#include <string>
#include <vector>

#include "Iid.hpp"

namespace Folio {

// ── Node marks — facts that ride a scene and render in every lens ────────────
// The same two facts Scott draws three ways (sidebar bg / map glyph / timeline
// track): WHICH SIGNPOST (color) and HOW DONE. Keyed by the scene's stable iid
// so the mark survives reorder. Persisted IN THE PROJECT (DESIGN_s19 §3f — must
// travel with the bundle to a collaborator/publisher), not app prefs.
struct SceneMark {
    std::string scene_iid;        // the scn_… this mark rides
    std::string signpost_id;      // active-anatomy signpost ("" = untagged)
    // Doneness is a HAND-FLIPPED state the author sets ("I've drafted this"),
    // NOT auto-computed from word count. Rationale: "the tool decides you're
    // done" is a small verdict, and the whole design refuses verdicts; Scott
    // "watches it fill in" — i.e. HE flips it. The map glyph reads this as
    // center-filled (done) vs black-center (not); the timeline doneness track
    // advances on it. (If a computed predicate is ever wanted, add it as a
    // SEPARATE surfaced fact — never overwrite the author's own flag.)
    bool        done = false;

    // ── Parallel-arc facts (added for multi-arc books — see §"two topologies"
    //    below). Single-arc books leave these at defaults and nothing changes. ──
    //
    // Two INDEPENDENT axes, forced apart by two real books:
    //   • LORD OF THE RINGS — many THREADS (Frodo's line, Aragorn's line) in ONE
    //     timeframe, told in blocks, may or may not converge in story-space.
    //   • TAPPER (Scott's) — ONE thread (Tapper) across THREE TIMEFRAMES (NOW /
    //     early-life / near-past), braided scene-by-scene, where two of the
    //     timeframes RHYME beat-for-beat to show a lifetime of exploitation.
    // A general book (multi-POV with flashbacks) has both at once. So a scene
    // needs both facts, independently, plus where it sits in its own strand.
    std::string thread;        // which character-line ("" = the sole/default thread)
    std::string timeframe;     // which era/stratum ("" = the sole/default timeframe)
    // Position within THIS scene's own (thread × timeframe) strand's internal
    // chronological order. This is the TAPPER requirement: it lets two parallel
    // timeframes be stacked beat-N over beat-N so the RHYME is visible. Within a
    // strand the scenes ARE internally chronological; this is that index.
    // (-1 = unset; the braid/told view ignores it, the rhyme view needs it.)
    int         within_arc_index = -1;
};

// ── Timeframe — a stratum, optionally PAIRED with another for parallel reading ─
// A timeframe is one era/stratum a story is told in. The load-bearing bit Tapper
// forced: two timeframes can be a DESIGNED PARALLEL PAIR (early-life ∥ near-past),
// meaning the rhyme view stacks them aligned by within_arc_index so beat sits
// under beat. The pairing is a property of the STRATA (structural, baked into how
// the book is built — Tapper's moral architecture depends on the rhyme), NOT an
// ad-hoc "lay these two side by side." (Ad-hoc comparison may come later as a
// lesser convenience; the designed pair is the real thing.)
struct Timeframe {
    std::string id;            // "now", "early_life", "near_past"
    std::string display_name;
    int         color_idx = 0; // 1-based into tag_colors (strata get colors too)
    // If set, the id of the timeframe this one is DESIGNED to run parallel to.
    // early_life.parallel_to == "near_past" (and vice-versa) tells the rhyme view
    // to stack them by within_arc_index. "" = not part of a designed pair.
    std::string parallel_to;
};

// ── Edge types — the MEANING on a link (the only thing added to s20's graph) ─
// s20's link is untyped (BacklinkEntry: source→target, anchor, text). A typed
// edge is that same link plus what it MEANS, so a lens can draw a foreshadow
// thread differently from a plain reference. Scott's headline relationship —
// "I foreshadow and bounce to the reveal" — is the Foreshadow edge: a
// first-class scene→scene plant→payoff link.
enum class EdgeKind {
    Reference,    // plain folio-link (today's default) — a mention/cross-ref
    Foreshadow,   // scene→scene, PLANT → PAYOFF (Scott's bounce-to-the-reveal)
    SetIn,        // scene→place   (this scene happens at this place)
    Involves,     // scene→character (this character is in this scene)
    Signpost,     // scene→signpost (structure; usually expressed via SceneMark,
                  //   but representable as an edge for graph-view uniformity)
};

// A typed edge between two iid parts. `kind` is the meaning; the rest mirrors
// BacklinkEntry so it tracks/serializes alongside the existing index.
struct StoryEdge {
    std::string from_iid;         // source (e.g. the plant scene)
    std::string to_iid;           // target (e.g. the reveal scene)
    EdgeKind    kind = EdgeKind::Reference;
    std::string anchor;           // source paragraph anchor (may be empty)
    std::string label;            // optional author note ("the locket")
};

// ── The lenses are PROJECTIONS of (nodes + marks + edges) ────────────────────
// None of these is a separate model. Each is a way to render the one graph.
// All key off the scene's iid, so activating any element (map glyph, timeline
// tick, card, sidebar row) opens the SAME file in the editor — exactly what the
// Iid widget-naming rule was built for (widget_name("map-node", iid), etc.).
//
// THE GOVERNING SEPARATION (Scott, s21): STRUCTURE IS IN THE LOOKING, NOT THE
// PROSE. The editor is deliberately STRUCTURE-BLIND — it is just the author
// inside one node, writing words; it neither knows nor needs to know the node's
// thread/timeframe/signpost. A structure-aware editor would be a nagging editor
// (the §5 violation at the worst place — where the writing happens). All
// structure knowledge lives in the LENSES, at the rim, never the center.
//
//   • Editor      : structure-blind. One node, prose. Stays dumb and clean.
//   • Sidebar/card: ambient marks — signpost color as bg + hover hint; doneness.
//   • Mind map    : LAYOUT-DUMB, hand-arranged. Holds nodes (ring = signpost/
//       timeframe color, center = doneness) + edges (Foreshadow & cross-strata
//       rhyme threads styled distinctly); the AUTHOR places them. General
//       because it imposes no layout — an unusual book (Tapper) is just an
//       unusual arrangement the author makes, not a shape the tool dictates.
//   • Timeline    : LAYOUT-PLURAL. One graph, SEVERAL chosen axes — the author
//       picks which they look through. The timeline does not know one way
//       timelines work; it knows several, because structure is in the looking:
//         – TOLD-ORDER  : reader's sequence (= binder order). Any book. Shows
//             the braid rhythm; carries two tracks (signpost color + doneness).
//         – RHYME       : paired timeframes stacked by within_arc_index so beat
//             sits under beat (Tapper's early-life ∥ near-past). Shows the echo.
//         – WORLD-CLOCK : threads as concurrent lanes on a shared story-time
//             (LOTR's split fellowship); convergence VISIBLE where lanes meet,
//             never required where they don't.
//       All three are layouts over the SAME StoryGraph; none is baked into the
//       data. Generality here is what stops the tool ever telling an author
//       their structure is "wrong" by being unable to draw it.
struct SceneGlyph {              // what the map/timeline render per scene
    std::string scene_iid;
    int         told_index = 0;  // order number shown on the glyph / told-timeline
    std::string signpost_id;     // → ring color
    bool        done = false;    // → center filled vs black
    std::string thread;          // → lane (world-clock layout)
    std::string timeframe;       // → stratum color / rhyme grouping
    int         within_arc_index = -1; // → rhyme-layout alignment key
    double      x = 0.0, y = 0.0;// AUTHORED map position (persisted); the
                                 //   timeline layouts ignore it and place by axis
};

// ── The graph read — what the lenses consume (pure; no verdict) ──────────────
// What is ABSENT and stays absent: no validity flag, no completeness score, no
// "issues". A reveal with no incoming Foreshadow edge surfaces as a DANGLING
// edge fact the author may read and act on — never as a defect the tool asserts.
struct GraphReading {
    std::vector<SceneGlyph> scenes;        // told-order; marks resolved for render
    std::vector<StoryEdge>  edges;         // typed; Foreshadow threads included
    int  total_scenes   = 0;
    int  done_scenes     = 0;               // surfaced progress, not a quota
    int  untagged_scenes = 0;               // surfaced, not scolded
    // Dangling foreshadow targets: payoff scenes that are the `to_iid` of no
    // Foreshadow edge the author drew — i.e. reveals with no linked plant. A
    // surfaced FACT for the author to bounce to and judge. Never "missing".
    std::vector<std::string> unplanted_payoffs;   // scene iids (author reads & decides)
};

// ── The engine (pure; g++/sandbox-testable; GTK-free) ────────────────────────
// Same discipline as Iid / CompileFormatIO: novel logic in a pure unit, the GTK
// lenses are thin renderers. Inputs are plain data the binder + s20 link index
// already expose (told-ordered scene iids, their marks, the typed edges derived
// from the existing backlink index).
class StoryGraph {
public:
    struct SceneInput {
        std::string iid;          // scn_… ; vector order IS the told order
        bool        done = false; // the hand-flipped mark
        std::string signpost_id;  // "" if untagged
        double      map_x = 0.0, map_y = 0.0;   // authored map position
    };

    // Pure: (told-ordered scenes + typed edges) → GraphReading. No I/O, no
    // mutation, no verdict. The lenses render the result.
    static GraphReading read(const std::vector<SceneInput>& told_order,
                             const std::vector<StoryEdge>&  edges);

    // Derive typed edges from the existing s20 backlink index. Default kind is
    // Reference (today's links); promotion to Foreshadow/SetIn/Involves is an
    // author act (or inferred from the target's part-kind: a link to a chr_… is
    // Involves, to a plc_… is SetIn — decidable from the iid prefix, not guessed).
    // This is the bridge that makes the graph a READING of what s20 already
    // stores, not a parallel structure to keep in sync.
    static std::vector<StoryEdge>
    edges_from_backlinks(/* const DocumentModel& */);   // signature filled at wiring
};

}  // namespace Folio
