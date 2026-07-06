// ─────────────────────────────────────────────────────────────────────────────
// ResearchCapture.cpp — impl of the pure Research capture engine.
// See ResearchCapture.hpp. STL-only; the extractor is sandbox-tested.
// ─────────────────────────────────────────────────────────────────────────────
#include "ResearchCapture.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/wait.h>   // WIFEXITED / WEXITSTATUS — decode std::system's status
#endif

namespace Folio {
namespace {

// ── small text utilities ─────────────────────────────────────────────────────

// ASCII-lowercase a copy (for case-insensitive tag/attr matching only — never
// applied to extracted user-facing text).
std::string lower_ascii(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Append the UTF-8 encoding of a Unicode code point to `out`.
void append_utf8(std::string& out, unsigned long cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        append_utf8(out, 0xFFFD); // out of range -> replacement char
    }
}

// Decode the HTML entities we actually meet in titles/descriptions: the named
// five, a handful of common named ones, and numeric (&#NN; / &#xHH;). Unknown
// entities are passed through verbatim (safer than dropping text).
std::string decode_entities(const std::string& s) {
    static const std::array<std::pair<const char*, const char*>, 10> named = {{
        {"amp", "&"},   {"lt", "<"},    {"gt", ">"},
        {"quot", "\""}, {"apos", "'"},  {"nbsp", " "},
        {"mdash", "\xE2\x80\x94"}, {"ndash", "\xE2\x80\x93"},
        {"hellip", "\xE2\x80\xA6"}, {"amp;", "&"} /* defensive dup */
    }};

    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ) {
        if (s[i] != '&') { out.push_back(s[i++]); continue; }
        std::size_t semi = s.find(';', i + 1);
        // Only treat as an entity if a ';' is reasonably close.
        if (semi == std::string::npos || semi - i > 12) { out.push_back(s[i++]); continue; }
        std::string body = s.substr(i + 1, semi - i - 1);
        if (!body.empty() && body[0] == '#') {
            // numeric
            unsigned long cp = 0;
            bool ok = false;
            try {
                if (body.size() > 1 && (body[1] == 'x' || body[1] == 'X'))
                    cp = std::stoul(body.substr(2), nullptr, 16), ok = true;
                else
                    cp = std::stoul(body.substr(1), nullptr, 10), ok = true;
            } catch (...) { ok = false; }
            if (ok) { append_utf8(out, cp); i = semi + 1; continue; }
            out.push_back(s[i++]);
            continue;
        }
        std::string low = lower_ascii(body);
        bool matched = false;
        for (const auto& n : named) {
            if (low == n.first) { out += n.second; matched = true; break; }
        }
        if (matched) { i = semi + 1; continue; }
        out.push_back(s[i++]);
    }
    return out;
}

// Collapse all runs of ASCII whitespace to single spaces and trim ends.
std::string collapse_ws(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool in_space = false;
    for (unsigned char c : s) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') {
            in_space = true;
        } else {
            if (in_space && !out.empty()) out.push_back(' ');
            in_space = false;
            out.push_back(static_cast<char>(c));
        }
    }
    return out;
}

// Strip HTML tags from a fragment, then decode + collapse + tidy. Turns
// "<p>Hi <b>there</b></p>" into "Hi there". Each tag becomes a space (so
// "one<br>two" doesn't merge), then a cleanup drops any space that landed
// directly before closing punctuation (so "pilgrim</i>." -> "pilgrim." not
// "pilgrim .").
std::string strip_tags(const std::string& frag) {
    std::string no_tags;
    no_tags.reserve(frag.size());
    bool in_tag = false;
    for (char c : frag) {
        if (c == '<') { in_tag = true; continue; }
        if (c == '>') { in_tag = false; no_tags.push_back(' '); continue; }
        if (!in_tag) no_tags.push_back(c);
    }
    std::string cleaned = collapse_ws(decode_entities(no_tags));
    std::string out;
    out.reserve(cleaned.size());
    for (std::size_t i = 0; i < cleaned.size(); ++i) {
        if (cleaned[i] == ' ' && i + 1 < cleaned.size()) {
            char n = cleaned[i + 1];
            if (n == '.' || n == ',' || n == ';' || n == ':' || n == '!' ||
                n == '?' || n == ')' || n == ']')
                continue; // drop the spurious pre-punctuation space
        }
        out.push_back(cleaned[i]);
    }
    return out;
}

// Cap a decoded string to kResearchSummaryCap bytes on a word boundary, adding
// an ellipsis. (Byte cap, not codepoint — a UTF-8 codepoint is never split
// because we back up to the last space, which is ASCII.)
std::string cap_summary(const std::string& s) {
    if (s.size() <= kResearchSummaryCap) return s;
    std::size_t cut = s.rfind(' ', kResearchSummaryCap);
    if (cut == std::string::npos || cut < kResearchSummaryCap / 2) cut = kResearchSummaryCap;
    std::string out = s.substr(0, cut);
    // trim trailing space before the ellipsis
    while (!out.empty() && out.back() == ' ') out.pop_back();
    out += "\xE2\x80\xA6"; // …
    return out;
}

// Find the next <tag ...>...</tag> at or after `pos` (case-insensitive, tag
// boundary enforced so <title> never matches <titlebar>). On success: sets
// `inner` to the raw inner text, advances `pos` past </tag>, returns true. On
// failure: returns false and leaves `pos`/`inner` untouched. The explicit bool
// distinguishes "not found" from "found but empty" (an empty <p></p> is a real
// hit, not the end of the scan).
bool next_element_text(const std::string& lower, const std::string& src,
                       const std::string& tag, std::size_t& pos, std::string& inner) {
    const std::string open = "<" + tag;
    std::size_t search = pos;
    while (true) {
        std::size_t o = lower.find(open, search);
        if (o == std::string::npos) return false;
        std::size_t after = o + open.size();
        char c = (after < lower.size()) ? lower[after] : '>';
        bool boundary = (c == ' ' || c == '>' || c == '/' || c == '\t' ||
                         c == '\n' || c == '\r');
        if (!boundary) { search = o + open.size(); continue; }  // <pre>, <param>…
        std::size_t gt = src.find('>', o);
        if (gt == std::string::npos) return false;
        const std::string close = "</" + tag;
        std::size_t c2 = lower.find(close, gt + 1);
        if (c2 == std::string::npos) return false;
        inner = src.substr(gt + 1, c2 - gt - 1);
        pos = c2 + close.size();
        return true;
    }
}

// Pull the value of attribute `attr` from a single tag's text (the run between
// '<' and '>'). Handles double, single, and unquoted values. "" if absent.
std::string attr_value(const std::string& tag_lower, const std::string& tag_src,
                       const std::string& attr) {
    std::size_t a = tag_lower.find(attr);
    while (a != std::string::npos) {
        // require the char before to be a boundary (space or tag start) so
        // "content" doesn't match inside another word
        bool lb = (a == 0) || tag_lower[a - 1] == ' ' || tag_lower[a - 1] == '\t' ||
                  tag_lower[a - 1] == '\n' || tag_lower[a - 1] == '\r';
        std::size_t e = a + attr.size();
        // skip spaces, require '='
        std::size_t k = e;
        while (k < tag_lower.size() && (tag_lower[k] == ' ' || tag_lower[k] == '\t')) ++k;
        if (lb && k < tag_lower.size() && tag_lower[k] == '=') {
            ++k;
            while (k < tag_lower.size() && (tag_lower[k] == ' ' || tag_lower[k] == '\t')) ++k;
            if (k >= tag_src.size()) return "";
            char q = tag_src[k];
            if (q == '"' || q == '\'') {
                std::size_t end = tag_src.find(q, k + 1);
                if (end == std::string::npos) return "";
                return tag_src.substr(k + 1, end - k - 1);
            }
            // unquoted: to next space or end
            std::size_t end = k;
            while (end < tag_src.size() && tag_src[end] != ' ' && tag_src[end] != '\t' &&
                   tag_src[end] != '>' && tag_src[end] != '\n' && tag_src[end] != '\r')
                ++end;
            return tag_src.substr(k, end - k);
        }
        a = tag_lower.find(attr, a + attr.size());
    }
    return "";
}

// Extract the description meta (og:description preferred, then name/property
// "description"). Scans every <meta ...> tag.
std::string extract_meta_description(const std::string& lower, const std::string& src) {
    std::string og, plain;
    std::size_t pos = 0;
    while (true) {
        std::size_t m = lower.find("<meta", pos);
        if (m == std::string::npos) break;
        std::size_t gt = src.find('>', m);
        if (gt == std::string::npos) break;
        std::string tag_src   = src.substr(m, gt - m + 1);
        std::string tag_lower = lower.substr(m, gt - m + 1);
        std::string key = attr_value(tag_lower, tag_src, "property");
        if (key.empty()) key = attr_value(tag_lower, tag_src, "name");
        std::string key_low = lower_ascii(key);
        if (key_low == "og:description" && og.empty())
            og = attr_value(tag_lower, tag_src, "content");
        else if (key_low == "description" && plain.empty())
            plain = attr_value(tag_lower, tag_src, "content");
        pos = gt + 1;
    }
    const std::string& chosen = !og.empty() ? og : plain;
    return collapse_ws(decode_entities(chosen));
}

// First <p> whose stripped text is non-trivial (more than a couple of chars).
// Empty / whitespace paragraphs are skipped, not treated as end-of-scan.
std::string extract_first_paragraph(const std::string& lower, const std::string& src) {
    std::size_t pos = 0;
    std::string inner;
    for (int guard = 0; guard < 1000; ++guard) {   // bounded scan
        if (!next_element_text(lower, src, "p", pos, inner)) break; // truly no more
        std::string text = strip_tags(inner);
        if (text.size() >= 3) return text;
        // else empty/trivial <p>; keep scanning
    }
    return "";
}

} // namespace

ResearchMeta research_extract_meta(const std::string& html) {
    ResearchMeta meta;
    const std::string lower = lower_ascii(html);

    // Title — first <title>…</title>. strip_tags already decodes + collapses.
    std::size_t tpos = 0;
    std::string title_inner;
    if (next_element_text(lower, html, "title", tpos, title_inner))
        meta.title = strip_tags(title_inner);

    // Summary: meta description, else first real paragraph.
    std::string desc = extract_meta_description(lower, html);
    if (desc.empty()) desc = extract_first_paragraph(lower, html);
    meta.summary = cap_summary(desc);

    return meta;
}

// ── monolith shell-out (NOT sandbox-runnable) ────────────────────────────────

bool monolith_available() {
    const char* path = std::getenv("PATH");
    if (!path) return false;
    std::string p(path);
    std::size_t start = 0;
    while (start <= p.size()) {
        std::size_t colon = p.find(':', start);
        std::string dir = (colon == std::string::npos) ? p.substr(start)
                                                        : p.substr(start, colon - start);
        if (!dir.empty()) {
            std::string cand = dir + "/monolith";
            std::ifstream f(cand);
            if (f.good()) return true;
        }
        if (colon == std::string::npos) break;
        start = colon + 1;
    }
    return false;
}

namespace {
// Shell-escape a string for single-quote wrapping: ' -> '\''.
std::string sh_single_quote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out.push_back(c);
    }
    out += "'";
    return out;
}
} // namespace

CaptureResult research_capture(const std::string& url, const std::string& out_path) {
    CaptureResult r;
    if (url.empty())      { r.error = "No URL given.";            return r; }
    if (out_path.empty()) { r.error = "No output path given.";    return r; }
    if (!monolith_available()) {
        r.error = "monolith is not installed or not on PATH. Install monolith to "
                  "capture pages (https://github.com/Y2Z/monolith).";
        return r;
    }

    // monolith <url> -o <out>  -- embeds every asset into one self-contained
    // file (default behaviour). Its progress chatter goes to stdout/stderr; we
    // never read it (the capture lands in the -o file), and it floods the host's
    // console, so redirect both streams to /dev/null. Version-proof -- avoids
    // guessing a quiet flag whose name changes across monolith releases.
    // (JS-heavy pages that render client-side need a headless-Chromium pre-pass;
    // that is a later slice. Plain article/doc captures are faithful here.)
    std::string cmd = "monolith -o " + sh_single_quote(out_path) + " " +
                      sh_single_quote(url) + " >/dev/null 2>&1";
    int status = std::system(cmd.c_str());
    // std::system returns a wait-status, not the raw exit code; decode it so the
    // message shows monolith's real code (e.g. 2 = bad usage) not 512.
    int rc = status;
#ifdef WIFEXITED
    if (status != -1 && WIFEXITED(status))
        rc = WEXITSTATUS(status);
#endif
    if (rc != 0) {
        r.error = "monolith failed (exit " + std::to_string(rc) +
                  "). The URL may be unreachable, or the page may need the "
                  "headless-render path (not yet built).";
        return r;
    }

    // Read back the captured file for meta extraction + emptiness check.
    std::ifstream in(out_path, std::ios::binary);
    if (!in.good()) { r.error = "Capture wrote no file at " + out_path + "."; return r; }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string html = ss.str();
    if (html.size() < 64) {
        r.error = "Capture came back empty or too thin (" +
                  std::to_string(html.size()) + " bytes) — the page may render "
                  "with JavaScript (needs the headless path, not yet built).";
        return r;
    }

    r.meta = research_extract_meta(html);
    r.ok = true;
    return r;
}

} // namespace Folio
