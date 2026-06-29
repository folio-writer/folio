#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// TimelineSpine.hpp — the told-order spine + structure bands (DESIGN_timeline.md
// §9.1 / §9.9 step 2). The SECOND slice of the Relationship Timeline, and the
// pure half of "the timeline goes visual": the geometry the painter draws is a
// projection of the binder tree, computed here, GTK-free.
//
// TWO THINGS, ONE WALK (a single depth-first pass of the Manuscript tree):
//
//   • THE SPINE — the told-order sequence of SCENES (the leaves). A scene's
//     position is index+1; the spine is the reader's sequence (= binder order).
//     This is the same told-order the relief function (§9.2) consumes: the
//     `spine_iids()` helper hands the iid vector straight to compute_relief.
//
//   • THE STRUCTURE BANDS — the group-nesting levels ABOVE the spine
//     (Book ▸ Part ▸ Chapter), read from the tree. Each band is ONE group's
//     contiguous span over the spine: [first .. last] told-order position of the
//     scenes anywhere under it. CONTIGUOUS BY CONSTRUCTION — told-order is
//     depth-first, so every group's descendant scenes form one unbroken run.
//     This is §9.1's "above the spine: containment, always contiguous".
//
// DEPTH = the band's row. depth 0 = a top-level Manuscript group (a Part, or a
// Chapter when the book has no Parts). Each deeper nesting level is one row
// lower (closer to the spine). `band_rows` = max band depth + 1 = how many band
// rows the painter stacks. Books of different shapes (Part▸Chapter▸Scene vs.
// flat Chapter▸Scene vs. loose scenes at root) just yield different band_rows —
// the bands MIRROR the actual nesting; nothing is synthesised. In particular
// there is NO synthetic "Book" band: the Manuscript root is not a node, so a
// Book-level band exists only if the author made a real top-level group.
//
// EMPTY GROUPS contribute no band (a group with no descendant scene has no span
// to anchor). RAGGED DEPTH is truthful, not floated: a loose top-level scene
// sitting beside a Part group simply has no band above it at row 0 — bands may
// have holes within a row. "Always contiguous (single parent)" is a per-BAND
// guarantee, never a per-row one.
//
// KEYED ON IID, NOT POSITION (the §9.7 discipline shared with compute_relief):
// positions are derived from the tree at projection time, so a binder reorder
// renumbers the spine and respans the bands for free — the timeline stores no
// geometry. This unit owns no model; it projects a DTO the model layer fills,
// exactly as StoryGraph consumes a told_order vector someone else builds.
//
// Pure: <string>, <vector>. GTK-free, JSON-free, DocumentModel-free — sandbox-
// proven (TEST_timeline_spine.cpp) before any geometry is painted. The model→
// DTO adapter (spine_input_from_manuscript) is DECLARED here but DEFINED in the
// model TU, mirroring StoryGraph::edges_from_backlinks: the declaration documents
// the seam without dragging DocumentModel.hpp (which pulls GTK) into this unit.
// ─────────────────────────────────────────────────────────────────────────────

#include <string>
#include <vector>

namespace Folio {

// ── The pure input DTO — a lightweight mirror of the Manuscript tree ─────────
// Only what the projection needs: identity, label, group-vs-leaf, children.
// The model layer fills these from BinderNode (iid / title / kind / children);
// the sandbox fills them by hand. A leaf is a scene; a group is a container.
struct SpineInputNode {
  std::string iid;                       // scn_… or grp_…
  std::string title;                     // display label
  bool is_group = false;                 // group (container) vs scene (leaf)
  std::vector<SpineInputNode> children;  // groups only; leaves leave empty
};

// ── One scene on the told-order spine ────────────────────────────────────────
struct SpineScene {
  std::string iid;     // scn_…
  std::string title;   // display
  int position = 0;    // 1-based told-order index (== vector index + 1)
};

// ── One structure band: a group's contiguous span over the spine ─────────────
struct StructureBand {
  std::string iid;       // grp_… (the group node's iid)
  std::string label;     // group title
  int depth = 0;         // 0 = top-level group; deeper nesting = larger depth.
  int start_pos = 0;     // 1-based; first descendant scene's told-order position.
  int end_pos = 0;       // 1-based, inclusive; last descendant scene's position.
  int span() const { return end_pos - start_pos + 1; }  // # of scene columns.
};

// ── The projection: the spine + the bands + how many band rows to stack ──────
struct SpineProjection {
  std::vector<SpineScene> spine;      // told order; position == index + 1.
  std::vector<StructureBand> bands;   // every non-empty group, any depth.
  int band_rows = 0;                  // max band depth + 1 (0 when no bands).

  // The told-order iid vector, ready for compute_relief (§9.2). The spine and
  // the relief are computed over the SAME ordering — this is the bridge.
  std::vector<std::string> spine_iids() const {
    std::vector<std::string> v;
    v.reserve(spine.size());
    for (const auto& s : spine) v.push_back(s.iid);
    return v;
  }
};

// THE pure projection. Deterministic; depth-first walk; tree order authoritative.
// `roots` is the Manuscript section's top-level node list (model.root(...)).
SpineProjection project_spine(const std::vector<SpineInputNode>& roots);

// ── The zoom unit (s91) ──────────────────────────────────────────────────────
// Timeline zoom is a UNIFORM scale of the whole surface (an "actual zoom", like
// the Map): the painter draws everything at its base size and applies one
// cr->scale(z, z), so cards, lanes, gaps and labels all scale together and stay
// proportional. z is a pure factor; these are its bounds + the step. (Earlier
// cuts scaled only the column width, which distorted the layout — that is gone.)
//
// Range is generous both ways — 0.25 (a wide overview) to 5.0 (a single scene
// filling the view). 1.0 is the s80 mock size.
inline constexpr double kTimelineZoomDefault = 1.0;
inline constexpr double kTimelineZoomMin     = 0.25;
inline constexpr double kTimelineZoomMax     = 5.0;

// Apply a zoom factor to the current zoom: multiply and clamp into
// [kTimelineZoomMin, kTimelineZoomMax]. Returns the NEW zoom — which equals `current` when a
// factor would push past a rail it already sits on (clamp returns the exact
// bound) or when factor == 1.0, so the surface can compare == to skip the
// relayout/redraw. Pure: no GTK, no model — sandbox-tested (TEST_timeline_zoom.cpp).
double next_timeline_zoom(double current, double factor);

// ── Model → DTO adapter (DECLARED here, DEFINED in the model TU) ──────────────
// Mirrors StoryGraph::edges_from_backlinks: this signature documents how the
// painter feeds the projection from the live model, but the definition lives in
// a giomm-tainted TU so this header stays pure. The body is a trivial recursive
// copy of (iid, title, is_group, children) off the Manuscript binder tree.
class DocumentModel;  // fwd — no include; keeps this unit GTK-free.
std::vector<SpineInputNode> spine_input_from_manuscript(const DocumentModel& model);

}  // namespace Folio
