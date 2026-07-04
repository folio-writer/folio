//
// TEST_textmap.cpp -- folioedit :: TextMap (§27.1/§27.2 forward/inverse mapping)
//
// The viewer's selection() maps a (line,col) mark FORWARD to a codepoint range +
// quote in the scene's visible text; focus_note() must map a codepoint range
// (the re-anchored quote) INVERSE back to (line,col) so it can be highlighted.
// This proves visible_text + map_range are exact inverses of that forward step:
//   * a forward-mapped mark round-trips through map_range to the same byte
//     endpoints (single-line, multi-line, cross-paragraph, UTF-8 before/inside);
//   * chrome (title / blank / notes) lines are skipped, never landed on;
//   * degenerate / out-of-range inputs fail cleanly;
//   * and the real §27.2 path: reanchor(quote) -> map_range lands the highlight on
//     the right words after the prose has DRIFTED (offsets stale, quote moved).
//
// Pure STL -- sandbox-compilable with strict flags, no OpenSSL, no json.
//
/*
g++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I include tests/TEST_textmap.cpp src/TextMap.cpp src/Anchor.cpp -o /tmp/test_textmap && /tmp/test_textmap
clang++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I include tests/TEST_textmap.cpp src/TextMap.cpp src/Anchor.cpp -o /tmp/test_textmap && /tmp/test_textmap
*/

#include "folioedit/TextMap.hpp"
#include "folioedit/Anchor.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace fe = folioedit;

static int g_pass = 0, g_fail = 0;
static void check(const std::string& what, bool cond) {
    if (cond) { ++g_pass; }
    else      { ++g_fail; std::cerr << "  FAIL: " << what << "\n"; }
}

// Codepoints in the byte prefix s[0:byte] (mirrors the viewer's utf8_glyphs count
// used by selection() to produce cp offsets).
static int cp_before(const std::string& s, int byte) {
    int c = 0;
    for (int i = 0; i < byte && i < (int)s.size(); ) {
        unsigned char b = (unsigned char)s[(std::size_t)i];
        int step = (b < 0x80) ? 1 : (b >> 5) == 0x6 ? 2 : (b >> 4) == 0xE ? 3 : (b >> 3) == 0x1E ? 4 : 1;
        i += step; ++c;
    }
    return c;
}

// The FORWARD mapping, copied from ViewerBase::selection() so the test drives the
// exact same math the app does: a (al,ac)-(cl,cc) mark -> V byte endpoints -> cp
// range + quote. Returns the byte endpoints too, so the round-trip can be exact.
struct Fwd { int cp_start, cp_end, bstart, bend; std::string quote, V; };
static Fwd forward(const std::vector<fe::FlatLine>& flat, int al, int ac, int cl, int cc) {
    std::vector<int> off;
    std::string V = fe::visible_text(flat, off);
    int bstart = off[(std::size_t)al] + std::min(ac, (int)flat[(std::size_t)al].text.size());
    int bend   = off[(std::size_t)cl] + std::min(cc, (int)flat[(std::size_t)cl].text.size());
    std::string quote = V.substr((std::size_t)bstart, (std::size_t)(bend - bstart));
    int cps = cp_before(V, bstart);
    int cpe = cps + fe::utf8_length(quote);
    return {cps, cpe, bstart, bend, quote, V};
}

// Assert map_range inverts forward(): the returned endpoints, mapped back through
// byte_off, reproduce the forward byte endpoints exactly.
static void roundtrip(const std::string& what, const std::vector<fe::FlatLine>& flat,
                      int al, int ac, int cl, int cc) {
    Fwd f = forward(flat, al, ac, cl, cc);
    std::vector<int> off;
    (void)fe::visible_text(flat, off);
    fe::LineCol s, e;
    bool ok = fe::map_range(flat, f.cp_start, f.cp_end, s, e);
    check(what + " maps", ok);
    if (!ok) return;
    int rb0 = off[(std::size_t)s.line] + s.col;
    int rb1 = off[(std::size_t)e.line] + e.col;
    check(what + " start byte", rb0 == f.bstart);
    check(what + " end byte",   rb1 == f.bend);
    check(what + " reconstructs quote",
          f.V.substr((std::size_t)rb0, (std::size_t)(rb1 - rb0)) == f.quote);
}

int main() {
    // A scene as the viewer flattens it: a title + gap (chrome), then two prose
    // paragraphs, the first soft-wrapped into two visual lines.
    //   line 0  "The Cellar"      (title, non-prose)
    //   line 1  ""                (gap,  non-prose)
    //   line 2  "She opened the"  (prose, para_start)     <- para 1, wrap 1
    //   line 3  "old iron door."  (prose)                 <- para 1, wrap 2
    //   line 4  "Dust fell."      (prose, para_start)     <- para 2
    std::vector<fe::FlatLine> flat = {
        {"The Cellar",     false, true},
        {"",               false, false},
        {"She opened the", true,  true},
        {"old iron door.", true,  false},
        {"Dust fell.",     true,  true},
    };
    // Its visible text: "She opened the old iron door.\n\nDust fell."
    {
        std::vector<int> off;
        std::string V = fe::visible_text(flat, off);
        check("V joins wrap with space, para with blank",
              V == "She opened the old iron door.\n\nDust fell.");
        check("off skips title",  off[0] == -1);
        check("off skips gap",    off[1] == -1);
        check("off line2 == 0",   off[2] == 0);
        check("off line3 past sp", off[3] == 15);   // "She opened the" (14) + " " (1)
    }

    // ── round-trips ──────────────────────────────────────────────────────────
    roundtrip("single line",     flat, 2, 4, 2, 10);   // "ened " within line 2
    roundtrip("across wrap",     flat, 2, 4, 3, 8);     // spans the soft-wrap join
    roundtrip("across paragraph",flat, 2, 0, 4, 4);     // spans the \n\n break
    roundtrip("whole prose",     flat, 2, 0, 4, 10);    // start of prose to its end

    // ── chrome is never a landing site ────────────────────────────────────────
    {
        // cp range covering the very first prose codepoints: must land on line 2,
        // never the title/gap above it.
        fe::LineCol s, e;
        bool ok = fe::map_range(flat, 0, 3, s, e);   // "She"
        check("first prose maps", ok);
        check("start on prose line 2", ok && s.line == 2 && s.col == 0);
    }

    // ── degenerate / bounds ───────────────────────────────────────────────────
    {
        fe::LineCol s, e;
        check("empty range fails",   !fe::map_range(flat, 5, 5, s, e));
        check("reversed range fails",!fe::map_range(flat, 8, 3, s, e));
        std::vector<fe::FlatLine> nochrome = {{"Title", false, true}, {"", false, false}};
        check("no prose fails",      !fe::map_range(nochrome, 0, 2, s, e));
    }

    // ── the real §27.2 path: reanchor a returned quote onto DRIFTED prose, then
    //    map that codepoint range to (line,col) for the highlight ──────────────
    {
        // The author revised: inserted a clause before the quoted span, so the
        // note's stored offsets are stale but the quote text still exists.
        std::vector<fe::FlatLine> revised = {
            {"The Cellar",              false, true},
            {"",                        false, false},
            {"After a moment she",      true,  true},
            {"opened the old iron door.",true, false},
        };
        std::vector<int> off;
        std::string V = fe::visible_text(revised, off);
        // V = "After a moment she opened the old iron door."
        const std::string quote = "old iron door.";
        // stale offsets from the pre-edit pass (point too early now):
        fe::AnchorResult ar = fe::reanchor(V, 15, 29, quote);
        check("reanchor found the moved quote", ar.method == fe::AnchorMethod::Quote);
        fe::LineCol s, e;
        bool ok = fe::map_range(revised, ar.range_start, ar.range_end, s, e);
        check("reanchored range maps", ok);
        // it lands entirely on the second prose line (line 3)
        check("highlight on line 3", ok && s.line == 3);
        int rb0 = off[(std::size_t)s.line] + s.col;
        int rb1 = off[(std::size_t)e.line] + e.col;
        check("highlight covers the quote",
              ok && V.substr((std::size_t)rb0, (std::size_t)(rb1 - rb0)) == quote);
    }

    // ── UTF-8 before AND inside the span keeps offsets honest ─────────────────
    {
        std::vector<fe::FlatLine> uni = {
            {"caf\xC3\xA9 au lait", true, true},   // "café au lait" (é is 2 bytes)
        };
        // mark "au" : bytes 6..8 (after "café ", café=5 cps/6 bytes, space=1)
        roundtrip("utf8 before span", uni, 0, 6, 0, 8);
        // mark that starts on the é itself
        roundtrip("utf8 inside span", uni, 0, 3, 0, 8);
    }

    std::cout << "TEST_textmap: " << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail == 0 ? 0 : 1;
}
