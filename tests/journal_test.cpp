// journal_test.cpp — sandbox unit test for the pure journal nav/index model.
// Exercises every query the journal surface leans on: calendar dates, current-
// entry resolution, resume point, and the concept relief. Pure; no GTK.

/*
  Build + run (sandbox g++):
    g++ -std=c++20 -Wall -Wextra -Werror -I../include \
        ../src/Journal.cpp journal_test.cpp -o journal_test && ./journal_test

  Fedora (clang):
    clang++ -std=c++20 -Wall -Wextra -Werror -I../include \
        ../src/Journal.cpp journal_test.cpp -o journal_test && ./journal_test
*/

#include "Journal.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace Folio;

static int g_checks = 0;
static int g_fails = 0;
static void check(bool ok, const char *what) {
  ++g_checks;
  if (!ok) {
    ++g_fails;
    std::printf("  FAIL: %s\n", what);
  }
}

// A three-day journal: two entries on Jun 21, one on Jun 22, one on Jun 23.
// Offsets partition a 400-char buffer; last entry runs to buffer end.
static std::vector<JEntry> sample_entries() {
  return {
      {"2026-06-21T09:00:00", 0, 100},    // 0
      {"2026-06-21T14:30:00", 100, 180},  // 1  (same day, later)
      {"2026-06-22T08:15:00", 180, 300},  // 2
      {"2026-06-23T22:05:00", 300, 400},  // 3  (newest)
  };
}

int main() {
  auto es = sample_entries();

  // --- j_date_key ---
  check(j_date_key("2026-06-23T22:05:00") == "2026-06-23", "date_key full");
  check(j_date_key("short").empty(), "date_key too short -> empty");
  check(j_date_key("2026-06-23").size() == 10, "date_key exactly 10");

  // --- j_dates_with_entries ---
  auto days = j_dates_with_entries(es);
  check(days.size() == 3, "three distinct days");
  check(days["2026-06-21"] == 2, "Jun 21 has 2 entries");
  check(days["2026-06-22"] == 1, "Jun 22 has 1 entry");
  check(days["2026-06-23"] == 1, "Jun 23 has 1 entry");
  check(days.count("2026-06-20") == 0, "empty day absent");

  // --- j_first_entry_on (calendar jump target) ---
  check(j_first_entry_on(es, "2026-06-21") == 0, "first on Jun 21 = entry 0");
  check(j_first_entry_on(es, "2026-06-22") == 2, "first on Jun 22 = entry 2");
  check(j_first_entry_on(es, "2026-06-23") == 3, "first on Jun 23 = entry 3");
  check(j_first_entry_on(es, "2026-06-20") == -1, "no entry on Jun 20 -> -1");

  // --- j_entry_at (you-are-here) ---
  check(j_entry_at(es, 0) == 0, "caret at 0 -> entry 0");
  check(j_entry_at(es, 50) == 0, "caret mid entry 0");
  check(j_entry_at(es, 100) == 1, "caret at boundary -> next entry");
  check(j_entry_at(es, 150) == 1, "caret mid entry 1");
  check(j_entry_at(es, 250) == 2, "caret mid entry 2");
  check(j_entry_at(es, 399) == 3, "caret in last entry");
  check(j_entry_at(es, 400) == 3, "caret at buffer end -> last entry");
  check(j_entry_at(es, 1000) == 3, "caret past end -> last entry");

  // --- j_last_entry / j_resume_offset ---
  check(j_last_entry(es) == 3, "newest is entry 3");
  check(j_resume_offset(es) == 400, "resume jumps to end of newest");

  // Out-of-append-order safety: newest by iso_dt, not by position.
  std::vector<JEntry> shuffled = {
      {"2026-06-23T22:05:00", 0, 100},  // newest first in vector
      {"2026-06-21T09:00:00", 100, 200},
  };
  check(j_last_entry(shuffled) == 0, "last_entry by iso_dt, not position");

  // Empty journal.
  std::vector<JEntry> none;
  check(j_last_entry(none) == -1, "empty -> no last entry");
  check(j_resume_offset(none) == 0, "empty -> resume at 0");
  check(j_entry_at(none, 5) == -1, "empty -> entry_at -1");

  // Caret before the first entry (e.g., leading whitespace).
  std::vector<JEntry> offset_start = {{"2026-06-21T09:00:00", 10, 100}};
  check(j_entry_at(offset_start, 3) == -1, "caret before first entry -> -1");

  // --- concepts: assignment + the relief ---
  std::vector<JConcept> cs = {
      {"redemption", 40, 50, -1},      // entry 0
      {"lighthouse", 120, 130, -1},    // entry 1
      {"redemption", 200, 210, -1},    // entry 2
      {"redemption", 360, 370, -1},    // entry 3
      {"lighthouse", 365, 375, -1},    // entry 3 (same entry, dup label)
  };
  j_assign_concepts(es, cs);
  check(cs[0].entry_index == 0, "concept 0 -> entry 0");
  check(cs[1].entry_index == 1, "concept 1 -> entry 1");
  check(cs[3].entry_index == 3, "concept 3 -> entry 3");

  auto idx = j_concept_index(cs);
  check(idx.size() == 2, "two distinct concepts");
  check((idx["redemption"] == std::vector<int>{0, 2, 3}),
        "redemption relief = entries 0,2,3");
  check((idx["lighthouse"] == std::vector<int>{1, 3}),
        "lighthouse relief = entries 1,3 (deduped)");

  // Unassigned concept (entry_index -1) is excluded from the relief.
  std::vector<JConcept> orphan = {{"ghost", 5, 9, -1}};
  auto oidx = j_concept_index(orphan);
  check(oidx.empty(), "unassigned concept excluded from index");

  // --- Title + starred ---
  check(j_first_line("\n  \nHello world\nsecond") == "Hello world",
        "first_line skips blank lines + trims");
  check(j_first_line("   ").empty(), "first_line all-blank -> empty");
  const std::string STAR = "\xE2\x98\x85";
  check(j_starred(STAR + " Favourite day"), "starred: leading star");
  check(!j_starred("Ordinary day"), "not starred");
  check(j_strip_star(STAR + "  Big day") == "Big day", "strip_star removes star+spaces");
  check(j_strip_star("No star") == "No star", "strip_star no-op when unstarred");

  std::vector<JEntry> fe = {
      {"2026-06-21T09:00:00", 0, 10, STAR + " Found the voice\nA barker cadence with carny zing.", "", ""},
      {"2026-06-22T08:15:00", 10, 20, "Pacing worry\nStalled at the wall again.", "teal", "\xF0\x9F\x94\xA5"},
      {"2026-06-23T22:05:00", 20, 30, "Monster rally\nDiesel and funnel cake, the tone is right.", "", ""},
  };
  check(j_entry_title(fe[0]) == "Found the voice", "title = first line, star stripped");
  check(j_entry_starred(fe[0]), "entry 0 starred");
  check(!j_entry_starred(fe[1]), "entry 1 not starred");
  check(j_entry_title(fe[1]) == "Pacing worry", "title plain");

  // --- Filter ---
  std::vector<JConcept> fc = {
      {"tone", 2, 6, 0},
      {"carny", 6, 9, 0},
      {"30k wall", 12, 15, 1},
      {"tone", 22, 25, 2},
  };
  // content (case-insensitive), searches body incl. title
  check((j_filter(fe, fc, "CARNY", {}, false) == std::vector<int>{0}),
        "content filter case-insensitive");
  check((j_filter(fe, fc, "tone", {}, false) == std::vector<int>{2}),
        "content 'tone' only in entry 2 body");
  check(j_filter(fe, fc, "nonsense", {}, false).empty(),
        "content no match -> empty");
  // tag filter (intersect / AND)
  check((j_filter(fe, fc, "", {"tone"}, false) == std::vector<int>{0, 2}),
        "tag 'tone' -> entries 0,2");
  check((j_filter(fe, fc, "", {"tone", "carny"}, false) == std::vector<int>{0}),
        "tags AND: tone+carny -> entry 0 only");
  // starred
  check((j_filter(fe, fc, "", {}, true) == std::vector<int>{0}),
        "starred-only -> entry 0");
  // combined + empty (no filter -> all)
  check((j_filter(fe, fc, "", {}, false) == std::vector<int>{0, 1, 2}),
        "no filter -> all entries");
  check(j_filter(fe, fc, "diesel", {"tone"}, false) == std::vector<int>{2},
        "content+tag combined -> entry 2");

  std::printf("\njournal_test: %d/%d checks passed\n", g_checks - g_fails,
              g_checks);
  return g_fails == 0 ? 0 : 1;
}
