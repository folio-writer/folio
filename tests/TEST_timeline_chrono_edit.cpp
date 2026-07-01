// ─────────────────────────────────────────────────────────────────────────────
// TEST_timeline_chrono_edit.cpp — the drag-to-reorder WRITE path for the Chrono
// lens (DESIGN_timeline.md §9.14.8, build slice 1). chrono_reorder() takes the
// dated scenes in ascending-coordinate order, a from-rank and a to-rank, and
// returns the new iid order plus the coordinate(s) to persist so the next
// chronological_order() sorts the moved card into its new slot.
//
// What this pins down (a real test, not theatre — the midpoint/end/renormalize
// boundaries are exactly where a reorder goes wrong):
//   • new ORDER is correct for forward and backward moves and the no-op move;
//   • a move into the MIDDLE rewrites ONLY the moved card, to the integer midpoint
//     of its new neighbours (the rest of the spacing is preserved);
//   • a move to the FRONT / END steps out by `step` (no neighbour on that side);
//   • neighbours too close for an integer midpoint (delta < 2, or a tie) trigger a
//     full even-`step` renormalize of the whole run, never a tie;
//   • a single dated scene anchors at 0; out-of-range indices clamp.
// ─────────────────────────────────────────────────────────────────────────────
/*
  // sandbox (this machine):
  g++ -std=c++20 -Wall -Wextra -I include TEST_timeline_chrono_edit.cpp -o /tmp/tce && /tmp/tce

  // Fedora (the real toolchain):
  clang++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow \
      -I include TEST_timeline_chrono_edit.cpp -o /tmp/tce && /tmp/tce
*/

#include "TimelineChrono.hpp"

#include <iostream>
#include <string>
#include <vector>

using namespace Folio;

static int g_pass = 0, g_fail = 0;

static void check(bool ok, const std::string& what) {
  if (ok) { ++g_pass; }
  else    { ++g_fail; std::cout << "  FAIL: " << what << "\n"; }
}

static std::string join(const std::vector<std::string>& v) {
  std::string s;
  for (std::size_t i = 0; i < v.size(); ++i) { if (i) s += ","; s += v[i]; }
  return s;
}

// Find a write for `iid` in r.writes; returns true + sets out if present.
static bool wrote(const ChronoReorder& r, const std::string& iid, long long& out) {
  for (const auto& w : r.writes)
    if (w.iid == iid) { out = w.coord; return true; }
  return false;
}

int main() {
  // A 4-scene dated run, evenly a day apart: A0 B1 C2 D3 (coords ×86400).
  const long long D = 86400;   // the default step
  std::vector<ChronoDated> base = {
      {"A", 0 * D}, {"B", 1 * D}, {"C", 2 * D}, {"D", 3 * D}};

  // ── ORDER correctness ──────────────────────────────────────────────────────
  {   // move A (rank 0) to rank 2 → B,C,A,D
    ChronoReorder r = chrono_reorder(base, 0, 2);
    check(join(r.order) == "B,C,A,D", "fwd move order B,C,A,D");
  }
  {   // move D (rank 3) to rank 0 → D,A,B,C
    ChronoReorder r = chrono_reorder(base, 3, 0);
    check(join(r.order) == "D,A,B,C", "back move order D,A,B,C");
  }
  {   // no-op move (rank 1 → 1): order unchanged, no writes
    ChronoReorder r = chrono_reorder(base, 1, 1);
    check(join(r.order) == "A,B,C,D", "no-op move keeps order");
    check(r.writes.empty(),            "no-op move writes nothing");
    check(!r.renormalized,             "no-op move does not renormalize");
  }

  // ── MIDDLE move rewrites ONLY the moved card, to the midpoint ───────────────
  {   // move A to rank 2: new neighbours are C(2D) and D(3D) → midpoint 2.5D
    ChronoReorder r = chrono_reorder(base, 0, 2);
    check(r.writes.size() == 1,  "middle move writes exactly one card");
    check(!r.renormalized,        "middle move does not renormalize");
    long long c = -1;
    check(wrote(r, "A", c),       "middle move writes the moved card A");
    check(c == 2 * D + D / 2,     "A midpoint of C(2D) and D(3D) == 2.5 days");
    long long other;
    check(!wrote(r, "B", other) && !wrote(r, "C", other) && !wrote(r, "D", other),
          "middle move leaves the other cards' coordinates alone");
  }

  // ── END / FRONT moves step out by `step` (one open side) ────────────────────
  {   // move B to the END (rank 3): left neighbour D(3D), no right → 3D + step
    ChronoReorder r = chrono_reorder(base, 1, 3);
    check(join(r.order) == "A,C,D,B", "end move order A,C,D,B");
    long long c = -1;
    check(wrote(r, "B", c) && c == 3 * D + D, "end move B := last + step");
  }
  {   // move C to the FRONT (rank 0): right neighbour A(0), no left → 0 - step
    ChronoReorder r = chrono_reorder(base, 2, 0);
    check(join(r.order) == "C,A,B,D", "front move order C,A,B,D");
    long long c = 1;
    check(wrote(r, "C", c) && c == 0 - D, "front move C := first - step (negative ok)");
  }

  // ── RENORMALIZE: neighbours too close for an integer midpoint ───────────────
  {   // X0 Y1 Z2 — drop Z between X(0) and Y(1): delta 1, no integer between → all re-spaced
    std::vector<ChronoDated> tight = {{"X", 0}, {"Y", 1}, {"Z", 2}};
    ChronoReorder r = chrono_reorder(tight, 2, 1);
    check(join(r.order) == "X,Z,Y", "tight move order X,Z,Y");
    check(r.renormalized,           "tight neighbours trigger renormalize");
    check(r.writes.size() == 3,     "renormalize rewrites the whole run");
    long long x, z, y;
    check(wrote(r, "X", x) && x == 0 * D, "renorm X := 0");
    check(wrote(r, "Z", z) && z == 1 * D, "renorm Z := 1*step (its new rank)");
    check(wrote(r, "Y", y) && y == 2 * D, "renorm Y := 2*step (its new rank)");
    check(x < z && z < y,           "renorm coordinates are strictly increasing");
  }
  {   // a tie (two equal coordinates) is also < 2 apart → renormalize on a drop between
    std::vector<ChronoDated> ties = {{"P", 100}, {"Q", 100}, {"R", 500}};
    ChronoReorder r = chrono_reorder(ties, 2, 1);   // R between P(100) and Q(100)
    check(r.renormalized, "equal-coordinate neighbours (tie) renormalize");
  }

  // ── DEGENERATE sizes ────────────────────────────────────────────────────────
  {   // single dated scene moved onto itself: anchors at 0, no spurious write
    std::vector<ChronoDated> one = {{"solo", 999}};
    ChronoReorder r = chrono_reorder(one, 0, 0);
    check(join(r.order) == "solo", "single scene order");
    check(r.writes.empty(),        "single no-op writes nothing");
  }
  {   // empty input is inert
    std::vector<ChronoDated> none;
    ChronoReorder r = chrono_reorder(none, 0, 0);
    check(r.order.empty() && r.writes.empty(), "empty input inert");
  }
  {   // out-of-range to-index clamps to the last slot (defensive)
    ChronoReorder r = chrono_reorder(base, 0, 99);
    check(join(r.order) == "B,C,D,A", "out-of-range to clamps to end");
  }

  // ── ROUND-TRIP: the written coordinates actually re-sort to the new order ────
  {   // move A to rank 2, apply the write, re-run chronological_order, expect the order
    ChronoReorder r = chrono_reorder(base, 0, 2);
    std::vector<ChronoDated> after = base;
    for (auto& d : after)
      for (const auto& w : r.writes)
        if (d.iid == w.iid) d.coord = w.coord;
    std::vector<ChronoScene> told;
    for (const auto& d : after) told.push_back(ChronoScene{d.iid, true, d.coord});
    ChronoOrder co = chronological_order(told);
    check(join(co.chrono) == join(r.order),
          "written coordinate re-sorts to the dragged order (round-trip)");
  }

  std::cout << "\nTimelineChrono reorder: " << g_pass << " passed, "
            << g_fail << " failed.\n";
  return g_fail == 0 ? 0 : 1;
}
