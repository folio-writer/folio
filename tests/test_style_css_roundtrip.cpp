// ─────────────────────────────────────────────────────────────────────────────
// test_style_css_roundtrip.cpp — s88 phase 2 (styles)
//
// The editor stores paragraph spacing / first-line indent as named tags
// (pa:N, pb:N, fi:N) and the HTML serializer maps them to CSS and back:
//     pa:N  <->  margin-top:Npx
//     pb:N  <->  margin-bottom:Npx
//     fi:N  <->  text-indent:Npx
//
// EditorHtmlSerializer.cpp can't be compiled here (needs gtkmm), so this test
// mirrors the two pure-string pieces verbatim:
//   * the tag-name -> CSS emission (to_html), and
//   * the pv() extractor + the ORDERED deserialize chain (from_html).
//
// What matters most: pv() uses a naive find(), so this verifies each new span
// routes to the CORRECT handler through the *full* chain and is not pre-empted
// by an earlier key (e.g. text-indent vs text-align, margin-top vs margin-left).
// ─────────────────────────────────────────────────────────────────────────────
/*
  Sandbox (g++):
    g++ -std=c++20 -Wall -Wextra -Wconversion -Wshadow \
        test/test_style_css_roundtrip.cpp -o /tmp/test_style_css && /tmp/test_style_css

  Fedora (clang):
    clang++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow \
        test/test_style_css_roundtrip.cpp -o /tmp/test_style_css && /tmp/test_style_css
*/
#include <cstdio>
#include <string>
#include <utility>

// ── Mirror of the to_html tag-name -> CSS span (only the relevant arms) ──────
static std::string to_css(const std::string& tn) {
    auto pre = [&](const char* p){ return tn.size() > 3 && tn.compare(0,3,p)==0; };
    if (pre("lh:")) return "line-height:" + tn.substr(3);
    if (pre("fg:")) return "color:" + tn.substr(3);
    if (pre("bg:")) return "background-color:" + tn.substr(3);
    if (pre("li:")) return "margin-left:" + tn.substr(3) + "px";
    if (pre("ri:")) return "margin-right:" + tn.substr(3) + "px";
    if (pre("pa:")) return "margin-top:" + tn.substr(3) + "px";
    if (pre("pb:")) return "margin-bottom:" + tn.substr(3) + "px";
    if (pre("fi:")) return "text-indent:" + tn.substr(3) + "px";
    return "";
}

// ── Verbatim copy of pv() from EditorHtmlSerializer.cpp ──────────────────────
static std::string pv(const std::string& css, const std::string& prop) {
    size_t pos = css.find(prop);
    if (pos == std::string::npos) return std::string();
    pos += prop.size();
    while (pos < css.size() && (css[pos] == ' ' || css[pos] == '\'' || css[pos] == '"'))
        ++pos;
    std::string val;
    while (pos < css.size() && css[pos] != ';' && css[pos] != '\'' && css[pos] != '"')
        val += css[pos++];
    return val;
}

static std::string strip_px(std::string v) {
    while (!v.empty() && (v.back() == 'x' || v.back() == 'p')) v.pop_back();
    return v;
}

// ── Mirror of the from_html ORDERED chain: returns the (tag,value) produced ──
// Order is identical to the source: text-align, line-height, color,
// background-color, margin-left, margin-right, margin-top, margin-bottom,
// text-indent, then font.
static std::pair<std::string,std::string> classify(const std::string& css) {
    std::string v;
    if (!(v = pv(css, "text-align:")).empty())       return {"align", v};
    if (!(v = pv(css, "line-height:")).empty())      return {"lh", v};
    if (!(v = pv(css, "color:")).empty())            return {"fg", v};        // (naive-find quirk lives here)
    if (!(v = pv(css, "background-color:")).empty()) return {"bg", v};
    if (!(v = pv(css, "margin-left:")).empty())      return {"li", strip_px(v)};
    if (!(v = pv(css, "margin-right:")).empty())     return {"ri", strip_px(v)};
    if (!(v = pv(css, "margin-top:")).empty())       return {"pa", strip_px(v)};
    if (!(v = pv(css, "margin-bottom:")).empty())    return {"pb", strip_px(v)};
    if (!(v = pv(css, "text-indent:")).empty())      return {"fi", strip_px(v)};
    return {"", ""};
}

static int g_checks = 0;
#define CHECK(cond) do { ++g_checks; if(!(cond)){ \
    std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); return 1; } } while(0)

int main() {
    // ── Round-trip each new tag: tag -> CSS -> classify -> (tag,value) ───────
    struct Case { const char* tn; const char* kind; const char* val; };
    const Case cases[] = {
        {"pa:18", "pa", "18"},
        {"pb:24", "pb", "24"},
        {"fi:0",  "fi", "0"},   // explicit "no indent"
        {"fi:36", "fi", "36"},  // explicit indent
        {"li:48", "li", "48"},  // existing keys still route correctly
        {"ri:48", "ri", "48"},
    };
    for (const auto& c : cases) {
        std::string css = to_css(c.tn);
        CHECK(!css.empty());
        auto [kind, val] = classify(css);
        CHECK(kind == c.kind);
        CHECK(val  == c.val);
    }

    // ── Boundary safety: the new spans must NOT be caught by earlier keys ────
    // text-indent must not be mistaken for text-align.
    CHECK(classify("text-indent:32px").first == "fi");
    CHECK(pv("text-indent:32px", "text-align:").empty());
    // margin-top / margin-bottom must not be mistaken for margin-left/right.
    CHECK(classify("margin-top:18px").first == "pa");
    CHECK(classify("margin-bottom:24px").first == "pb");
    CHECK(pv("margin-top:18px", "margin-left:").empty());
    CHECK(pv("margin-bottom:24px", "margin-right:").empty());
    // ...and an existing align span is unaffected by the new text-indent check.
    CHECK(classify("text-align:center").first == "align");
    CHECK(pv("text-align:center", "text-indent:").empty());
    // A line-height span is not snagged by any new key.
    CHECK(classify("line-height:1.90").first == "lh");

    std::printf("ALL %d CHECKS PASSED\n", g_checks);
    return 0;
}
