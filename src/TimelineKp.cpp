// ─────────────────────────────────────────────────────────────────────────────
// TimelineKp.cpp — assemble_kp_lanes (s81, DESIGN_timeline.md §9.4 / step 6).
// Pure. See TimelineKp.hpp for the contract and the substrate grounding.
//
// One pass of the told-order spine. Each on-spine scene that carries a non-empty
// kp_id joins (or starts) its lane; off-spine tags never enter (we only iterate
// the spine), exactly as assemble_tracks drops off-spine claims. A lane's
// label/color_idx are FROZEN at its first claimed scene in told order — the
// first appearance both orders the lane (arc order) and names it, so a later
// scene with a blank or drifted label cannot rename the lane out from under it.
// ─────────────────────────────────────────────────────────────────────────────

#include "TimelineKp.hpp"

#include <algorithm>

namespace Folio {

std::vector<KpLane>
assemble_kp_lanes(const std::vector<std::string>& spine,
                  const std::unordered_map<std::string, SceneKpInfo>& scene_kp) {
  std::vector<KpLane> lanes;
  // kp_id → index into `lanes`, so the second+ scene of a beat finds its lane in
  // O(1) without a linear scan. Insertion-orderless; final order is set below.
  std::unordered_map<std::string, std::size_t> by_id;

  // Walk the spine in told order so first_pos / label / color_idx are taken from
  // each lane's earliest claimed scene, and `claimed` is filled spine-first.
  for (std::size_t i = 0; i < spine.size(); ++i) {
    const std::string& scene_iid = spine[i];

    const auto it = scene_kp.find(scene_iid);
    if (it == scene_kp.end()) continue;            // untagged scene → no lane.
    const SceneKpInfo& info = it->second;
    if (info.kp_id.empty()) continue;              // explicitly untagged → skip.
    if (!info.is_key_point) continue;              // a plain colour-tag, not a
                                                   // beat → not on the KP lane.

    const int pos = static_cast<int>(i) + 1;       // 1-based told-order position.

    auto found = by_id.find(info.kp_id);
    if (found == by_id.end()) {
      // First told-order appearance of this KP → birth the lane, freezing its
      // identity (label + colour) and its sort key (first_pos) from THIS scene.
      KpLane lane;
      lane.kp_id     = info.kp_id;
      lane.label     = info.kp_label;
      lane.color_idx = info.color_idx;
      lane.first_pos = pos;
      lane.claimed.insert(scene_iid);
      by_id.emplace(info.kp_id, lanes.size());
      lanes.push_back(std::move(lane));
    } else {
      // A later beat of an existing KP → claim only; identity stays frozen.
      lanes[found->second].claimed.insert(scene_iid);
    }
  }

  // Arc order: first appearance (told order), then kp_id for a total, stable,
  // determinable order. KPs are inherently arc-ordered, so first_pos IS arc
  // order; the kp_id tiebreak only fires on the (degenerate) shared-first-scene
  // case and just keeps the output reproducible.
  std::sort(lanes.begin(), lanes.end(),
            [](const KpLane& a, const KpLane& b) {
              if (a.first_pos != b.first_pos) return a.first_pos < b.first_pos;
              return a.kp_id < b.kp_id;
            });

  return lanes;
}

}  // namespace Folio
