//
// TEST_anchor.cpp -- folioedit :: Anchor (pure re-anchor ladder, §7)
//
// Proves the three rungs against a scene that drifts under the annotation:
//   Offset  -- no drift, offsets verify against the quote.
//   Quote   -- drift shifted the text; the quote is found and re-anchored
//              (single match; multiple matches -> nearest, flagged ambiguous).
//   Floating -- quote gone (or absent + version unknown) -> unplaceable, 0..0.
// Plus UTF-8 codepoint correctness (multibyte before the span must not skew
// offsets) and the no-quote / version_matches fallback.
//
// Pure STL -- sandbox-compilable with strict flags, no OpenSSL.
//
/*
g++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I include tests/TEST_anchor.cpp src/Anchor.cpp -o /tmp/test_anchor && /tmp/test_anchor
clang++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I include tests/TEST_anchor.cpp src/Anchor.cpp -o /tmp/test_anchor && /tmp/test_anchor
*/

#include "folioedit/Anchor.hpp"

#include <iostream>
#include <string>

namespace fe = folioedit;

static int g_pass = 0, g_fail = 0;
static void check(const std::string& what, bool cond) {
    if (cond) { ++g_pass; }
    else      { ++g_fail; std::cerr << "  FAIL: " << what << "\n"; }
}

int main() {
    using M = fe::AnchorMethod;

    // ── utf8_length ──────────────────────────────────────────────────────────
    check("ascii length",      fe::utf8_length("hello") == 5);
    check("empty length",      fe::utf8_length("") == 0);
    check("multibyte length",  fe::utf8_length("caf\xC3\xA9") == 4);       // café
    check("emoji length",      fe::utf8_length("a\xF0\x9F\x98\x80" "b") == 3); // a😀b

    // ── rung 1: Offset (no drift) ────────────────────────────────────────────
    {
        std::string text = "Hello world, goodbye moon.";
        // "world" is codepoints 6..11
        fe::AnchorResult r = fe::reanchor(text, 6, 11, "world");
        check("offset trusted when it matches", r.method == M::Offset &&
              r.range_start == 6 && r.range_end == 11 && !r.ambiguous);
    }

    // ── rung 2: Quote (drift shifted the text) ───────────────────────────────
    {
        std::string original = "Hello world, goodbye moon.";
        // author inserted "Oh, " at the front -> "world" now at 10..15
        std::string drifted  = "Oh, Hello world, goodbye moon.";
        // stale offsets 6..11 point at "o wor" now, not "world"
        fe::AnchorResult r = fe::reanchor(drifted, 6, 11, "world");
        check("re-anchored by quote after drift", r.method == M::Quote &&
              r.range_start == 10 && r.range_end == 15 && !r.ambiguous);
        (void)original;
    }

    // ── rung 2: multiple matches -> nearest, flagged ambiguous ───────────────
    {
        std::string text = "cat, then a cat, and one more cat";
        //  positions of "cat": 0, 12, 30  (codepoints == bytes here)
        // original offset near the middle occurrence
        fe::AnchorResult r = fe::reanchor(text, 13, 16, "cat");
        check("ambiguous picks nearest occurrence", r.method == M::Quote &&
              r.range_start == 12 && r.range_end == 15 && r.ambiguous);
    }

    // ── rung 3: Floating (quote gone) ────────────────────────────────────────
    {
        std::string text = "The scene was entirely rewritten overnight.";
        fe::AnchorResult r = fe::reanchor(text, 6, 11, "world");
        check("floating when quote absent from text",
              r.floating() && r.range_start == 0 && r.range_end == 0);
    }

    // ── UTF-8: multibyte before the span must not skew the offset ────────────
    {
        // "café world" -> c a f é(1 cp, 2 bytes) space w o r l d
        // codepoints: c0 a1 f2 é3 (sp)4 w5 o6 r7 l8 d9  -> "world" is 5..10
        std::string text = "caf\xC3\xA9 world";
        check("multibyte scene length", fe::utf8_length(text) == 10);
        fe::AnchorResult r = fe::reanchor(text, 5, 10, "world");
        check("offset trusted across a multibyte char", r.method == M::Offset &&
              r.range_start == 5 && r.range_end == 10);

        // drift the same multibyte scene; quote re-anchor must still land right
        std::string drifted = "Ah, caf\xC3\xA9 world";   // +4 codepoints ("Ah, ")
        fe::AnchorResult r2 = fe::reanchor(drifted, 5, 10, "world");
        check("quote re-anchor across multibyte", r2.method == M::Quote &&
              r2.range_start == 9 && r2.range_end == 14);
    }

    // ── no quote captured: version_matches decides ───────────────────────────
    {
        std::string text = "Hello world.";
        fe::AnchorResult trust = fe::reanchor(text, 0, 5, "", /*version_matches=*/true);
        check("empty quote + version match -> trust offsets",
              trust.method == M::Offset && trust.range_start == 0 && trust.range_end == 5);

        fe::AnchorResult drift = fe::reanchor(text, 0, 5, "", /*version_matches=*/false);
        check("empty quote + no version match -> floating", drift.floating());

        // even with version_matches, an out-of-range offset can't be trusted
        fe::AnchorResult oob = fe::reanchor(text, 50, 60, "", true);
        check("empty quote, oob offset -> floating", oob.floating());
    }

    // ── malformed / boundary offsets fall through safely ─────────────────────
    {
        std::string text = "alpha beta gamma";
        // start > end -> not sane; quote still saves it
        fe::AnchorResult r = fe::reanchor(text, 11, 4, "beta");
        check("inverted range recovered by quote", r.method == M::Quote &&
              r.range_start == 6 && r.range_end == 10);

        // quote longer than the whole scene -> floating
        fe::AnchorResult big = fe::reanchor("hi", 0, 2, "a very long quote indeed");
        check("over-long quote -> floating", big.floating());
    }

    std::cout << "TEST_anchor: " << g_pass << "/" << (g_pass + g_fail) << " passed\n";
    return g_fail == 0 ? 0 : 1;
}
