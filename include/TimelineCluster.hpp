#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// TimelineCluster.hpp — temporal CLUSTERS from the chronological step chain
// (DESIGN_timeline.md §9.14.9, the cluster primitive). Pure, GTK-free, header-only.
//
// The model Scott landed on in the s95 design talk: a timeline is the chronological
// ORDER of scenes; the start does not matter; what matters is each scene's RELATIVE
// step to the previous scene and the GAP nomenclature between them. A CLUSTER is the
// emergent primitive — a contiguous run of scenes close enough in story-time to read
// as one continuous stretch ("a story unto itself"), bounded by a gap on each side.
//
//   • Input: the chain in chronological order, each scene carrying the SIGNED step
//     (seconds) from the previous scene. step on index 0 is ignored (no predecessor).
//   • A step BREAKS the cluster when it is COARSER than the cluster's own prevailing
//     scale — "10 scenes on an hour divide break at a day or a week"; "a week/month
//     cluster breaks at a gap of years". The break is RELATIVE to the cluster's
//     rhythm, not an absolute threshold (the §9.14.4 adaptive-units idea).
//   • A BACKWARD step (negative) breaks the same way by magnitude — it just makes the
//     next cluster a FLASHBACK (the ⊓ bracket the renderer draws above it, labelled
//     "Flashback" / "Far Past"). 0-second steps ("meanwhile") never break and do not
//     define the cluster scale.
//   • Output: the clusters (index spans + iids + the cluster's prevailing unit, which
//     the renderer reads for per-cluster ticks and the bracket label) and the signed
//     gap between each consecutive pair.
//
// Storage-agnostic on purpose: the chain's steps are whatever the host hands in —
// derived from absolute coordinates (neighbour subtraction) OR stored as relative
// steps directly. That fork (and the reorder/label forks) is settled in the host,
// not here. Labels are NOT derived here — a cluster's name is authored/stored by the
// host; this layer only finds the spans. Header-only inline, the TimelineClock /
// TimelineChrono precedent: pure derivation, no DocumentModel, no CMakeLists entry.
// ─────────────────────────────────────────────────────────────────────────────

#include "TimelineClock.hpp"   // DurationUnit, UnitCount, coarsest_unit, unit_seconds

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace Folio {

// One scene as the cluster pass sees it: its iid + the SIGNED seconds from the
// previous scene in chronological order. `step` on the first scene is unused.
struct ChainScene {
  std::string iid;
  long long   step = 0;
};

// A contiguous temporal cluster. [first,last] are inclusive indices into the chain;
// `iids` is the scenes in order; `scale` is the cluster's prevailing (coarsest intra)
// unit — what the renderer ticks the cluster in and labels its bracket against.
struct TimelineCluster {
  std::size_t  first = 0;
  std::size_t  last  = 0;
  std::vector<std::string> iids;
  DurationUnit scale = DurationUnit::Second;
};

// The signed gap that separates one cluster from the next (negative = the next
// cluster happens EARLIER — a flashback seam). `unit` is its coarsest unit, for the
// broken-axis label ("2 years", "↩ 2 years earlier").
struct ClusterGap {
  long long    step = 0;
  DurationUnit unit = DurationUnit::Second;
};

struct ClusterLayout {
  std::vector<TimelineCluster> clusters;
  std::vector<ClusterGap>      gaps;   // size == max(clusters.size() - 1, 0)
};

// The enum order Second..Year IS the rank: finest = Second(0) … coarsest = Year(6).
inline int unit_rank(DurationUnit u) { return static_cast<int>(u); }

// Does step `step_rank` start a NEW cluster, given the current cluster's established
// scale rank (or -1 when the cluster still has only its first scene)? Two ways to
// break, sign ignored throughout (a backward step of equal magnitude breaks the same
// — it just flips the next cluster to a flashback):
//   • ABSOLUTE FLOOR — a step at/above `floor_rank` is ALWAYS a gap, even as the
//     first step out of a lone scene. Default floor = Year: weeks/months may pace a
//     cluster (Scott: "clustered in a week or month"), years-and-coarser never do
//     ("a gap of years breaks"). This is what splits a lone leader (A —+5yr→ B) off
//     instead of welding it to the next scene.
//   • ADAPTIVE — below the floor, the first intra step seeds the scale and never
//     breaks; after that a step breaks when it is coarser than the scale by
//     >= `unit_jump` ranks (the §9.14.4 per-cluster adaptive idea; a day breaks an
//     hour run, a year breaks a week run). 0-second "meanwhile" steps never break.
inline bool cluster_breaks(int step_rank, int scale_rank, int unit_jump, int floor_rank) {
  if (step_rank >= floor_rank) return true;          // absolute gap floor (Year): always a gap
  if (scale_rank < 0) return false;                  // below floor + first intra step → seed, join
  return (step_rank - scale_rank) >= unit_jump;      // coarser than the cluster's own pace
}

inline ClusterLayout cluster_chain(const std::vector<ChainScene>& chain, int unit_jump = 1,
                                   DurationUnit floor_unit = DurationUnit::Year) {
  ClusterLayout out;
  const std::size_t N = chain.size();
  if (N == 0) return out;
  const int floor_rank = unit_rank(floor_unit);

  TimelineCluster cur;
  cur.first = 0;
  cur.iids.push_back(chain[0].iid);
  int scale_rank = -1;   // unset until the first non-zero, sub-floor intra step seeds it

  auto seal = [&](std::size_t last_idx) {
    cur.last  = last_idx;
    cur.scale = (scale_rank < 0) ? DurationUnit::Second
                                 : static_cast<DurationUnit>(scale_rank);
    out.clusters.push_back(cur);
  };

  for (std::size_t i = 1; i < N; ++i) {
    const long long s  = chain[i].step;
    const UnitCount uc = coarsest_unit(s);
    const int       r  = unit_rank(uc.unit);
    if (cluster_breaks(r, scale_rank, unit_jump, floor_rank)) {
      seal(i - 1);
      out.gaps.push_back(ClusterGap{s, uc.unit});
      cur = TimelineCluster{};
      cur.first = i;
      cur.iids.push_back(chain[i].iid);
      scale_rank = -1;
    } else {
      cur.iids.push_back(chain[i].iid);
      if (uc.count != 0) scale_rank = std::max(scale_rank, r);   // 0-step never seeds the scale
    }
  }
  seal(N - 1);
  return out;
}

}  // namespace Folio
