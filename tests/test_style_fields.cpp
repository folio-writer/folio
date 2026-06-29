// ─────────────────────────────────────────────────────────────────────────────
// test_style_fields.cpp — s88 phase 1 (styles)
//
// Proves the pure-layer model + GKeyFile serialization for the three new
// TextStyle fields added this session:
//     space_above_px        (paragraph spacing, px above)
//     space_below_px        (paragraph spacing, px below)
//     first_line_indent_px  (tri-state: -1 inherit global, 0 none, >0 explicit)
//
// Checks:
//   1. Aggregate (positional) initialisers still compile and leave the new
//      trailing fields at their defaults — mirrors FolioPrefs::default_styles().
//   2. Full round-trip (write → read) through a real GKeyFile preserves every
//      field, including the three new ones.
//   3. Backward compatibility: a prefs file written WITHOUT the new keys reads
//      back as space=0/0 and first_line_indent=-1 (inherit), so existing user
//      configs behave exactly as before.
//
// The struct below and the read/write blocks are kept byte-for-byte faithful to
// FolioPrefs.hpp / FolioPrefs.cpp (which can't be included here — they pull in
// glibmm/giomm, absent in the sandbox; the raw C glib API used by the real code
// IS available, so the logic under test is identical).
// ─────────────────────────────────────────────────────────────────────────────
/*
  Sandbox (g++):
    g++ -std=c++20 -Wall -Wextra -Wconversion -Wshadow \
        test/test_style_fields.cpp -o /tmp/test_style_fields \
        $(pkg-config --cflags --libs glib-2.0) && /tmp/test_style_fields

  Fedora (clang):
    clang++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow \
        test/test_style_fields.cpp -o /tmp/test_style_fields \
        $(pkg-config --cflags --libs glib-2.0) && /tmp/test_style_fields
*/
#include <glib.h>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

// ── Faithful copy of Folio::TextStyle (FolioPrefs.hpp), new fields appended ──
struct TextStyle {
    std::string kind          = "paragraph";
    std::string name          = "Unnamed";
    std::string font_family;
    int         font_size     = 0;
    bool        bold          = false;
    bool        italic        = false;
    bool        underline     = false;
    std::string justification;
    std::string fg_color;
    std::string bg_color;
    double      line_height   = 0.0;
    int         space_above_px        = 0;
    int         space_below_px        = 0;
    int         first_line_indent_px  = -1;
};

static const char* GROUP_STYLES = "TextStyles";

// ── Mirror of the read helpers used in FolioPrefs::load ──────────────────────
static int intv(GKeyFile* kf, const char* group, const char* key, int dflt) {
    GError* err = nullptr;
    int v = g_key_file_get_integer(kf, group, key, &err);
    if (err) { g_error_free(err); return dflt; }
    return v;
}
static bool boolv(GKeyFile* kf, const char* group, const char* key, bool dflt) {
    GError* err = nullptr;
    gboolean v = g_key_file_get_boolean(kf, group, key, &err);
    if (err) { g_error_free(err); return dflt; }
    return v;
}

// ── Mirror of FolioPrefs::save (the text-styles loop) ────────────────────────
static void write_styles(GKeyFile* kf, const std::vector<TextStyle>& styles) {
    for (int i = 0; i < (int)styles.size(); ++i) {
        const auto& ts = styles[(size_t)i];
        std::string pfx = "style-" + std::to_string(i) + "-";
        auto sk = [&](const char* k, const std::string& v) {
            g_key_file_set_string(kf, GROUP_STYLES, (pfx+k).c_str(), v.c_str());
        };
        sk("name",          ts.name);
        sk("kind",          ts.kind);
        sk("font-family",   ts.font_family);
        g_key_file_set_integer(kf, GROUP_STYLES, (pfx+"font-size").c_str(),   ts.font_size);
        g_key_file_set_boolean(kf, GROUP_STYLES, (pfx+"bold").c_str(),        ts.bold);
        g_key_file_set_boolean(kf, GROUP_STYLES, (pfx+"italic").c_str(),      ts.italic);
        g_key_file_set_boolean(kf, GROUP_STYLES, (pfx+"underline").c_str(),   ts.underline);
        sk("justification", ts.justification);
        sk("fg-color",      ts.fg_color);
        sk("bg-color",      ts.bg_color);
        g_key_file_set_double(kf, GROUP_STYLES,  (pfx+"line-height").c_str(), ts.line_height);
        g_key_file_set_integer(kf, GROUP_STYLES, (pfx+"space-above").c_str(),       ts.space_above_px);
        g_key_file_set_integer(kf, GROUP_STYLES, (pfx+"space-below").c_str(),       ts.space_below_px);
        g_key_file_set_integer(kf, GROUP_STYLES, (pfx+"first-line-indent").c_str(), ts.first_line_indent_px);
    }
}

// ── Mirror of FolioPrefs::load (the text-styles loop) ────────────────────────
static std::vector<TextStyle> read_styles(GKeyFile* kf) {
    std::vector<TextStyle> out;
    for (int i = 0; ; ++i) {
        std::string pfx = "style-" + std::to_string(i) + "-";
        char* nv = g_key_file_get_string(kf, GROUP_STYLES, (pfx + "name").c_str(), nullptr);
        if (!nv) break;
        TextStyle ts;
        ts.name = nv; g_free(nv);
        auto sv = [&](const char* k) -> std::string {
            char* v = g_key_file_get_string(kf, GROUP_STYLES, (pfx + k).c_str(), nullptr);
            if (!v) return "";
            std::string s(v); g_free(v); return s;
        };
        ts.kind          = sv("kind");  if (ts.kind.empty()) ts.kind = "paragraph";
        ts.font_family   = sv("font-family");
        ts.font_size     = intv(kf, GROUP_STYLES, (pfx + "font-size").c_str(), 0);
        ts.bold          = boolv(kf, GROUP_STYLES, (pfx + "bold").c_str(), false);
        ts.italic        = boolv(kf, GROUP_STYLES, (pfx + "italic").c_str(), false);
        ts.underline     = boolv(kf, GROUP_STYLES, (pfx + "underline").c_str(), false);
        ts.justification = sv("justification");
        ts.fg_color      = sv("fg-color");
        ts.bg_color      = sv("bg-color");
        ts.line_height   = g_key_file_get_double(kf, GROUP_STYLES, (pfx + "line-height").c_str(), nullptr);
        ts.space_above_px       = intv(kf, GROUP_STYLES, (pfx + "space-above").c_str(),       0);
        ts.space_below_px       = intv(kf, GROUP_STYLES, (pfx + "space-below").c_str(),       0);
        ts.first_line_indent_px = intv(kf, GROUP_STYLES, (pfx + "first-line-indent").c_str(), -1);
        out.push_back(ts);
    }
    return out;
}

static int g_checks = 0;
#define CHECK(cond) do { ++g_checks; if(!(cond)){ \
    std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); return 1; } } while(0)

int main() {
    // ── 1. Positional aggregate init still works; trailing fields default ────
    // Mirrors default_styles(): most styles give values only through line_height.
    std::vector<TextStyle> defs;
    defs.push_back({"paragraph", "Body Text",       "JansonText", 12, false,false,false, "left", "","", 1.9});
    defs.push_back({"paragraph", "Body Text First", "JansonText", 12, false,false,false, "left", "","", 1.9, 0,0,0});
    defs.push_back({"character", "Emphasis",        "",            0, false,true, false, "",     "","", 0.0});

    CHECK(defs[0].space_above_px == 0);
    CHECK(defs[0].space_below_px == 0);
    CHECK(defs[0].first_line_indent_px == -1);   // Body Text inherits the global
    CHECK(defs[1].first_line_indent_px == 0);    // Body Text First: explicit none
    CHECK(defs[2].first_line_indent_px == -1);   // character style: irrelevant/inherit

    // ── 2. Full round-trip preserves the new fields ─────────────────────────
    std::vector<TextStyle> in;
    {
        TextStyle a; a.kind="paragraph"; a.name="Verse"; a.font_family="JansonText";
        a.font_size=11; a.italic=true; a.justification="center"; a.line_height=1.4;
        a.space_above_px=18; a.space_below_px=24; a.first_line_indent_px=0;
        in.push_back(a);

        TextStyle b; b.kind="paragraph"; b.name="Body Indented"; b.font_size=12;
        b.space_above_px=0; b.space_below_px=0; b.first_line_indent_px=36;
        in.push_back(b);

        TextStyle c; c.kind="character"; c.name="Code"; c.font_family="Courier New";
        c.font_size=11; // character styles leave spacing/indent at defaults
        in.push_back(c);
    }

    GKeyFile* kf = g_key_file_new();
    write_styles(kf, in);
    std::vector<TextStyle> out = read_styles(kf);
    g_key_file_free(kf);

    CHECK(out.size() == in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        CHECK(out[i].name                == in[i].name);
        CHECK(out[i].kind                == in[i].kind);
        CHECK(out[i].font_family         == in[i].font_family);
        CHECK(out[i].font_size           == in[i].font_size);
        CHECK(out[i].italic              == in[i].italic);
        CHECK(out[i].justification       == in[i].justification);
        CHECK(out[i].space_above_px      == in[i].space_above_px);
        CHECK(out[i].space_below_px      == in[i].space_below_px);
        CHECK(out[i].first_line_indent_px== in[i].first_line_indent_px);
    }
    // Spot the meaningful distinctions survived
    CHECK(out[0].space_above_px == 18 && out[0].space_below_px == 24);
    CHECK(out[0].first_line_indent_px == 0);   // explicit none
    CHECK(out[1].first_line_indent_px == 36);  // explicit value
    CHECK(out[2].first_line_indent_px == -1);  // never set → default inherit

    // ── 3. Backward compat: an OLD prefs file lacks the new keys entirely ────
    GKeyFile* old = g_key_file_new();
    {
        // Write only the pre-s88 keys for one style.
        const char* p = "style-0-";
        auto K = [&](const std::string& k){ return std::string(p)+k; };
        g_key_file_set_string (old, GROUP_STYLES, K("name").c_str(), "Legacy Body");
        g_key_file_set_string (old, GROUP_STYLES, K("kind").c_str(), "paragraph");
        g_key_file_set_string (old, GROUP_STYLES, K("font-family").c_str(), "JansonText");
        g_key_file_set_integer(old, GROUP_STYLES, K("font-size").c_str(), 12);
        g_key_file_set_double (old, GROUP_STYLES, K("line-height").c_str(), 1.9);
        // NOTE: no space-above / space-below / first-line-indent keys
    }
    std::vector<TextStyle> legacy = read_styles(old);
    g_key_file_free(old);

    CHECK(legacy.size() == 1);
    CHECK(legacy[0].name == "Legacy Body");
    CHECK(legacy[0].space_above_px == 0);
    CHECK(legacy[0].space_below_px == 0);
    CHECK(legacy[0].first_line_indent_px == -1); // inherit → unchanged behaviour

    std::printf("ALL %d CHECKS PASSED\n", g_checks);
    return 0;
}
