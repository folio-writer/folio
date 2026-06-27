// ─────────────────────────────────────────────────────────────────────────────
// TimelineSweep.cpp — the subject-first sweep planner (s80 step 5). See header.
// Normalize the span, clamp to the spine, walk it in told order, split each scene
// into add (not yet claimed) vs already (claimed by any source).
// ─────────────────────────────────────────────────────────────────────────────

#include "TimelineSweep.hpp"

#include <algorithm>

namespace Folio {

SweepPlan plan_sweep(const std::vector<std::string>& spine,
                     int from_pos, int to_pos,
                     const std::unordered_set<std::string>& claimed) {
  SweepPlan plan;
  const int n = static_cast<int>(spine.size());
  if (n == 0) return plan;

  int lo = std::min(from_pos, to_pos);
  int hi = std::max(from_pos, to_pos);
  lo = std::max(lo, 1);
  hi = std::min(hi, n);
  if (lo > hi) return plan;  // span entirely off the spine

  for (int p = lo; p <= hi; ++p) {
    const std::string& iid = spine[static_cast<std::size_t>(p - 1)];
    if (claimed.count(iid) != 0)
      plan.already.push_back(iid);
    else
      plan.add.push_back(iid);
  }
  return plan;
}

}  // namespace Folio
