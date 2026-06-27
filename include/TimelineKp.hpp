#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// TimelineKp.hpp — the Key-Point lane assembler (s81, DESIGN_timeline.md §9.4 /
// §9.9 step 6). Pure, GTK-free, JSON-free, sandbox-testable.
//
// THE GROUNDING (the s80 substrate finding — read it before this header). There
// is NO literal "tagline" system in Folio. §9.4's "a KP is a tagline" maps onto
// the scene node's own stamped fields — `kp_id` / `kp_label` / `color_idx`
// (DocumentModel.hpp), set by the Module/Pattern materializer (ModulePlanner
// samples a KP's positional spectrum colour into `color_idx`; Module::KeyPoint
// carries id/label/order/color_idx). So the KP lane is simply **the relief of
// `kp_id`**: the §9.4 "tagline→scenes index" is "on-spine scenes grouped by
// `kp_id`, in told order" — a pure read off the scene nodes. NO new substrate,
// NO edges (unlike the subject sweep, which owns scene→subject links): a KP beat
// is carried by the scene itself.
//
// SHAPE — the exact analog of assemble_tracks (TimelineTracks.hpp), one row per
// KP instead of one per subject:
//
//   input:  spine        — told-order scene iids (project_spine().spine_iids()).
//                          Authoritative for ordering AND membership: only
//                          scenes ON the spine are claimed; a tag on an off-
//                          spine / deleted scene is silently dropped, exactly as
//                          assemble_tracks drops off-spine claims, so the lane
//                          always lines up under the spine.
//           scene_kp     — per-scene KP fact, keyed by scene iid. The model
//                          fills it inline in the GTK TU (a walk of all_node_
//                          ptrs, like the `labels` map in TimelineSurface::
//                          rebuild); the sandbox fills it by hand. A scene
//                          absent from the map, present with an empty kp_id, OR
//                          present but NOT flagged is_key_point is untagged and
//                          joins no lane — a colour-tag alone is a plain colour,
//                          not a beat (s81: the Inspector checkbox makes it one).
//   output: lanes        — one KpLane per distinct kp_id that claims ≥1 on-spine
//                          scene, ordered by FIRST APPEARANCE (earliest claimed
//                          told-order position), then kp_id for determinism.
//                          KPs are inherently arc-ordered, so first-appearance
//                          IS arc order; no category grouping (unlike tracks —
//                          a KP has no §9.6 category, it has a positional colour).
//
// FORK-FREE BY CONSTRUCTION. This layer carries `color_idx` THROUGH, unread,
// exactly as compute_relief carries `colour` and assemble_tracks carries
// `category` — the renderer maps it to a hue. That is what keeps the open colour
// fork (HANDOFF #1: per-KP spectrum colour vs. one yellow category hue; docs
// §9.4 vs §9.6 disagree) OUT of the pure layer and IN the painter where Scott's
// ruling lands. Likewise the singleton→DIAMOND split (vs. the subject Dot) is a
// RENDERER concern: compute_relief reports only Kind::Dot for a singleton; the
// KP painter draws that singleton as a diamond. And the attach gesture (stamp
// `kp_id`+`color_idx` onto swept scenes), its direction (HANDOFF #2), and the
// glance-list (HANDOFF #3) are all GTK concerns this read does not touch.
//
// KEYED ON IID, NOT POSITION (the §9.7 discipline shared with compute_relief /
// project_spine / assemble_tracks): positions are derived from the spine at call
// time, so a binder reorder renumbers and recomputes the lane for free. The
// timeline stores no geometry and owns no KP edges — a KP beat is a fact ON the
// scene, read here, never duplicated. Per the constitution: edges/tags are read,
// never owned (§1).
//
// Pure: <string>, <unordered_map>, <unordered_set>, <vector>. No GTK, no GLib,
// no JSON, no DocumentModel — sandbox-proven (TEST_timeline_kp.cpp) before any
// geometry is wired, like every other timeline layer.
// ─────────────────────────────────────────────────────────────────────────────

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Folio {

// ── The per-scene KP fact (a mirror of the scene node's stamped fields) ───────
// The model fills these from BinderNode (kp_id / kp_label / color_idx); the
// sandbox fills them by hand. Untagged scenes are simply absent from the map (or
// carry an empty kp_id). Self-contained — no DocumentModel dependency.
struct SceneKpInfo {
  std::string kp_id;        // the KP this scene serves; "" = untagged (no lane).
  std::string kp_label;     // human-readable KP label (display); may be empty.
  int         color_idx = 0;  // KP positional spectrum index (0 = unstamped).
  bool        is_key_point = false;  // s81 — this tagged scene is a story beat
                                     // (the Inspector checkbox bound beside the
                                     // colour/tag). Only flagged scenes form a
                                     // lane; an unflagged colour-tag is a plain
                                     // colour, not a beat. The filter is per
                                     // scene regardless of whether the bool is
                                     // stored on the scene or mirrored from the
                                     // swatch — this layer only reads it.
};

// ── One Key-Point lane: a named, positionally-coloured, ordered scene-set ─────
// Its claimed set is handed to compute_relief at render to produce the lane's
// bars (a beat spanning scenes) and singletons (the renderer draws these as
// DIAMONDS, vs. the subject Dot). Carries color_idx unread — the renderer
// resolves it to a hue (or the yellow fallback) once Scott settles the fork.
struct KpLane {
  std::string kp_id;        // the stable machine key this lane groups on.
  std::string label;        // display label (kp_label of the first claimed scene
                            //  in told order; "" if that scene had none).
  int         color_idx = 0;  // KP positional spectrum index (first claimed scene;
                              //  0 = unstamped → renderer's fallback hue).
  std::unordered_set<std::string> claimed;  // claimed scene iids, all ON the spine.
  int         first_pos = 0;  // earliest claimed told-order position (sort key).
};

// Assemble the KP lanes from the told-order spine + the per-scene KP map (§9.4).
// Deterministic; first-appearance (arc) order, then kp_id. The label/color_idx
// of each lane are taken from its FIRST claimed scene in told order, so a lane
// is self-describing even if later scenes carry a stale/blank label. See header
// for the claim rule (on-spine only) and the fork-deferral rationale.
std::vector<KpLane>
assemble_kp_lanes(const std::vector<std::string>& spine,
                  const std::unordered_map<std::string, SceneKpInfo>& scene_kp);

}  // namespace Folio
