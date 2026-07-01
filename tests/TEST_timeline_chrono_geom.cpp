// TEST_timeline_chrono_geom.cpp — s97 variable-spacing chrono geometry (gap
// authoring). Exercises chrono_col_cx / chrono_clamped_col / chrono_col_at /
// chrono_seam_nearest: a per-rank lead gap pushes that card + everything after it
// over, and pixel x inverts back to the right column or nearest seam.
//
// Build + run (sandbox g++, header dependency-free):
/*
g++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I include -o /tmp/test_geom TEST_timeline_chrono_geom.cpp && /tmp/test_geom
clang++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I include -o /tmp/test_geom TEST_timeline_chrono_geom.cpp && /tmp/test_geom
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

int main() {
  const double X0 = 100.0, COL = 72.0;

  // --- no leads: behaves exactly like the ordinal axis -------------------------
  {
    std::vector<double> leads(5, 0.0);   // 5 ranks, no gaps
    check(chrono_col_cx(leads, X0, COL, 0) == X0 + COL / 2.0, "rank 0 centre");
    check(chrono_col_cx(leads, X0, COL, 3) == X0 + 3 * COL + COL / 2.0, "rank 3 centre, no leads");
    // a click in rank 2's cell resolves to column 3 (1-based)
    check(chrono_col_at(leads, X0, COL, 5, X0 + 2 * COL + 1.0) == 3, "col_at midcell");
    check(chrono_clamped_col(leads, X0, COL, 5, X0 + 2 * COL + 1.0) == 3, "clamped midcell");
    // before the first / after the last clamps to the ends
    check(chrono_clamped_col(leads, X0, COL, 5, X0 - 50.0) == 1, "clamp before first");
    check(chrono_clamped_col(leads, X0, COL, 5, X0 + 999.0) == 5, "clamp after last");
  }

  // --- one lead before rank 2 pushes ranks 2,3,4 over by that amount -----------
  {
    std::vector<double> leads = {0.0, 0.0, 120.0, 0.0, 0.0};   // 120px gap before rank 2
    // ranks 0,1 unchanged
    check(chrono_col_cx(leads, X0, COL, 1) == X0 + COL + COL / 2.0, "rank 1 unchanged by a later lead");
    // rank 2 shifted right by 120
    check(chrono_col_cx(leads, X0, COL, 2) == X0 + 2 * COL + COL / 2.0 + 120.0, "rank 2 pushed over by its lead");
    // rank 4 shifted by the same 120 (spacing past the gap preserved)
    check(chrono_col_cx(leads, X0, COL, 4) == X0 + 4 * COL + COL / 2.0 + 120.0, "downstream pushed by the same lead");
    // interior spacing rank3-rank2 still one COL
    check(chrono_col_cx(leads, X0, COL, 3) - chrono_col_cx(leads, X0, COL, 2) == COL,
          "spacing past the gap stays COL");
  }

  // --- inverse lands on the right card despite the gap -------------------------
  {
    std::vector<double> leads = {0.0, 0.0, 120.0, 0.0, 0.0};
    // a click squarely on rank 3 (now shifted +120) must resolve to column 4
    const double cx3 = chrono_col_cx(leads, X0, COL, 3);
    check(chrono_col_at(leads, X0, COL, 5, cx3) == 4, "col_at finds shifted rank 3");
    check(chrono_clamped_col(leads, X0, COL, 5, cx3) == 4, "clamped finds shifted rank 3");
    // a click INSIDE the 120px lead region (between rank 1 and rank 2) is not over
    // any card -> col_at 0, but clamps toward the nearer card
    const double in_gap = chrono_col_left(leads, X0, COL, 2) - 60.0;   // mid of the lead band
    check(chrono_col_at(leads, X0, COL, 5, in_gap) == 0, "col_at in the gap is 0 (no card)");
    check(chrono_clamped_col(leads, X0, COL, 5, in_gap) == 3,
          "clamped in the gap snaps to the card the gap leads into (rank 2 -> col 3)");
  }

  // --- seam selection: nearest seam on the time bar ----------------------------
  {
    std::vector<double> leads(5, 0.0);
    // x near the rank1|rank2 boundary picks seam 2
    const double mid12 = (chrono_col_cx(leads, X0, COL, 1) + chrono_col_cx(leads, X0, COL, 2)) / 2.0;
    check(chrono_seam_nearest(leads, X0, COL, 5, mid12) == 2, "seam nearest the 1|2 boundary is 2");
    // x at the very left picks the first seam (1)
    check(chrono_seam_nearest(leads, X0, COL, 5, X0 - 99.0) == 1, "leftmost x -> seam 1");
    // x at the very right picks the last seam (n-1 = 4)
    check(chrono_seam_nearest(leads, X0, COL, 5, X0 + 9999.0) == 4, "rightmost x -> seam 4");
  }

  // --- seam selection respects an open gap's shifted geometry ------------------
  {
    std::vector<double> leads = {0.0, 0.0, 120.0, 0.0, 0.0};
    // the boundary that opened (between rank 1 and rank 2) is now wide; x in the
    // middle of that wide band should still resolve to seam 2
    const double band_mid = chrono_col_left(leads, X0, COL, 2) - 60.0;
    check(chrono_seam_nearest(leads, X0, COL, 5, band_mid) == 2, "wide-gap band still picks seam 2");
  }

  // --- degenerate inputs -------------------------------------------------------
  {
    std::vector<double> none;
    check(chrono_clamped_col(none, X0, COL, 0, X0) == 0, "no ranks -> clamped 0");
    check(chrono_seam_nearest(none, X0, COL, 0, X0) == 0, "no ranks -> no seam");
    std::vector<double> one(1, 0.0);
    check(chrono_seam_nearest(one, X0, COL, 1, X0) == 0, "single rank -> no seam");
    check(chrono_clamped_col(one, X0, COL, 1, X0 + 9999.0) == 1, "single rank clamps to 1");
  }

  // --- leads shorter than n (trailing ranks carry no lead) ---------------------
  {
    std::vector<double> leads = {0.0, 50.0};   // only 2 entries, n = 4
    check(chrono_col_cx(leads, X0, COL, 3) == X0 + 3 * COL + COL / 2.0 + 50.0,
          "rank 3 carries the rank-1 lead, no own lead past array end");
    check(chrono_clamped_col(leads, X0, COL, 4, chrono_col_cx(leads, X0, COL, 3)) == 4,
          "inverse works with a short leads array");
  }

  std::cout << (g_fail == 0 ? "ALL PASS" : "FAILURES") << ": "
            << g_pass << " passed, " << g_fail << " failed\n";
  return g_fail == 0 ? 0 : 1;
}
