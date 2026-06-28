// ─────────────────────────────────────────────────────────────────────────────
// TEST_timeline_threads.cpp — sandbox tests for assemble_thread_lanes (s83,
// DESIGN_timeline.md §9.12 / step 7, the arc model). Pure; GTK-free.
//
// Proves the THREAD-lane assembler (the "assigned arc" half of the §9.8 #2 fork)
// behaves like its KP sibling: grouping by thread_key, told-order claiming,
// first-appearance (braid) order, on-spine-only membership, frozen identity, and
// the deliberate DROPPED is_key_point gate (assignment IS the opt-in). The final
// block runs each lane's claimed set through the shipped compute_relief to prove
// threads are the fourth adapter on the one relief renderer (bar / dot / gap),
// including the no-hand-bounding case (a posthumous-style lone beat after a gap
// draws as a detached Dot for free).
//
// Build+run — bare command lines, copy-paste as a block:
/*
g++ -std=c++20 -Wall -Wextra -Wconversion -Wshadow -I include \
    tests/TEST_timeline_threads.cpp src/TimelineThreads.cpp src/TimelineRelief.cpp \
    -o /tmp/test_timeline_threads && /tmp/test_timeline_threads
clang++ -std=c++20 -Wall -Wextra -Wconversion -Wshadow -I include \
    tests/TEST_timeline_threads.cpp src/TimelineThreads.cpp src/TimelineRelief.cpp \
    -o /tmp/test_timeline_threads && /tmp/test_timeline_threads
*/
// ─────────────────────────────────────────────────────────────────────────────

#include "TimelineThreads.hpp"
#include "TimelineRelief.hpp"

#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace Folio;

static int g_pass = 0;
static int g_fail = 0;

static void check(bool cond, const std::string& what) {
  if (cond) {
    ++g_pass;
  } else {
    ++g_fail;
    std::cout << "  FAIL: " << what << "\n";
  }
}

// Convenience: build a SceneThreadInfo.
static SceneThreadInfo th(const std::string& key, const std::string& label = {},
                          int color_idx = 0) {
  return SceneThreadInfo{key, label, color_idx};
}

int main() {
  // ── 1. Empty spine → no lanes ──────────────────────────────────────────────
  {
    std::unordered_map<std::string, SceneThreadInfo> m{{"scn_a", th("frodo")}};
    auto lanes = assemble_thread_lanes({}, m);
    check(lanes.empty(), "empty spine yields no lanes");
  }

  // ── 2. Empty assignment map → no lanes ─────────────────────────────────────
  {
    auto lanes = assemble_thread_lanes({"scn_a", "scn_b"}, {});
    check(lanes.empty(), "no assignments yields no lanes");
  }

  // ── 3. Single thread, one contiguous block ─────────────────────────────────
  {
    std::vector<std::string> spine{"scn_1", "scn_2", "scn_3"};
    std::unordered_map<std::string, SceneThreadInfo> m{
        {"scn_1", th("frodo", "Frodo's line", 1)},
        {"scn_2", th("frodo", "Frodo's line", 1)},
        {"scn_3", th("frodo", "Frodo's line", 1)},
    };
    auto lanes = assemble_thread_lanes(spine, m);
    check(lanes.size() == 1, "single thread → one lane");
    if (!lanes.empty()) {
      check(lanes[0].thread_key == "frodo", "lane keyed on thread");
      check(lanes[0].label == "Frodo's line", "lane label frozen from first scene");
      check(lanes[0].color_idx == 1, "lane colour from first scene");
      check(lanes[0].first_pos == 1, "first_pos = earliest told position");
      check(lanes[0].claimed.size() == 3, "all three scenes claimed");
    }
  }

  // ── 4. Two threads braided → first-appearance (braid) order ────────────────
  // Told order: aragorn, frodo, aragorn, frodo. Aragorn first-appears at pos 1,
  // Frodo at pos 2, so the lane order must be [aragorn, frodo] regardless of the
  // map's (unordered) iteration.
  {
    std::vector<std::string> spine{"scn_1", "scn_2", "scn_3", "scn_4"};
    std::unordered_map<std::string, SceneThreadInfo> m{
        {"scn_1", th("aragorn", "Aragorn", 2)},
        {"scn_2", th("frodo", "Frodo", 1)},
        {"scn_3", th("aragorn", "Aragorn", 2)},
        {"scn_4", th("frodo", "Frodo", 1)},
    };
    auto lanes = assemble_thread_lanes(spine, m);
    check(lanes.size() == 2, "two threads → two lanes");
    if (lanes.size() == 2) {
      check(lanes[0].thread_key == "aragorn", "earlier first-appearance leads");
      check(lanes[1].thread_key == "frodo", "later first-appearance follows");
      check(lanes[0].first_pos == 1, "aragorn first_pos 1");
      check(lanes[1].first_pos == 2, "frodo first_pos 2");
      check(lanes[0].claimed.size() == 2 && lanes[1].claimed.size() == 2,
            "each braided thread claims its two scenes");
    }
  }

  // ── 5. Off-spine assignment dropped ────────────────────────────────────────
  // A scene assigned a thread but NOT on the spine (deleted / off-spine) never
  // enters — we only iterate the spine.
  {
    std::vector<std::string> spine{"scn_1", "scn_2"};
    std::unordered_map<std::string, SceneThreadInfo> m{
        {"scn_1", th("frodo", "Frodo", 1)},
        {"scn_2", th("frodo", "Frodo", 1)},
        {"scn_ghost", th("frodo", "Frodo", 1)},   // off-spine
    };
    auto lanes = assemble_thread_lanes(spine, m);
    check(lanes.size() == 1, "off-spine assignment does not split/extend");
    if (!lanes.empty())
      check(lanes[0].claimed.count("scn_ghost") == 0, "ghost scene not claimed");
    if (!lanes.empty())
      check(lanes[0].claimed.size() == 2, "only on-spine scenes claimed");
  }

  // ── 6. Unassigned scenes skipped (absent OR empty key) ─────────────────────
  {
    std::vector<std::string> spine{"scn_1", "scn_2", "scn_3", "scn_4"};
    std::unordered_map<std::string, SceneThreadInfo> m{
        {"scn_1", th("frodo", "Frodo", 1)},
        {"scn_2", th("", "", 0)},          // present but empty key → unassigned
        // scn_3 absent entirely → unassigned
        {"scn_4", th("frodo", "Frodo", 1)},
    };
    auto lanes = assemble_thread_lanes(spine, m);
    check(lanes.size() == 1, "empty-key and absent scenes do not form lanes");
    if (!lanes.empty()) {
      check(lanes[0].claimed.count("scn_2") == 0, "empty-key scene not claimed");
      check(lanes[0].claimed.count("scn_3") == 0, "absent scene not claimed");
      check(lanes[0].claimed.size() == 2, "frodo claims only its two scenes");
    }
  }

  // ── 7. NO is_key_point gate — the deliberate difference from KP ─────────────
  // SceneThreadInfo has no is_key_point field; a plain assignment is enough. This
  // test documents intent: every assigned scene counts, no opt-in flag needed.
  {
    std::vector<std::string> spine{"scn_1"};
    std::unordered_map<std::string, SceneThreadInfo> m{
        {"scn_1", th("frodo", "Frodo", 1)},   // no flag, yet it forms a lane
    };
    auto lanes = assemble_thread_lanes(spine, m);
    check(lanes.size() == 1, "assignment alone (no beat flag) forms a lane");
  }

  // ── 8. Identity frozen at first appearance; later drift ignored ────────────
  // A later scene of the same thread carries a blank label / different colour;
  // the lane keeps the FIRST scene's identity, never renaming out from under it.
  {
    std::vector<std::string> spine{"scn_1", "scn_2", "scn_3"};
    std::unordered_map<std::string, SceneThreadInfo> m{
        {"scn_1", th("frodo", "Frodo's line", 1)},
        {"scn_2", th("frodo", "", 0)},                  // blank drift
        {"scn_3", th("frodo", "Renamed Later", 7)},     // drifted label+colour
    };
    auto lanes = assemble_thread_lanes(spine, m);
    check(lanes.size() == 1, "drift stays one lane");
    if (!lanes.empty()) {
      check(lanes[0].label == "Frodo's line", "label frozen at first appearance");
      check(lanes[0].color_idx == 1, "colour frozen at first appearance");
      check(lanes[0].claimed.size() == 3, "all three still claimed");
    }
  }

  // ── 9. Determinism — stable order over repeated assembly ────────────────────
  {
    std::vector<std::string> spine{"scn_1", "scn_2", "scn_3"};
    std::unordered_map<std::string, SceneThreadInfo> m{
        {"scn_1", th("c", "C", 3)},
        {"scn_2", th("a", "A", 1)},
        {"scn_3", th("b", "B", 2)},
    };
    auto l1 = assemble_thread_lanes(spine, m);
    auto l2 = assemble_thread_lanes(spine, m);
    bool same = l1.size() == l2.size();
    for (std::size_t i = 0; same && i < l1.size(); ++i)
      same = (l1[i].thread_key == l2[i].thread_key);
    check(same, "assembly order is deterministic across calls");
    // First-appearance order is c(1), a(2), b(3) — by position, NOT alphabetical.
    if (l1.size() == 3) {
      check(l1[0].thread_key == "c" && l1[1].thread_key == "a" &&
                l1[2].thread_key == "b",
            "braid order is by first appearance, not key");
    }
  }

  // ── 10. Reorder independence — spine order drives everything ────────────────
  // Same assignment set, the spine reversed: first_pos and ordering recompute for
  // free (the §9.7 keyed-on-iid property). The lane that was first is now last.
  {
    std::unordered_map<std::string, SceneThreadInfo> m{
        {"scn_1", th("frodo", "Frodo", 1)},
        {"scn_2", th("aragorn", "Aragorn", 2)},
    };
    auto fwd = assemble_thread_lanes({"scn_1", "scn_2"}, m);
    auto rev = assemble_thread_lanes({"scn_2", "scn_1"}, m);
    check(fwd.size() == 2 && rev.size() == 2, "both orders produce two lanes");
    if (fwd.size() == 2 && rev.size() == 2) {
      check(fwd[0].thread_key == "frodo", "forward: frodo leads");
      check(rev[0].thread_key == "aragorn", "reversed: aragorn leads (recomputed)");
    }
  }

  // ── 11. END-TO-END — a thread lane through compute_relief (fourth adapter) ──
  // Boromir-style: a thread runs as a block 1..3, goes dark, then a single
  // posthumous beat at 6. No hand-bounding: the relief draws Bar(1-3),
  // interior Gap(4-5), Dot(6) — the death + memory falls out for free.
  {
    std::vector<std::string> spine{"scn_1", "scn_2", "scn_3",
                                   "scn_4", "scn_5", "scn_6"};
    std::unordered_map<std::string, SceneThreadInfo> m{
        {"scn_1", th("boromir", "Boromir", 4)},
        {"scn_2", th("boromir", "Boromir", 4)},
        {"scn_3", th("boromir", "Boromir", 4)},
        {"scn_6", th("boromir", "Boromir", 4)},   // posthumous lone beat
    };
    auto lanes = assemble_thread_lanes(spine, m);
    check(lanes.size() == 1, "boromir → one lane");
    if (!lanes.empty()) {
      Relief r = compute_relief(spine, lanes[0].claimed, lanes[0].label,
                                /*colour*/ {});
      check(r.segments.size() == 2, "two segments: the block and the lone beat");
      if (r.segments.size() == 2) {
        check(r.segments[0].kind == ReliefSegment::Kind::Bar &&
                  r.segments[0].start_pos == 1 && r.segments[0].end_pos == 3,
              "Bar(1-3) = the living run");
        check(r.segments[1].kind == ReliefSegment::Kind::Dot &&
                  r.segments[1].start_pos == 6,
              "Dot(6) = the posthumous beat, drawn for free");
      }
      check(r.gaps.size() == 1, "one interior gap");
      if (r.gaps.size() == 1)
        check(r.gaps[0].start_pos == 4 && r.gaps[0].end_pos == 5,
              "Gap(4-5) = the absence (focus surfaces, never verdicts)");
    }
  }

  // ── 12. END-TO-END — a fragmented thread (block, gap, block) ───────────────
  {
    std::vector<std::string> spine{"scn_1", "scn_2", "scn_3", "scn_4", "scn_5"};
    std::unordered_map<std::string, SceneThreadInfo> m{
        {"scn_1", th("now", "NOW", 1)},
        {"scn_2", th("now", "NOW", 1)},
        {"scn_4", th("now", "NOW", 1)},
        {"scn_5", th("now", "NOW", 1)},
    };
    auto lanes = assemble_thread_lanes(spine, m);
    if (!lanes.empty()) {
      Relief r = compute_relief(spine, lanes[0].claimed, lanes[0].label, {});
      check(r.segments.size() == 2, "two blocks across the gap");
      check(r.gaps.size() == 1, "one interior gap between blocks");
    }
  }

  // ── summary ────────────────────────────────────────────────────────────────
  std::cout << "TEST_timeline_threads: " << g_pass << " passed, " << g_fail
            << " failed\n";
  return g_fail == 0 ? 0 : 1;
}
