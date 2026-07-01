// TEST_timeline_cascade.cpp — s97 gap-duration authoring (slice 2) pure layer.
// Exercises Folio::cascade_shift: set the gap at a seam to an exact duration and
// shift the downstream block by the delta, internal spacing preserved. Seam index
// is the right-hand scene of the gap (1..N-1); rank 0 has no left anchor.
//
// Build + run (sandbox g++, header is dependency-free):
/*
g++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I include -o /tmp/test_cascade TEST_timeline_cascade.cpp && /tmp/test_cascade
clang++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I include -o /tmp/test_cascade TEST_timeline_cascade.cpp && /tmp/test_cascade
*/

#include "TimelineChrono.hpp"
#include <iostream>
#include <vector>

using namespace Folio;

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const std::string& what) {
  if (ok) { ++g_pass; }
  else { ++g_fail; std::cout << "  FAIL: " << what << "\n"; }
}

// Helper: assert the writes vector matches an expected (iid,coord) list exactly.
static bool writes_eq(const std::vector<ChronoMove>& w,
                      const std::vector<std::pair<std::string, long long>>& exp) {
  if (w.size() != exp.size()) return false;
  for (std::size_t i = 0; i < w.size(); ++i)
    if (w[i].iid != exp[i].first || w[i].coord != exp[i].second) return false;
  return true;
}

int main() {
  const long long DAY  = 86400;
  const long long YEAR = 365 * DAY;

  // A, B, C, D at 0, 1d, 2d, 3d  (even one-day spacing).
  std::vector<ChronoDated> d = {
    {"A", 0}, {"B", DAY}, {"C", 2 * DAY}, {"D", 3 * DAY},
  };

  // --- widen the A|B seam (seam_k = 1) to 1 year -------------------------------
  {
    auto w = cascade_shift(d, 1, YEAR);
    const long long delta = YEAR - DAY;   // B was at 1 day; target = 0 + 1yr
    check(writes_eq(w, {
      {"B", DAY + delta}, {"C", 2 * DAY + delta}, {"D", 3 * DAY + delta},
    }), "widen seam 1 to 1yr shifts B,C,D by delta, spacing preserved");
    // interior gaps unchanged: C-B and D-C still one day each
    check(w[1].coord - w[0].coord == DAY && w[2].coord - w[1].coord == DAY,
          "interior spacing preserved after widen");
    // new A|B gap is exactly a year
    check(w[0].coord - d[0].coord == YEAR, "A|B gap is exactly the set duration");
  }

  // --- shrink the C|D seam (seam_k = 3): only D moves --------------------------
  {
    auto w = cascade_shift(d, 3, 0);     // D coincident with C (gap 0)
    check(writes_eq(w, { {"D", 2 * DAY} }), "shrink last seam moves only the tail");
  }

  // --- middle seam B|C (seam_k = 2): C and D shift, A and B fixed --------------
  {
    auto w = cascade_shift(d, 2, 10 * DAY);
    const long long target = DAY + 10 * DAY;       // C target = B + 10d
    const long long delta  = target - 2 * DAY;
    check(writes_eq(w, {
      {"C", 2 * DAY + delta}, {"D", 3 * DAY + delta},
    }), "middle seam shifts only the downstream block");
    check(w.size() == 2, "upstream scenes (A,B) untouched");
  }

  // --- no-op when the gap already equals the set value -------------------------
  {
    auto w = cascade_shift(d, 1, DAY);   // A|B already one day
    check(w.empty(), "setting a seam to its current gap is a no-op");
  }

  // --- seam 0 has no left anchor -> empty --------------------------------------
  {
    check(cascade_shift(d, 0, YEAR).empty(), "seam 0 (clock start) yields no writes");
  }

  // --- out-of-range seam -> empty ----------------------------------------------
  {
    check(cascade_shift(d, 4, YEAR).empty(), "seam == N yields no writes");
    check(cascade_shift(d, 99, YEAR).empty(), "seam past N yields no writes");
  }

  // --- single dated scene: no seam exists --------------------------------------
  {
    std::vector<ChronoDated> one = { {"X", 500} };
    check(cascade_shift(one, 1, YEAR).empty(), "single dated scene has no seam");
    check(cascade_shift(one, 0, YEAR).empty(), "single dated scene seam 0 empty");
  }

  // --- empty input -------------------------------------------------------------
  {
    std::vector<ChronoDated> none;
    check(cascade_shift(none, 0, YEAR).empty(), "empty dated yields no writes");
    check(cascade_shift(none, 1, YEAR).empty(), "empty dated, seam 1, no writes");
  }

  // --- delta carries a negative correctly (shrink a wide gap) ------------------
  {
    std::vector<ChronoDated> wide = { {"P", 0}, {"Q", 5 * YEAR}, {"R", 5 * YEAR + DAY} };
    auto w = cascade_shift(wide, 1, YEAR);   // pull Q from 5yr down to 1yr
    const long long delta = YEAR - 5 * YEAR; // negative
    check(writes_eq(w, {
      {"Q", 5 * YEAR + delta}, {"R", 5 * YEAR + DAY + delta},
    }), "shrinking a wide gap shifts the block down (negative delta)");
    check(w[1].coord - w[0].coord == DAY, "Q|R interior gap preserved through shrink");
  }

  std::cout << (g_fail == 0 ? "ALL PASS" : "FAILURES") << ": "
            << g_pass << " passed, " << g_fail << " failed\n";
  return g_fail == 0 ? 0 : 1;
}
