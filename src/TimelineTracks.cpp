// ─────────────────────────────────────────────────────────────────────────────
// TimelineTracks.cpp — subject-track assembly (s80 step 3). See header.
//
// One pass over the edges builds subject → {claimed scenes}, keeping only scenes
// on the spine. Then each subject becomes a track, sorted by category / first
// appearance. The category split is the only "decision", and it is just the iid
// prefix — pure, via iid_kind_of.
// ─────────────────────────────────────────────────────────────────────────────

#include "TimelineTracks.hpp"

#include <algorithm>
#include <climits>

#include "Iid.hpp"

namespace Folio {

namespace {

bool is_scene(const std::string& iid) {
  return iid_kind_of(iid) == IidKind::Scene;
}

// True if the iid names a track SUBJECT (character / place / reference / image).
bool is_subject(const std::string& iid) {
  switch (iid_kind_of(iid)) {
    case IidKind::Character:
    case IidKind::Place:
    case IidKind::Reference:
    case IidKind::Asset:
      return true;
    default:
      return false;
  }
}

TrackCategory category_of(const std::string& iid) {
  switch (iid_kind_of(iid)) {
    case IidKind::Place:     return TrackCategory::Place;
    case IidKind::Reference: return TrackCategory::Reference;
    case IidKind::Asset:     return TrackCategory::Image;
    default:                 return TrackCategory::Character;  // chr_ (and guard)
  }
}

}  // namespace

std::vector<TimelineTrack>
assemble_tracks(const std::vector<std::string>& spine,
                const std::vector<StoryEdge>& edges,
                const std::unordered_map<std::string, std::string>& labels) {
  // told-order position lookup (1-based) — both the spine-membership filter and
  // the first-appearance sort key read it.
  std::unordered_map<std::string, int> pos_of;
  pos_of.reserve(spine.size() * 2);
  for (int i = 0; i < static_cast<int>(spine.size()); ++i)
    pos_of.emplace(spine[i], i + 1);

  // subject iid → claimed scene iids (only scenes on the spine).
  std::unordered_map<std::string, std::unordered_set<std::string>> claims;
  auto note = [&](const std::string& subject, const std::string& scene) {
    if (pos_of.find(scene) == pos_of.end()) return;  // off-spine scene → drop
    claims[subject].insert(scene);
  };

  for (const auto& e : edges) {
    const bool f_scene = is_scene(e.from_iid);
    const bool t_scene = is_scene(e.to_iid);
    if (f_scene && is_subject(e.to_iid))        note(e.to_iid, e.from_iid);
    else if (t_scene && is_subject(e.from_iid)) note(e.from_iid, e.to_iid);
    // scene↔scene and subject↔subject contribute no track claim.
  }

  std::vector<TimelineTrack> tracks;
  tracks.reserve(claims.size());
  for (auto& [subject, scenes] : claims) {
    if (scenes.empty()) continue;
    TimelineTrack tr;
    tr.iid = subject;
    tr.category = category_of(subject);
    auto it = labels.find(subject);
    tr.label = (it != labels.end() && !it->second.empty()) ? it->second : subject;
    int fp = INT_MAX;
    for (const auto& s : scenes) fp = std::min(fp, pos_of[s]);
    tr.first_pos = fp;
    tr.claimed = std::move(scenes);
    tracks.push_back(std::move(tr));
  }

  // category (§9.6 order) → first appearance → iid (determinism).
  std::sort(tracks.begin(), tracks.end(),
            [](const TimelineTrack& a, const TimelineTrack& b) {
              if (a.category != b.category)
                return static_cast<int>(a.category) < static_cast<int>(b.category);
              if (a.first_pos != b.first_pos) return a.first_pos < b.first_pos;
              return a.iid < b.iid;
            });
  return tracks;
}

}  // namespace Folio
