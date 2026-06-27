// ─────────────────────────────────────────────────────────────────────────────
// Folio — ModuleIO.cpp   (s23)   GTK-free / GLib-free. See ModuleIO.hpp.
// Also houses the built-in module factories (declared in Module.hpp).
// ─────────────────────────────────────────────────────────────────────────────
#include "ModuleIO.hpp"
#include <cmath>
#include <cstdio>

namespace Folio {
namespace ModuleIO {

// ── struct → json ─────────────────────────────────────────────────────────────
static json kp_to_json(const KeyPoint& k) {
    return json{ {"id", k.id}, {"label", k.label}, {"order", k.order},
                 {"frenetic", k.frenetic}, {"arc", k.arc}, {"weight", k.weight},
                 {"fixed_scenes", k.fixed_scenes}, {"scene_words", k.scene_words},
                 {"color_idx", k.color_idx}, {"description", k.description} };
}
static json act_to_json(const Act& a) {
    json kps = json::array();
    for (const auto& k : a.kps) kps.push_back(kp_to_json(k));
    return json{ {"id", a.id}, {"label", a.label}, {"kps", kps} };
}
static json matter_to_json(const MatterRole& r) {
    return json{ {"role", r.role}, {"rank", r.rank},
                 {"page_side", r.page_side}, {"src", r.src} };
}

json to_json(const Module& m) {
    json acts = json::array();
    for (const auto& a : m.craft.acts) acts.push_back(act_to_json(a));

    json front = json::array();
    for (const auto& r : m.deploy.front) front.push_back(matter_to_json(r));
    json back = json::array();
    for (const auto& r : m.deploy.back) back.push_back(matter_to_json(r));

    json pacing = json::array();
    for (double v : m.pacing.levels) pacing.push_back(v);

    return json{
        {"schema", 2},
        {"id", m.id},
        {"name", m.name},
        {"top", top_container_to_str(m.top)},
        {"craft", { {"kind", m.craft.kind}, {"acts", acts} }},
        {"deploy", { {"kind", m.deploy.kind}, {"front", front}, {"back", back} }},
        {"pacing", pacing},
    };
}

// ── json → struct (tolerant) ──────────────────────────────────────────────────
static KeyPoint kp_from_json(const json& j) {
    KeyPoint k;
    k.id       = j.value("id", "");
    k.label    = j.value("label", "");
    k.order    = j.value("order", 0);
    k.frenetic = j.value("frenetic", 0.0);
    k.arc      = j.value("arc", 0.0);
    k.weight   = j.value("weight", 1.0);
    k.fixed_scenes = j.value("fixed_scenes", 0);
    k.scene_words  = j.value("scene_words", 0);
    k.color_idx    = j.value("color_idx", 0);
    k.description  = j.value("description", "");
    return k;
}
static Act act_from_json(const json& j) {
    Act a;
    a.id    = j.value("id", "");
    a.label = j.value("label", "");
    if (j.contains("kps") && j["kps"].is_array())
        for (const auto& kj : j["kps"]) a.kps.push_back(kp_from_json(kj));
    return a;
}
static MatterRole matter_from_json(const json& j) {
    MatterRole r;
    r.role      = j.value("role", "");
    r.rank      = j.value("rank", 0);
    r.page_side = j.value("page_side", "recto");
    r.src       = j.value("src", "authored");
    return r;
}

Module from_json(const json& j) {
    Module m;
    m.id   = j.value("id", "");
    m.name = j.value("name", "");
    m.top  = top_container_from_str(j.value("top", "part"));

    if (j.contains("craft") && j["craft"].is_object()) {
        const auto& c = j["craft"];
        m.craft.kind = c.value("kind", "");
        if (c.contains("acts") && c["acts"].is_array())
            for (const auto& aj : c["acts"]) m.craft.acts.push_back(act_from_json(aj));
    }
    if (j.contains("deploy") && j["deploy"].is_object()) {
        const auto& d = j["deploy"];
        m.deploy.kind = d.value("kind", "");
        if (d.contains("front") && d["front"].is_array())
            for (const auto& rj : d["front"]) m.deploy.front.push_back(matter_from_json(rj));
        if (d.contains("back") && d["back"].is_array())
            for (const auto& rj : d["back"]) m.deploy.back.push_back(matter_from_json(rj));
    }
    if (j.contains("pacing") && j["pacing"].is_array())
        for (const auto& v : j["pacing"])
            if (v.is_number()) m.pacing.levels.push_back(v.get<double>());
    return m;
}

std::string to_string(const Module& m, bool pretty) {
    return to_json(m).dump(pretty ? 2 : -1);
}
Module from_string(const std::string& text) {
    return from_json(json::parse(text));
}

// ── Spectrum palette ──────────────────────────────────────────────────────────
namespace {
// Bright ramp anchored on the PRIMARIES (blue, yellow, red); secondaries (cyan,
// green, orange, magenta) interpolated, purple as the cool tail. Stops are
// POSITIONED (not evenly spaced) so the green band is compressed and a pure
// yellow lands ~0.38 — the eye reads the primaries as fixed points and the
// climax band burns. (Scott's calibration, s24.) blue→...→purple by arc order.
struct Stop { double p; int r, g, b; };
const Stop k_spectrum[] = {
    {0.00, 0x1A,0x5C,0xFF}, // blue   (primary anchor)
    {0.15, 0x00,0xBB,0xD6}, // cyan
    {0.26, 0x25,0xD9,0x4B}, // green  (compressed band)
    {0.38, 0xFF,0xDE,0x00}, // yellow (primary anchor — pure)
    {0.55, 0xFF,0x8C,0x00}, // orange
    {0.70, 0xFF,0x1E,0x1E}, // red    (primary anchor)
    {0.85, 0xE8,0x1E,0x8C}, // magenta
    {1.00, 0x7B,0x2F,0xF0}, // purple (cool tail)
};
int lerp(int a, int b, double f) { return (int)std::lround(a + (b - a) * f); }
} // namespace

std::string spectrum_hex(double t) {
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    const int n = (int)(sizeof(k_spectrum)/sizeof(k_spectrum[0]));
    int i = 0;
    while (i < n - 1 && t > k_spectrum[i + 1].p) ++i;   // segment by position
    const Stop& a = k_spectrum[i];
    const Stop& b = k_spectrum[(i + 1 < n) ? i + 1 : i];
    double span = b.p - a.p;
    double f = (span > 0.0) ? (t - a.p) / span : 0.0;
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x",
                  lerp(a.r, b.r, f), lerp(a.g, b.g, f), lerp(a.b, b.b, f));
    return std::string(buf);
}

std::vector<KpSwatch> keypoint_palette(const Module& m) {
    std::vector<const KeyPoint*> kps;
    for (const auto& act : m.craft.acts)
        for (const auto& k : act.kps) kps.push_back(&k);
    std::vector<KpSwatch> pal;
    const int n = (int)kps.size();
    for (int i = 0; i < n; ++i) {
        double t = (n <= 1) ? 0.0 : (double)i / (double)(n - 1);
        pal.push_back({kps[i]->id, kps[i]->label, spectrum_hex(t)});
    }
    return pal;
}

} // namespace ModuleIO

// ─────────────────────────────────────────────────────────────────────────────
// Built-in modules
// ─────────────────────────────────────────────────────────────────────────────

// Standard fiction front/back matter (the §5.3 table, fiction subset).
static DeployAnatomy kdp_fiction_deploy() {
    DeployAnatomy d;
    d.kind = "kdp";
    d.front = {
        {"blurb",      0, "any",   "authored"},
        {"half_title", 1, "recto", "generated"},
        {"title",      3, "recto", "generated"},
        {"copyright",  4, "verso", "generated"},
        {"dedication", 5, "recto", "authored"},
        {"epigraph",   6, "recto", "authored"},
    };
    d.back = {
        {"about_author", 10, "recto", "authored"},
        {"also_by",      11, "recto", "authored"},
    };
    return d;
}

Module built_in_three_act() {
    Module m;
    m.id   = "three_act";
    m.name = "Three-act";
    m.top  = TopContainer::Part;
    m.craft.kind = "three_act";
    m.craft.acts = {
        //  id                  label            ord  fren  arc   weight
        { "act1", "Setup",        { {"kp_setup", "Setup", 1, 0.25, 0.30, 4.0} } },
        { "act2", "Confrontation",{ {"kp_confrontation", "Confrontation", 2, 0.60, 0.65, 8.0} } },
        { "act3", "Resolution",   { {"kp_resolution", "Resolution", 3, 0.85, 0.95, 4.0} } },
    };
    m.deploy = kdp_fiction_deploy();
    return m;
}

// Folio's own 12-KP arc. frenetic = the stock-market curve (up, slight pullback,
// up again, apex at Climax, cool release). weight = scene-count share. The two
// are independent (Deepening Stakes: big weight, warm frenetic; Climax: hottest,
// large; Mirror Moment: small, mid).
Module built_in_folio_keypoints() {
    Module m;
    m.id   = "folio_keypoints";
    m.name = "Folio Key Points (14-beat)";
    m.top  = TopContainer::None;        // Parts optional/author-defined, off by default
    m.craft.kind = "folio_keypoints";
    // Calibrated to Scott's four-novel signature (caper, drama, PI thriller,
    // literary): 14 Key Points in 5-4-5. The FIVE hinges (Resistance, both
    // Thresholds, The Reckoning, Final Insight) are a single EXTENDED scene each
    // (~1400w) — pinned to 1 in every tagged book. The nine blocks expand by
    // weight; the long Act-II middle (Deepening Stakes) is the scene-count peak
    // while the Climax stays SHORT and HOT — weight and frenetic diverge by
    // design. Frenetic rises cool→hot to the Climax with a short release tail.
    // color_idx maps the band onto the default 7-swatch palette (stopgap):
    //   sky=7 teal=1 green=4 yellow=2 peach=6 red=3 mauve=5  (cool → hot).
    // Names are Folio's own (propose-and-redline).
    // color_idx = the KP's order (1..14): each KP owns one swatch in the
    // spectrum palette (built by keypoint_palette + installed on apply), so the
    // scene's tag and colour are the same entry and cannot drift. (Scott's
    // design — the palette IS the arc, blue→purple.)
    m.craft.acts = {
        { "act1", "Act I", {
            //  id            label                  ord  fren   arc   weight  fix  words  ci  description
            { "kp_disturb",  "Opening Disturbance",   1, 0.42, 0.20,  2.5,    0,    0,    1, "First crack in the ordinary world" },
            { "kp_setup",    "The Setup",             2, 0.24, 0.28,  4.0,    0,    0,    2, "Stakes and the world established" },
            { "kp_resist",   "Resistance",            3, 0.30, 0.34,  1.0,    1, 1400,    3, "Refusal; the door not yet opened" },
            { "kp_trouble",  "Gathering Trouble",     4, 0.55, 0.44, 10.0,    0,    0,    4, "Pressure mounts toward commitment" },
            { "kp_thresh1",  "First Threshold",       5, 0.62, 0.52,  1.0,    1, 1400,    5, "Crossing into the new arena" },
        }},
        { "act2", "Act II", {
            { "kp_trials",   "First Trials",          6, 0.40, 0.56, 17.0,    0,    0,    6, "Tests, allies, the rules learned" },
            { "kp_reckon",   "The Reckoning",         7, 0.58, 0.64,  1.0,    1, 1400,    7, "Midpoint truth shifts the goal" },
            { "kp_deepen",   "Deepening Stakes",      8, 0.48, 0.70, 24.0,    0,    0,    8, "The long middle; everything tightens" },  // weight peak
            { "kp_thresh2",  "Second Threshold",      9, 0.68, 0.74,  1.0,    1, 1400,    9, "Point of no return" },
        }},
        { "act3", "Act III", {
            { "kp_mounting", "Mounting Forces",      10, 0.74, 0.82,  7.0,    0,    0,   10, "Forces converge for the end" },
            { "kp_lowpoint", "The Low Point",        11, 0.38, 0.66,  3.5,    0,    0,   11, "All seems lost (low pacing, high tension)" },
            { "kp_insight",  "Final Insight",        12, 0.55, 0.86,  1.0,    1, 1400,   12, "The key understanding arrives" },
            { "kp_climax",   "The Climax",           13, 0.96, 1.00,  5.0,    0,    0,   13, "Short, hot, decisive" },  // short + hottest
            { "kp_resolve",  "Resolution",           14, 0.22, 0.40,  2.0,    0,    0,   14, "The new world settles" },  // release tail
        }},
    };
    // s24: the scene-to-scene pacing rhythm — the Bond cadence (light, light,
    // medium, action), cycled across the whole told-line, each scene scaled
    // around its KP's frenetic baseline. The author's signature, saved with the
    // module; redefine freely (Cussler's would run hotter).
    m.pacing.levels = { 0.30, 0.30, 0.62, 1.00 };
    m.deploy = kdp_fiction_deploy();
    return m;
}

} // namespace Folio
