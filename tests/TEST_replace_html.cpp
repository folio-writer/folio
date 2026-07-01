// ─────────────────────────────────────────────────────────────────────────────
// TEST_replace_html.cpp — validates the replace_html ALGORITHM in isolation.
// The real SearchEngine header transitively pulls giomm (via DocumentModel), so it
// can't link standalone in the sandbox; this harness mirrors replace_html and
// build_regex EXACTLY (keep in sync by hand) to prove the logic that fixes the
// "extra empty return" + "lost style" search/replace regressions. GTK-free.
// Build: g++ -std=c++20 -Wall -Wextra -Wconversion -Wshadow TEST_replace_html.cpp -o /tmp/trh && /tmp/trh
// ─────────────────────────────────────────────────────────────────────────────
#include <iostream>
#include <regex>
#include <string>

struct Opts { bool case_sensitive = false; bool whole_word = false; bool use_regex = false; };
struct Result { int replacements = 0; std::string new_html; std::string error; };

static std::string regex_escape(const std::string& s) {
    static const std::string special = R"(\.+*?^${}()|[]/)";
    std::string out; out.reserve(s.size() * 2);
    for (char c : s) { if (special.find(c) != std::string::npos) out += '\\'; out += c; }
    return out;
}
static std::regex build_regex(const std::string& query, const Opts& opts) {
    auto flags = std::regex_constants::ECMAScript;
    if (!opts.case_sensitive) flags |= std::regex_constants::icase;
    std::string pattern = opts.use_regex ? query : regex_escape(query);
    if (opts.whole_word) pattern = "\\b" + pattern + "\\b";
    return std::regex(pattern, flags);
}
static Result replace_html(const std::string& html, const std::string& query,
                           const std::string& replacement, const Opts& opts) {
    Result r;
    if (query.empty()) return r;
    std::regex re;
    try { re = build_regex(query, opts); }
    catch (const std::regex_error& e) { r.error = e.what(); return r; }
    auto html_esc = [](const std::string& s) {
        std::string out; out.reserve(s.size());
        for (unsigned char c : s) {
            if      (c == '&') out += "&amp;";
            else if (c == '<') out += "&lt;";
            else if (c == '>') out += "&gt;";
            else               out += static_cast<char>(c);
        }
        return out;
    };
    auto decode_basic = [](const std::string& s) {
        std::string out; out.reserve(s.size());
        for (std::size_t k = 0; k < s.size();) {
            if (s[k] == '&') {
                if      (s.compare(k, 5, "&amp;")  == 0) { out += '&';  k += 5; continue; }
                else if (s.compare(k, 4, "&lt;")   == 0) { out += '<';  k += 4; continue; }
                else if (s.compare(k, 4, "&gt;")   == 0) { out += '>';  k += 4; continue; }
                else if (s.compare(k, 6, "&quot;") == 0) { out += '"';  k += 6; continue; }
                else if (s.compare(k, 5, "&#39;")  == 0) { out += '\''; k += 5; continue; }
            }
            out += s[k++];
        }
        return out;
    };
    std::string out; out.reserve(html.size());
    std::string run;
    auto flush_run = [&]() {
        if (run.empty()) return;
        const std::string decoded = decode_basic(run);
        int reps = 0;
        try {
            reps = static_cast<int>(std::distance(
                std::sregex_iterator(decoded.begin(), decoded.end(), re),
                std::sregex_iterator()));
        } catch (...) { reps = 0; }
        if (reps > 0) {
            std::string replaced;
            try { replaced = std::regex_replace(decoded, re, replacement); }
            catch (...) { out += run; run.clear(); return; }
            out += html_esc(replaced);
            r.replacements += reps;
        } else {
            out += run;
        }
        run.clear();
    };
    for (std::size_t i = 0; i < html.size();) {
        if (html[i] == '<') {
            flush_run();
            const std::size_t et = html.find('>', i);
            if (et == std::string::npos) { out += html.substr(i); break; }
            out += html.substr(i, et - i + 1);
            i = et + 1;
        } else {
            run += html[i++];
        }
    }
    flush_run();
    if (r.replacements > 0) r.new_html = out;
    return r;
}

static int g_pass = 0, g_fail = 0;
static void check(bool cond, const std::string& what, const std::string& got = "") {
    if (cond) { ++g_pass; }
    else { ++g_fail; std::cout << "  FAIL: " << what;
           if (!got.empty()) std::cout << "\n         got: [" << got << "]";
           std::cout << "\n"; }
}
int main() {
    std::cout << "\n=== TEST_replace_html (algorithm mirror) ===\n\n";
    Opts o;
    { auto r = replace_html("<p>Jasper went to work.</p>", "Jasper", "Basper", o);
      check(r.replacements == 1, "single replacement counted");
      check(r.new_html == "<p>Basper went to work.</p>", "no stray newline", r.new_html);
      check(r.new_html.find('\n') == std::string::npos, "no newline at all", r.new_html); }
    { auto r = replace_html("<p>The <b>quick</b> brown Jasper.</p>", "Jasper", "Basper", o);
      check(r.new_html == "<p>The <b>quick</b> brown Basper.</p>", "inline <b> preserved", r.new_html); }
    { auto r = replace_html("<p data-ol=\"1\">Chapter Jasper</p>", "Jasper", "Basper", o);
      check(r.new_html == "<p data-ol=\"1\">Chapter Basper</p>", "para attributes preserved", r.new_html); }
    { const std::string in = "<p>Jasper one.</p>\n<p>No match here.</p>\n<p>Jasper three.</p>";
      auto r = replace_html(in, "Jasper", "Basper", o);
      check(r.replacements == 2, "two matches", std::to_string(r.replacements));
      check(r.new_html == "<p>Basper one.</p>\n<p>No match here.</p>\n<p>Basper three.</p>",
            "newlines + unmatched para intact", r.new_html); }
    { auto r = replace_html("<p>apple</p>", "p", "X", o);
      check(r.new_html == "<p>aXXle</p>", "tags untouched; only text replaced", r.new_html); }
    { auto r = replace_html("<p>Tom &amp; Jasper</p>", "Jasper", "Basper", o);
      check(r.new_html == "<p>Tom &amp; Basper</p>", "ampersand entity preserved", r.new_html); }
    { auto r = replace_html("<p>nothing here</p>", "zzz", "q", o);
      check(r.replacements == 0 && r.new_html.empty(), "no match -> empty new_html"); }
    std::cout << "\n──────────────────────────────\n";
    std::cout << "passed: " << g_pass << "   failed: " << g_fail << "\n";
    return g_fail == 0 ? 0 : 1;
}
