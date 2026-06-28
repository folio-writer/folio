// ─────────────────────────────────────────────────────────────────────────────
// TimelineThreads.cpp — assemble_thread_lanes (s83, DESIGN_timeline.md §9.12 /
// step 7). Pure. See TimelineThreads.hpp for the contract and the arc-model
// settlement (entity = revealed, thread = assigned; no hand-bounding).
//
// One pass of the told-order spine — the exact shape of assemble_kp_lanes, with
// ONE deliberate difference: there is no is_key_point-style gate. A KP beat is an
// opt-in flag ON a coloured scene; a thread is the assignment itself, so any
// on-spine scene with a non-empty thread_key joins its lane. A lane's
// label/color_idx are FROZEN at its first claimed scene in told order — first
// appearance both orders the lane (braid order) and names it, so a later scene
// with a blank or drifted label cannot rename the lane out from under it.
// ─────────────────────────────────────────────────────────────────────────────

#include "TimelineThreads.hpp"

#include <algorithm>

namespace Folio {

std::vector<ThreadLane>
assemble_thread_lanes(const std::vector<std::string>& spine,
                      const std::unordered_map<std::string, SceneThreadInfo>& scene_thread) {
  std::vector<ThreadLane> lanes;
  // thread_key → index into `lanes`, so the second+ scene of a thread finds its
  // lane in O(1) without a linear scan. Final order is set below.
  std::unordered_map<std::string, std::size_t> by_key;

  // Walk the spine in told order so first_pos / label / color_idx are taken from
  // each lane's earliest claimed scene, and `claimed` is filled spine-first.
  for (std::size_t i = 0; i < spine.size(); ++i) {
    const std::string& scene_iid = spine[i];

    const auto it = scene_thread.find(scene_iid);
    if (it == scene_thread.end()) continue;        // unassigned scene → no lane.
    const SceneThreadInfo& info = it->second;
    if (info.thread_key.empty()) continue;         // explicitly unassigned → skip.
    // NOTE: no is_key_point gate here — the assignment IS the opt-in (header).

    const int pos = static_cast<int>(i) + 1;       // 1-based told-order position.

    auto found = by_key.find(info.thread_key);
    if (found == by_key.end()) {
      // First told-order appearance of this thread → birth the lane, freezing
      // its identity (label + colour) and its sort key (first_pos) from THIS
      // scene.
      ThreadLane lane;
      lane.thread_key = info.thread_key;
      lane.label      = info.thread_label;
      lane.color_idx  = info.color_idx;
      lane.first_pos  = pos;
      lane.claimed.insert(scene_iid);
      by_key.emplace(info.thread_key, lanes.size());
      lanes.push_back(std::move(lane));
    } else {
      // A later scene of an existing thread → claim only; identity stays frozen.
      lanes[found->second].claimed.insert(scene_iid);
    }
  }

  // Braid order: first appearance (told order), then thread_key for a total,
  // stable, determinable order. The thread_key tiebreak only fires on the
  // (degenerate) shared-first-scene case — a scene can carry only one thread, so
  // two lanes cannot truly share a first scene; the tiebreak just keeps the
  // output reproducible.
  std::sort(lanes.begin(), lanes.end(),
            [](const ThreadLane& a, const ThreadLane& b) {
              if (a.first_pos != b.first_pos) return a.first_pos < b.first_pos;
              return a.thread_key < b.thread_key;
            });

  return lanes;
}

}  // namespace Folio
