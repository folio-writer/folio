// ─────────────────────────────────────────────────────────────────────────────
// TimelineSpine.cpp — the told-order spine + structure-band projection (§9.1).
//
// ONE depth-first walk does both jobs. Scenes (leaves) are appended to the spine
// in encounter order, so a scene's position is its 1-based index. Each group,
// after its subtree is walked, knows the [min..max] told-order position of every
// scene beneath it — that range IS its band span, contiguous because the walk is
// depth-first (a group's scenes are visited as one unbroken run). Empty groups
// (no descendant scene → range stays "none") contribute no band.
//
// The adapter spine_input_from_manuscript() is intentionally NOT defined here —
// it needs DocumentModel.hpp (which pulls GTK), so it lives in the model TU,
// exactly like StoryGraph::edges_from_backlinks. This TU stays pure and sandbox-
// compilable on its own.
// ─────────────────────────────────────────────────────────────────────────────

#include "TimelineSpine.hpp"

namespace Folio {

namespace {

// Walk one node list at `depth`. Appends scenes to proj.spine (assigning their
// positions) and, for each group, emits a band spanning its descendant scenes.
// Returns the [min,max] told-order position of scenes found in THIS list's
// subtree, or {0,0} (has_any=false) when the subtree holds no scene.
struct Span { int lo = 0; int hi = 0; bool has_any = false; };

Span walk(const std::vector<SpineInputNode>& nodes, int depth,
          SpineProjection& proj) {
  Span here;
  for (const auto& n : nodes) {
    if (n.is_group) {
      const Span sub = walk(n.children, depth + 1, proj);
      if (sub.has_any) {
        StructureBand b;
        b.iid = n.iid;
        b.label = n.title;
        b.depth = depth;
        b.start_pos = sub.lo;
        b.end_pos = sub.hi;
        proj.bands.push_back(std::move(b));
        if (depth + 1 > proj.band_rows) proj.band_rows = depth + 1;
        // Fold the group's span into this level's running span.
        if (!here.has_any) { here.lo = sub.lo; here.hi = sub.hi; here.has_any = true; }
        else { if (sub.lo < here.lo) here.lo = sub.lo;
               if (sub.hi > here.hi) here.hi = sub.hi; }
      }
      // Empty group: no band, contributes nothing to the span.
    } else {
      // A scene leaf: take the next told-order position.
      const int pos = static_cast<int>(proj.spine.size()) + 1;
      proj.spine.push_back(SpineScene{n.iid, n.title, pos});
      if (!here.has_any) { here.lo = here.hi = pos; here.has_any = true; }
      else { if (pos < here.lo) here.lo = pos;
             if (pos > here.hi) here.hi = pos; }
    }
  }
  return here;
}

}  // namespace

SpineProjection project_spine(const std::vector<SpineInputNode>& roots) {
  SpineProjection proj;
  walk(roots, 0, proj);
  return proj;
}

}  // namespace Folio
