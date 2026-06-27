// ─────────────────────────────────────────────────────────────────────────────
// TimelineRelief.cpp — implementation of the pure relief function (§9.2).
//
// Single told-order walk of the spine. Occupancy is tested by iid membership in
// `claimed`, so iteration is ALWAYS in told order, never in claimed's (a set's)
// order. Maximal contiguous occupied runs become segments; the unclaimed spans
// strictly between consecutive segments become interior gaps.
// ─────────────────────────────────────────────────────────────────────────────

#include "TimelineRelief.hpp"

#include <algorithm>
#include <unordered_map>

namespace Folio {

Relief compute_relief(const std::vector<std::string>& spine,
                      const std::unordered_set<std::string>& claimed,
                      std::string label, std::string colour) {
  Relief r;
  r.label = std::move(label);
  r.colour = std::move(colour);

  const int n = static_cast<int>(spine.size());

  // iid → 1-based position, for unplaced detection. First wins on the (illegal)
  // chance of a duplicate spine iid; spine iids are unique by construction.
  std::unordered_map<std::string, int> pos_of;
  pos_of.reserve(spine.size() * 2);
  for (int i = 0; i < n; ++i)
    pos_of.emplace(spine[i], i + 1);

  // Walk told-order positions 1..n, grouping maximal contiguous claimed runs.
  // run_start == 0 means "not currently inside a run".
  int run_start = 0;
  auto flush = [&](int end_pos) {
    ReliefSegment seg;
    seg.start_pos = run_start;
    seg.end_pos = end_pos;
    seg.kind = (end_pos > run_start) ? ReliefSegment::Kind::Bar
                                     : ReliefSegment::Kind::Dot;
    seg.iids.reserve(static_cast<std::size_t>(end_pos - run_start + 1));
    for (int p = run_start; p <= end_pos; ++p)
      seg.iids.push_back(spine[static_cast<std::size_t>(p - 1)]);
    r.segments.push_back(std::move(seg));
  };

  for (int p = 1; p <= n; ++p) {
    const bool occupied = claimed.count(spine[static_cast<std::size_t>(p - 1)]) != 0;
    if (occupied && run_start == 0) {
      run_start = p;
    } else if (!occupied && run_start != 0) {
      flush(p - 1);
      run_start = 0;
    }
  }
  if (run_start != 0) flush(n);

  // Interior gaps: the unclaimed span strictly between consecutive segments.
  for (std::size_t i = 1; i < r.segments.size(); ++i) {
    const int gap_start = r.segments[i - 1].end_pos + 1;
    const int gap_end = r.segments[i].start_pos - 1;
    if (gap_start <= gap_end)
      r.gaps.push_back(ReliefGap{gap_start, gap_end});
  }

  // Claimed iids not on the spine are dropped from the relief but reported.
  // Sorted because `claimed` is an unordered_set (iteration order is unstable).
  for (const auto& iid : claimed)
    if (pos_of.find(iid) == pos_of.end())
      r.unplaced.push_back(iid);
  std::sort(r.unplaced.begin(), r.unplaced.end());

  return r;
}

}  // namespace Folio
