// ─────────────────────────────────────────────────────────────────────────────
// TEST_timeline_cluster.cpp — temporal clustering from the chronological step chain
// (DESIGN_timeline.md §9.14.9). cluster_chain() walks the signed-step chain and
// splits it into contiguous clusters, breaking where a step is coarser than the
// cluster's own prevailing scale; the signed gap between clusters carries direction
// (negative = the next cluster is a flashback).
//
// What this pins down (the break boundaries are exactly where this goes wrong):
//   • an hour-paced run stays one cluster; a day/week step breaks it (Scott's case);
//   • a week/month run breaks on a gap of years;
//   • a BACKWARD step breaks and the gap is negative (a flashback cluster);
//   • 0-second "meanwhile" steps never break and never define the scale;
//   • unit_jump widens the tolerance (a day no longer breaks an hour run at jump 2);
//   • per-cluster scale unit is the coarsest intra step; degenerate sizes are inert.
//   • ABSOLUTE FLOOR (Year): a year-or-coarser step is always a gap, even as the
//     first step out of a lone scene (A,+5yr,B splits A off) — months/weeks may still
//     pace a cluster. One known sub-floor edge stays joined (documented below).
// ─────────────────────────────────────────────────────────────────────────────
/*
  g++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I include TEST_timeline_cluster.cpp -o /tmp/tcl && /tmp/tcl
  clang++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I include TEST_timeline_cluster.cpp -o /tmp/tcl && /tmp/tcl
*/

#include "TimelineCluster.hpp"

#include <iostream>
#include <string>
#include <vector>

using namespace Folio;

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const std::string& what) {
  if (ok) ++g_pass; else { ++g_fail; std::cout << "  FAIL: " << what << "\n"; }
}

static const long long HOUR = unit_seconds(DurationUnit::Hour);
static const long long DAY  = unit_seconds(DurationUnit::Day);
static const long long WEEK = unit_seconds(DurationUnit::Week);
static const long long YEAR = unit_seconds(DurationUnit::Year);

// build a chain: first iid has step 0 (ignored), rest carry their step
static std::vector<ChainScene> chain(std::vector<std::pair<std::string, long long>> v) {
  std::vector<ChainScene> c;
  for (auto& p : v) c.push_back(ChainScene{p.first, p.second});
  return c;
}

int main() {
  // ── single + empty ──────────────────────────────────────────────────────────
  {
    ClusterLayout L = cluster_chain(chain({{"A", 0}}));
    check(L.clusters.size() == 1, "single scene → one cluster");
    check(L.clusters[0].first == 0 && L.clusters[0].last == 0, "single span [0,0]");
    check(L.gaps.empty(), "single scene → no gaps");
  }
  {
    ClusterLayout L = cluster_chain(chain({}));
    check(L.clusters.empty() && L.gaps.empty(), "empty chain inert");
  }

  // ── an hour-paced run stays one cluster (Scott's 10-on-the-hour) ─────────────
  {
    ClusterLayout L = cluster_chain(chain({
        {"s1", 0}, {"s2", HOUR}, {"s3", HOUR}, {"s4", HOUR},
        {"s5", HOUR}, {"s6", HOUR}, {"s7", HOUR}, {"s8", HOUR},
        {"s9", HOUR}, {"s10", HOUR}}));
    check(L.clusters.size() == 1, "ten hourly scenes → one cluster");
    check(L.clusters[0].iids.size() == 10, "cluster holds all ten");
    check(L.clusters[0].scale == DurationUnit::Hour, "cluster scale = hour");
  }

  // ── a day step breaks the hour run; the gap carries Day ──────────────────────
  {
    ClusterLayout L = cluster_chain(chain({
        {"a", 0}, {"b", HOUR}, {"c", HOUR},     // hour cluster
        {"d", DAY},                             // ← break
        {"e", HOUR}, {"f", HOUR}}));            // next hour cluster
    check(L.clusters.size() == 2, "day step breaks the hour run");
    check(L.clusters[0].last == 2 && L.clusters[1].first == 3, "split at the day step");
    check(L.gaps.size() == 1 && L.gaps[0].unit == DurationUnit::Day, "gap unit = day");
    check(L.gaps[0].step == DAY, "gap step = +1 day");
    check(L.clusters[1].scale == DurationUnit::Hour, "second cluster scale = hour");
  }

  // ── a week/month run breaks on a gap of years ────────────────────────────────
  {
    ClusterLayout L = cluster_chain(chain({
        {"p", 0}, {"q", WEEK}, {"r", WEEK},     // week cluster
        {"s", 2 * YEAR},                        // ← years break
        {"t", WEEK}}));
    check(L.clusters.size() == 2, "years break a week-paced run");
    check(L.clusters[0].scale == DurationUnit::Week, "first cluster scale = week");
    check(L.gaps[0].unit == DurationUnit::Year, "gap unit = year");
  }

  // ── BACKWARD step → flashback cluster, gap is negative ───────────────────────
  {
    ClusterLayout L = cluster_chain(chain({
        {"now1", 0}, {"now2", HOUR},            // present
        {"fb1", -2 * YEAR},                     // ← jump back: flashback begins
        {"fb2", HOUR}}));                        // flashback continues
    check(L.clusters.size() == 2, "backward step breaks into a flashback cluster");
    check(L.gaps.size() == 1 && L.gaps[0].step < 0, "flashback gap is negative");
    check(L.gaps[0].unit == DurationUnit::Year, "flashback gap unit = year");
    check(L.clusters[1].iids.size() == 2, "flashback cluster holds its run");
  }

  // ── 0-second "meanwhile" never breaks, never seeds the scale ─────────────────
  {
    ClusterLayout L = cluster_chain(chain({
        {"x", 0}, {"y", 0}, {"z", 0}}));        // three simultaneous scenes
    check(L.clusters.size() == 1, "all-meanwhile → one cluster");
    check(L.clusters[0].scale == DurationUnit::Second, "no real step → scale stays Second");
  }
  {
    ClusterLayout L = cluster_chain(chain({
        {"a", 0}, {"b", HOUR}, {"c", 0}, {"d", HOUR}}));  // meanwhile inside an hour run
    check(L.clusters.size() == 1, "meanwhile inside an hour run does not break");
    check(L.clusters[0].scale == DurationUnit::Hour, "scale stays hour, meanwhile ignored");
  }

  // ── unit_jump widens tolerance: a day no longer breaks an hour run at jump 2 ──
  {
    auto c = chain({{"a", 0}, {"b", HOUR}, {"c", DAY}, {"d", HOUR}});
    ClusterLayout j1 = cluster_chain(c, 1);
    ClusterLayout j2 = cluster_chain(c, 2);
    check(j1.clusters.size() == 2, "jump 1: day breaks the hour run");
    check(j2.clusters.size() == 1, "jump 2: day (1 rank coarser) no longer breaks");
  }
  {   // at jump 2 a WEEK (2 ranks over hour) still breaks
    ClusterLayout L = cluster_chain(chain({{"a", 0}, {"b", HOUR}, {"c", WEEK}, {"d", HOUR}}), 2);
    check(L.clusters.size() == 2, "jump 2: week (2 ranks) still breaks the hour run");
  }

  // ── ABSOLUTE FLOOR (Year): a lone scene + a coarse step splits cleanly ───────
  {   // A —+5yr→ B: the year floor breaks even as the first step, so A stands alone
    ClusterLayout L = cluster_chain(chain({{"A", 0}, {"B", 5 * YEAR}}));
    check(L.clusters.size() == 2, "A,+5yr,B → two clusters (year floor breaks first step)");
    check(L.clusters[0].iids.size() == 1 && L.clusters[1].iids.size() == 1, "both are singletons");
    check(L.gaps.size() == 1 && L.gaps[0].unit == DurationUnit::Year, "gap unit = year");
  }
  {   // the lone-leader-then-tight-run case the floor was added for
    ClusterLayout L = cluster_chain(chain({{"A", 0}, {"B", 5 * YEAR}, {"C", HOUR}, {"D", HOUR}}));
    check(L.clusters.size() == 2, "A | B,C,D — the year step splits A off, B opens an hour run");
    check(L.clusters[0].iids.size() == 1, "A alone");
    check(L.clusters[1].scale == DurationUnit::Hour, "B,C,D is an hour cluster");
  }
  {   // months MAY pace a cluster (floor is Year, not Month)
    ClusterLayout L = cluster_chain(chain({{"a", 0}, {"b", unit_seconds(DurationUnit::Month)},
                                           {"c", unit_seconds(DurationUnit::Month)}}));
    check(L.clusters.size() == 1, "month-paced run is one cluster (below the year floor)");
    check(L.clusters[0].scale == DurationUnit::Month, "cluster scale = month");
  }
  {   // a year cadence is gaps, not a cluster — each year step breaks
    ClusterLayout L = cluster_chain(chain({{"a", 0}, {"b", YEAR}, {"c", YEAR}, {"d", YEAR}}));
    check(L.clusters.size() == 4, "yearly cadence → four singletons (years are always gaps)");
  }
  {   // KNOWN sub-floor edge (documented, not a bug): a weeks-then-hours leader stays
      // joined because weeks are below the floor and max-scale doesn't retro-split.
    ClusterLayout L = cluster_chain(chain({{"A", 0}, {"B", 3 * WEEK}, {"C", HOUR}, {"D", HOUR}}));
    check(L.clusters.size() == 1, "sub-floor leader (3wk then hours) stays one cluster (known edge)");
  }

  std::cout << "\nTimelineCluster: " << g_pass << " passed, " << g_fail << " failed.\n";
  return g_fail == 0 ? 0 : 1;
}
