// ─────────────────────────────────────────────────────────────────────────────
// test_html_offset_map.cpp — s88 (style round-trip fix)
//
// from_html recorded tag range offsets as BYTE positions in the raw `plain`
// string (HTML entities still encoded), but applied them with
// get_iter_at_offset(), which expects CHARACTER offsets in the DECODED buffer.
// On non-ASCII / entity content the styled runs drifted onto the wrong text.
//
// This mirrors the decode() lambda and the byte→char map verbatim and checks the
// map agrees with the actual character index in decode(plain) for ASCII, UTF-8
// (curly quotes, em-dash), and HTML entities.
// ─────────────────────────────────────────────────────────────────────────────
/*
  Sandbox (g++):
    g++ -std=c++20 -Wall -Wextra -Wconversion -Wshadow \
        test/test_html_offset_map.cpp -o /tmp/test_off && /tmp/test_off

  Fedora (clang):
    clang++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow \
        test/test_html_offset_map.cpp -o /tmp/test_off && /tmp/test_off
*/
#include <cstdio>
#include <string>
#include <vector>

// ── decode() — verbatim from EditorHtmlSerializer.cpp ────────────────────────
static std::string decode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    size_t j = 0;
    while (j < s.size()) {
        if (s[j] == '&') {
            if      (s.compare(j, 5, "&amp;")  == 0) { out += '&'; j += 5; }
            else if (s.compare(j, 4, "&lt;")   == 0) { out += '<'; j += 4; }
            else if (s.compare(j, 4, "&gt;")   == 0) { out += '>'; j += 4; }
            else if (s.compare(j, 6, "&quot;") == 0) { out += '"'; j += 6; }
            else out += s[j++];
        } else {
            out += s[j++];
        }
    }
    return out;
}

// ── byte→char map — verbatim from the fix ────────────────────────────────────
static std::vector<int> make_b2c(const std::string& plain, int& cc_out) {
    std::vector<int> b2c(plain.size() + 1, 0);
    int cc = 0;
    size_t bi = 0;
    while (bi < plain.size()) {
        int blen = 1;
        if (plain[bi] == '&') {
            if      (plain.compare(bi, 5, "&amp;")  == 0) blen = 5;
            else if (plain.compare(bi, 4, "&lt;")   == 0) blen = 4;
            else if (plain.compare(bi, 4, "&gt;")   == 0) blen = 4;
            else if (plain.compare(bi, 6, "&quot;") == 0) blen = 6;
            else blen = 1;
        } else {
            unsigned char uc = (unsigned char)plain[bi];
            if      ((uc & 0x80) == 0x00) blen = 1;
            else if ((uc & 0xE0) == 0xC0) blen = 2;
            else if ((uc & 0xF0) == 0xE0) blen = 3;
            else if ((uc & 0xF8) == 0xF0) blen = 4;
            else                          blen = 1;
        }
        for (int k = 0; k < blen && bi + (size_t)k < plain.size(); ++k)
            b2c[bi + (size_t)k] = cc;
        bi += (size_t)blen;
        ++cc;
    }
    b2c[plain.size()] = cc;
    cc_out = cc;
    return b2c;
}

// Count UTF-8 code points (the "character" count GTK uses) in a decoded string.
static int cp_count(const std::string& s) {
    int n = 0;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        int len = 1;
        if      ((c & 0x80) == 0x00) len = 1;
        else if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        i += (size_t)len;
        ++n;
    }
    return n;
}

static int g_checks = 0;
#define CHECK(cond) do { ++g_checks; if(!(cond)){ \
    std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); return 1; } } while(0)

// For a raw plain string, verify b2c[byte_off] == char index in decode of the
// prefix plain[0..byte_off).
static int verify(const std::string& plain) {
    int cc = 0;
    auto b2c = make_b2c(plain, cc);
    CHECK(cc == cp_count(decode(plain)));     // total chars match
    // For every byte that starts a token, the mapped char offset must equal the
    // char length of the decoded prefix up to that byte.
    size_t bi = 0;
    while (bi <= plain.size()) {
        int expected = cp_count(decode(plain.substr(0, bi)));
        CHECK(b2c[bi] == expected);
        // advance to next token boundary
        if (bi == plain.size()) break;
        int blen = 1;
        if (plain[bi] == '&') {
            if      (plain.compare(bi, 5, "&amp;")  == 0) blen = 5;
            else if (plain.compare(bi, 4, "&lt;")   == 0) blen = 4;
            else if (plain.compare(bi, 4, "&gt;")   == 0) blen = 4;
            else if (plain.compare(bi, 6, "&quot;") == 0) blen = 6;
        } else {
            unsigned char uc = (unsigned char)plain[bi];
            if      ((uc & 0xE0) == 0xC0) blen = 2;
            else if ((uc & 0xF0) == 0xE0) blen = 3;
            else if ((uc & 0xF8) == 0xF0) blen = 4;
        }
        bi += (size_t)blen;
    }
    return 0;
}

int main() {
    // 1. Pure ASCII — byte offsets already equal char offsets.
    if (verify("Hello world")) return 1;

    // 2. Em-dash (U+2014, 3 bytes) — a styled run after it must not drift.
    if (verify("a\xE2\x80\x94" "b")) return 1;          // a — b

    // 3. Curly quotes (U+201C / U+201D, 3 bytes each).
    if (verify("\xE2\x80\x9C" "hi\xE2\x80\x9D")) return 1;

    // 4. HTML entities collapse (5/4/6 bytes → 1 char).
    if (verify("x&amp;y&lt;z&gt;w&quot;v")) return 1;

    // 5. Mixed entities + multibyte + ascii.
    if (verify("The \xE2\x80\x9C" "cat&amp;dog\xE2\x80\x9D ran\xE2\x80\x94" "fast")) return 1;

    // 6. Concrete drift example: a marker after 2 em-dashes is at byte 6 but
    //    character 2 — the old code would have applied a tag 4 chars too far.
    {
        std::string plain = "\xE2\x80\x94\xE2\x80\x94X"; // — — X
        int cc = 0; auto b2c = make_b2c(plain, cc);
        CHECK(b2c[6] == 2);   // byte 6 (the 'X') → char index 2
        CHECK(cc == 3);
    }

    std::printf("ALL %d CHECKS PASSED\n", g_checks);
    return 0;
}
