#pragma once
//
// folioedit :: TextMap -- the viewer's forward/inverse text mapping (§27.1/§27.2).
//
// The TUI viewer flattens a scene into WRAPPED visual lines (title + chrome +
// prose). Two things need the same coordinate space:
//
//   * selection()  turns a (line,col)..(line,col) mark into a CODEPOINT range +
//                  quote in the scene's VISIBLE TEXT (prose only, wrapped pieces
//                  rejoined) -- the forward direction, so a marked note's offsets
//                  live where absorb re-anchors them.
//   * focus_note() must do the INVERSE: given a codepoint range in that same
//                  visible text (the result of re-anchoring a returned note's
//                  quote, §27.2), find the (line,col) endpoints so the range can
//                  be highlighted and centred.
//
// This header factors the pure half of BOTH directions out of the FTXUI viewer
// so it is sandbox-testable: `visible_text` builds V exactly as selection() does,
// and `map_range` is its inverse. A forward/inverse round-trip is the acceptance
// test. No FTXUI, no crypto, no json -- plain strings + primitives.
//
#include <string>
#include <vector>

namespace folioedit {

// A flattened visual line as the viewer holds it: the wrapped text plus the two
// flags that decide how it joins into the scene's visible text.
struct FlatLine {
    std::string text;
    bool        prose      = false;  // markable scene prose (vs title/chrome)
    bool        para_start = false;  // first wrapped line of its paragraph
};

// Build the scene's VISIBLE TEXT exactly as the viewer's selection() does: join
// the PROSE lines only, a paragraph break as "\n\n", a soft-wrap continuation as
// " ". Fills byte_off[i] = the byte offset in V where prose line i's text begins
// (AFTER any separator), or -1 for a non-prose line. So a prose (line,col) maps
// to a V byte offset by byte_off[line]+col, and map_range inverts that.
std::string visible_text(const std::vector<FlatLine>& flat,
                         std::vector<int>& byte_off);

// One endpoint of a range in the viewer's own coordinates.
struct LineCol {
    int line = 0;   // index into `flat`
    int col  = 0;   // BYTE offset within that line's text, on a UTF-8 boundary
};

// Invert visible_text(): map a CODEPOINT range [cp_start, cp_end) in V onto
// (line,col) endpoints in `flat`. The START lands at the first prose line reaching
// cp_start (a codepoint sitting in a paragraph/word join rolls forward to the next
// prose line's start); the END closes on the last prose line before cp_end. Cols
// are snapped to UTF-8 boundaries. Returns false when `flat` has no prose or the
// range is degenerate / out of bounds. Deterministic and pure -- the exact inverse
// of the forward mapping selection() performs, so forward->inverse is the test.
bool map_range(const std::vector<FlatLine>& flat, int cp_start, int cp_end,
              LineCol& start_out, LineCol& end_out);

}  // namespace folioedit
