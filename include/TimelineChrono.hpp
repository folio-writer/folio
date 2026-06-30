#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// TimelineChrono.hpp — re-lay the told-order spine by story-time (the world-clock
// axis, DESIGN_timeline.md §9.14, build-order step 3). Pure, GTK-free,
// header-only. The chronological REARRANGEMENT Scott asked for ("it helps set the
// mind to the period better"): flashbacks spring back to where they happen,
// parallel storylines line up.
//
// ONE input fact per scene: the OPTIONAL absolute story-time coordinate (the
// Option-B BinderNode field). The ordinal is DERIVED here — sort by coordinate,
// rank falls out (§9.14.1). This step is ORDINAL/even-spacing only; proportional
// spacing + the broken-axis ruler are step 4.
//
// UNDATED scenes (the §9.14.4 fork, RE-DECIDED s93b): a scene stays ON the spine,
// but DATED scenes lay out first in time order and UNDATED ("not yet placed") scenes
// trail in told order. The point is that setting a date is VISIBLE: a dated scene
// moves into the timed sequence the moment you place it, instead of sitting still
// (the carry-forward rule made a single date invisible, since an ordinal position is
// a rank, and rank only changes once two dated scenes conflict). With nothing dated
// the order is still identical to told; as you date scenes they join the timed run
// up front; the relationship relief stays attached because every scene is on spine.
//
// TIES (two scenes at the SAME coordinate — simultaneous, "meanwhile" — and the
// whole undated trail) keep their told order via a STABLE sort, so a deliberate
// ordering of concurrent or unplaced scenes survives the flip.
//
// Header-only inline (the TimelineFocus / TimelineClock precedent): pure ordering,
// no DocumentModel, no CMakeLists entry. The GTK side builds the input vector from
// the spine + BinderNode coordinates and feeds `chrono` to the same relief engine
// the told-order spine uses (the §9.14.5 substrate reuse).
// ─────────────────────────────────────────────────────────────────────────────

#include <algorithm>
#include <climits>
#include <cstddef>
#include <string>
#include <vector>

namespace Folio {

// One told-order on-spine scene as the chronological sort sees it. `time` is valid
// only when `dated` (the BinderNode has_story_time / story_time pair).
struct ChronoScene {
  std::string iid;
  bool        dated = false;
  long long   time  = 0;
};

struct ChronoOrder {
  std::vector<std::string> chrono;    // ALL scenes: dated ascending by coordinate, then undated in told order
  std::vector<std::string> undated;   // empty since s93 (undated scenes hold their spine slot); kept for the host API
};

// Re-lay `told` (index = told position) by story-time. Every scene stays on the
// spine: dated scenes ascend by coordinate; undated scenes trail in told order.
// Stable, so equal coordinates — and the undated trail — preserve told order.
inline ChronoOrder chronological_order(const std::vector<ChronoScene>& told) {
  ChronoOrder out;
  const std::size_t N = told.size();
  std::vector<long long> eff(N);
  for (std::size_t i = 0; i < N; ++i)
    eff[i] = told[i].dated ? told[i].time : LLONG_MAX;   // undated sort after every dated scene
  std::vector<std::size_t> idx(N);
  for (std::size_t i = 0; i < N; ++i) idx[i] = i;
  std::stable_sort(idx.begin(), idx.end(),
                   [&eff](std::size_t a, std::size_t b) { return eff[a] < eff[b]; });
  out.chrono.reserve(N);
  for (std::size_t i : idx) out.chrono.push_back(told[i].iid);
  return out;   // out.undated stays empty — undated scenes trail on the spine, not in a tray
}

}  // namespace Folio
