//
// folioedit :: Anchor -- see Anchor.hpp. Pure STL only.
//
#include "folioedit/Anchor.hpp"

#include <cstddef>
#include <vector>

namespace folioedit {

namespace {

// Byte index of the start of each codepoint, plus a trailing sentinel == size().
// So starts.size() - 1 == number of codepoints, and codepoint k spans bytes
// [starts[k], starts[k+1]). A byte is a UTF-8 continuation iff (b & 0xC0)==0x80;
// every other byte begins a codepoint. Malformed input still yields a consistent
// partition (each stray lead/ASCII byte is its own "codepoint").
std::vector<std::size_t> codepoint_starts(const std::string& s) {
    std::vector<std::size_t> starts;
    starts.reserve(s.size() + 1);
    for (std::size_t i = 0; i < s.size(); ++i) {
        const unsigned char b = static_cast<unsigned char>(s[i]);
        if ((b & 0xC0) != 0x80) starts.push_back(i);   // not a continuation byte
    }
    starts.push_back(s.size());                          // sentinel
    return starts;
}

}  // namespace

int utf8_length(const std::string& s) {
    // codepoint_starts has one entry per codepoint plus the sentinel.
    return static_cast<int>(codepoint_starts(s).size()) - 1;
}

AnchorResult reanchor(const std::string& current_text,
                      int range_start, int range_end,
                      const std::string& quote,
                      bool version_matches) {
    AnchorResult r;   // defaults to Floating, 0..0

    const std::vector<std::size_t> starts = codepoint_starts(current_text);
    const int n = static_cast<int>(starts.size()) - 1;   // codepoints in current text

    const bool range_sane =
        range_start >= 0 && range_end >= range_start && range_end <= n;

    // Byte-substring of current_text spanning codepoints [cp, cp+len).
    auto cp_substr = [&](int cp, int len) -> std::string {
        const std::size_t a = starts[static_cast<std::size_t>(cp)];
        const std::size_t b = starts[static_cast<std::size_t>(cp + len)];
        return current_text.substr(a, b - a);
    };

    if (!quote.empty()) {
        // ── rung 1: offsets still point exactly at the quote ──────────────────
        if (range_sane && cp_substr(range_start, range_end - range_start) == quote) {
            r.method = AnchorMethod::Offset;
            r.range_start = range_start;
            r.range_end   = range_end;
            return r;
        }

        // ── rung 2: find the quote as text in the current scene ───────────────
        const int qlen = utf8_length(quote);
        if (qlen > 0 && qlen <= n) {
            int found_at   = -1;   // nearest match start (codepoints)
            int count      = 0;
            int best_dist  = 0;
            for (int i = 0; i + qlen <= n; ++i) {
                if (cp_substr(i, qlen) == quote) {
                    ++count;
                    const int dist = i >= range_start ? i - range_start : range_start - i;
                    if (found_at < 0 || dist < best_dist) {
                        found_at  = i;
                        best_dist = dist;
                    }
                }
            }
            if (count >= 1) {
                r.method      = AnchorMethod::Quote;
                r.range_start = found_at;
                r.range_end   = found_at + qlen;
                r.ambiguous   = (count > 1);
                return r;
            }
        }
        // fall through -> Floating
        return r;
    }

    // ── no quote captured: trust offsets only if the body is known un-drifted ─
    if (version_matches && range_sane) {
        r.method = AnchorMethod::Offset;
        r.range_start = range_start;
        r.range_end   = range_end;
        return r;
    }

    return r;   // Floating
}

}  // namespace folioedit
