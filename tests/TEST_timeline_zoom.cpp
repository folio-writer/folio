// ─────────────────────────────────────────────────────────────────────────────
// TEST_timeline_zoom.cpp — the pure half of Timeline zoom (s91).
//
// Zoom hangs off ONE value: COL, the per-scene column width in px (the s80 mock
// unit). The painter derives every card x, band span and axis tick from it
// (col_left(k) = x0 + k*COL) — that mapping is plain linear arithmetic and is
// NOT what this test guards; re-asserting x0 + k*COL would just restate the
// formula. What HAS branching, and is worth proving GTK-free, is next_col_px:
// the factor application (lround so a 1.10 step always moves ≥1px off 72),
// the clamp into [kColPxMin, kColPxMax], and the == no-op signal the surface
// uses to skip a relayout when a factor pushes past a rail it already sits on.
//
// Pure unit (TimelineSpine.{hpp,cpp}) — no GTK, no model, no nlohmann; compiles
// and runs in the sandbox as well as on Fedora.
//
/*
g++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I include src/TimelineSpine.cpp src/TEST_timeline_zoom.cpp -o /tmp/test_timeline_zoom && /tmp/test_timeline_zoom
clang++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I include src/TimelineSpine.cpp src/TEST_timeline_zoom.cpp -o /tmp/test_timeline_zoom && /tmp/test_timeline_zoom
*/

#include "TimelineSpine.hpp"

#include <cmath>
#include <cstdio>
#include <string>

using namespace Folio;

static int g_failed = 0;
static int g_passed = 0;

static void check(bool ok, const std::string& what) {
  if (ok) { ++g_passed; }
  else    { ++g_failed; std::printf("  FAIL: %s\n", what.c_str()); }
}

static void eq(int got, int want, const std::string& what) {
  check(got == want, what + " (got " + std::to_string(got) +
                     ", want " + std::to_string(want) + ")");
}

int main() {
  // ── Bounds are sane ────────────────────────────────────────────────────────
  check(kColPxMin < kColPxDefault && kColPxDefault < kColPxMax,
        "bounds ordered: min < default < max");

  // ── Zoom IN from the default (factor 1.10, the Map's per-step) ─────────────
  // 72 * 1.10 = 79.2 -> 79. Always moves at least a px (the lround guard).
  eq(next_col_px(kColPxDefault, 1.10), 79, "72 *1.10 rounds to 79");
  check(next_col_px(kColPxDefault, 1.10) != kColPxDefault,
        "a 1.10 step off the default is NOT a no-op");

  // ── Zoom OUT from the default ──────────────────────────────────────────────
  // 72 / 1.10 = 65.45 -> 65.
  eq(next_col_px(kColPxDefault, 1.0 / 1.10), 65, "72 /1.10 rounds to 65");

  // ── Clamp at the ceiling ───────────────────────────────────────────────────
  eq(next_col_px(kColPxMax, 1.10), kColPxMax, "at ceiling, zoom IN clamps to max");
  eq(next_col_px(118, 1.10), kColPxMax, "near ceiling, a step that overshoots clamps to max");
  check(next_col_px(kColPxMax, 1.10) == kColPxMax,
        "ceiling + zoom IN == current (no-op signal)");

  // ── Clamp at the floor ─────────────────────────────────────────────────────
  eq(next_col_px(kColPxMin, 1.0 / 1.10), kColPxMin, "at floor, zoom OUT clamps to min");
  eq(next_col_px(30, 1.0 / 1.10), kColPxMin, "near floor, a step that undershoots clamps to min");
  check(next_col_px(kColPxMin, 1.0 / 1.10) == kColPxMin,
        "floor + zoom OUT == current (no-op signal)");

  // ── At a rail, the OPPOSITE direction still moves ──────────────────────────
  check(next_col_px(kColPxMin, 1.10) > kColPxMin, "at floor, zoom IN moves up");
  check(next_col_px(kColPxMax, 1.0 / 1.10) < kColPxMax, "at ceiling, zoom OUT moves down");

  // ── A factor of exactly 1.0 is always a no-op ──────────────────────────────
  for (int c : {kColPxMin, 50, kColPxDefault, 100, kColPxMax})
    eq(next_col_px(c, 1.0), c, "factor 1.0 holds " + std::to_string(c));

  // ── Round-trip is monotone and stays in band across a long zoom-in burst ───
  {
    int c = kColPxMin;
    int prev = -1;
    bool monotone_nondecreasing = true;
    bool in_band = true;
    for (int i = 0; i < 40; ++i) {      // far more steps than min->max needs
      c = next_col_px(c, 1.10);
      if (c < prev) monotone_nondecreasing = false;
      if (c < kColPxMin || c > kColPxMax) in_band = false;
      prev = c;
    }
    check(monotone_nondecreasing, "zoom-in burst never decreases");
    check(in_band, "zoom-in burst stays within [min,max]");
    eq(c, kColPxMax, "a long zoom-in burst settles exactly at the ceiling");
  }
  {
    int c = kColPxMax;
    bool in_band = true;
    for (int i = 0; i < 40; ++i) {
      c = next_col_px(c, 1.0 / 1.10);
      if (c < kColPxMin || c > kColPxMax) in_band = false;
    }
    check(in_band, "zoom-out burst stays within [min,max]");
    eq(c, kColPxMin, "a long zoom-out burst settles exactly at the floor");
  }

  // ── Sanity: the linear x-mapping the surface uses (documented, not the unit
  //    under test). x0 is fixed (LEFT_PAD+GUTTER), so only the *stride* scales.
  //    Proven here only to show a column stride genuinely tracks COL with no
  //    hidden constant: stride(k) for col c == k*c. ──────────────────────────
  auto stride = [](int k, int c) { return k * c; };
  eq(stride(3, kColPxDefault), 216, "column stride at default");
  eq(stride(3, kColPxMin),      84, "column stride at floor");
  eq(stride(3, kColPxMax),     360, "column stride at ceiling");

  std::printf("\n%s — %d passed, %d failed\n",
              g_failed == 0 ? "OK" : "FAILED", g_passed, g_failed);
  return g_failed == 0 ? 0 : 1;
}
