// journal_codec_test.cpp — the dt:<payload> sidecar codec (s53 step 1).
//
// The extractor parses each dt stamp's tag name into iso/accent/mood with
// j_split_dt and the flair picker writes it back with j_make_dt. This proves
// the codec round-trips (bare iso, accent-only, full sidecar, emoji mood) and
// that the JEntry fields it fills drive Title / star / filter correctly — the
// pure half of "extractor fills text/accent/mood". The buffer-walking half
// (offsets -> body slice) is GTK-bound and lives in Editor_journal.cpp.

/* sandbox g++:
     g++ -std=c++20 -Wall -Wextra -Werror -I../include \
         journal_codec_test.cpp ../src/Journal.cpp -o /tmp/jcodec && /tmp/jcodec
   Fedora clang++:
     clang++ -std=c++20 -Wall -Wextra -Werror -I../include \
         journal_codec_test.cpp ../src/Journal.cpp -o /tmp/jcodec && /tmp/jcodec
*/

#include "Journal.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace Folio;

static int g_fail = 0;
static int g_pass = 0;
static void check(bool ok, const char *what) {
  if (ok) {
    ++g_pass;
  } else {
    ++g_fail;
    std::printf("  FAIL: %s\n", what);
  }
}

int main() {
  const std::string ISO = "2026-06-23T14:05:00";

  // ── split: bare iso (a freshly stamped entry has no sidecar) ──
  {
    std::string iso, acc, mood;
    j_split_dt(ISO, iso, acc, mood);
    check(iso == ISO, "bare: iso passes through");
    check(acc.empty(), "bare: accent empty");
    check(mood.empty(), "bare: mood empty");
  }

  // ── split: full sidecar ──
  {
    std::string iso, acc, mood;
    j_split_dt(ISO + "|amber|\xF0\x9F\x94\xA5", iso, acc, mood); // 🔥
    check(iso == ISO, "full: iso");
    check(acc == "amber", "full: accent");
    check(mood == "\xF0\x9F\x94\xA5", "full: mood emoji");
  }

  // ── split: accent only / mood only (empty fields survive) ──
  {
    std::string iso, acc, mood;
    j_split_dt(ISO + "|sky|", iso, acc, mood);
    check(iso == ISO && acc == "sky" && mood.empty(), "accent-only");
    j_split_dt(ISO + "||\xE2\x98\x80", iso, acc, mood); // ☀ mood, no accent
    check(iso == ISO && acc.empty() && mood == "\xE2\x98\x80", "mood-only");
  }

  // ── make: inverse, and the no-sidecar contract (bare iso back) ──
  {
    check(j_make_dt(ISO, "", "") == ISO, "make: no flair -> bare iso");
    check(j_make_dt(ISO, "amber", "\xF0\x9F\x94\xA5") ==
              ISO + "|amber|\xF0\x9F\x94\xA5",
          "make: full sidecar");
  }

  // ── round-trip make∘split == identity for every shape ──
  {
    struct Cse {
      std::string a, m;
    };
    std::vector<Cse> cs = {
        {"", ""}, {"amber", ""}, {"", "\xE2\x98\x80"}, {"rose", "\xF0\x9F\x8C\x99"}};
    for (auto &c : cs) {
      std::string p = j_make_dt(ISO, c.a, c.m);
      std::string iso, acc, mood;
      j_split_dt(p, iso, acc, mood);
      check(iso == ISO && acc == c.a && mood == c.m, "round-trip identity");
    }
  }

  // ── the fields the codec fills actually drive Title / star / filter ──
  {
    JEntry e;
    e.iso_dt = ISO;
    e.text = "\xE2\x98\x85 Monster Truck Rally\nDante would not approve.";
    e.accent = "amber";
    e.mood = "\xF0\x9F\x94\xA5";
    check(j_entry_title(e) == "Monster Truck Rally", "title = first line, star off");
    check(j_entry_starred(e), "leading star -> starred flag");

    std::vector<JEntry> es = {e};
    std::vector<JConcept> cs;
    check(j_filter(es, cs, "monster", {}, false) == std::vector<int>{0},
          "content filter hits the body the extractor filled");
    check(j_filter(es, cs, "", {}, true) == std::vector<int>{0},
          "starred filter sees the leading star");
    check(j_filter(es, cs, "nope", {}, false).empty(), "content miss excludes");
  }

  std::printf("journal_codec_test: %d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
