// ─────────────────────────────────────────────────────────────────────────────
// Folio — pattern_test.cpp   (s25)   Pure-layer checks for the arc model:
//   KeyPoint.description / KeyPoint.arc, the module pacing pattern → per-scene
//   frenetic (continuous global phase, KP baseline preserved as the centre),
//   per-scene arc, growable Prologue/Epilogue slices, the positioned spectrum,
//   and JSON round-trip of the new fields.
// ─────────────────────────────────────────────────────────────────────────────
/*
Build + run — sandbox (g++):
g++ -std=c++20 -Wall -Wextra -Werror -I include -I /home/claude/sbox tests/pattern_test.cpp src/ModuleIO.cpp src/ModulePlanner.cpp -o /tmp/pattern_test
/tmp/pattern_test

Build + run — Fedora target (clang++):
clang++ -std=c++20 -Wall -Wextra -Werror -I include tests/pattern_test.cpp src/ModuleIO.cpp src/ModulePlanner.cpp -o /tmp/pattern_test
/tmp/pattern_test
*/
#include "Module.hpp"
#include "ModuleIO.hpp"
#include "ModulePlanner.hpp"

#include <cstdio>
#include <cmath>
#include <map>
#include <string>

using namespace Folio;

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const char* what) {
    if (ok) { ++g_pass; }
    else    { ++g_fail; std::printf("  FAIL: %s\n", what); }
}
static bool approx(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }

// Flatten a module's KPs into an id→KeyPoint map.
static std::map<std::string, KeyPoint> kp_map(const Module& m) {
    std::map<std::string, KeyPoint> out;
    for (const auto& a : m.craft.acts)
        for (const auto& k : a.kps) out[k.id] = k;
    return out;
}

int main() {
    Module m = built_in_folio_keypoints();
    auto km = kp_map(m);

    // ── 1. Built-in carries the new model ────────────────────────────────────
    check(km.size() == 14, "built-in has 14 KPs");
    bool all_desc = true, arc_in_range = true;
    for (auto& [id, k] : km) {
        if (k.description.empty()) all_desc = false;
        if (k.arc < 0.0 || k.arc > 1.0) arc_in_range = false;
    }
    check(all_desc, "every KP has a description");
    check(arc_in_range, "every KP arc in [0,1]");
    check(km["kp_climax"].arc == 1.00, "Climax arc is the apex (1.0)");
    check(km["kp_climax"].frenetic > km["kp_deepen"].frenetic, "Climax hotter than Deepening");
    check(km["kp_deepen"].weight  > km["kp_climax"].weight,    "Deepening weightier than Climax");
    check(km["kp_lowpoint"].frenetic < km["kp_lowpoint"].arc,  "Low Point: low pacing, high tension");
    check(m.pacing.levels.size() == 4, "pacing pattern has 4 levels");
    check(approx(m.pacing.levels[0],0.30) && approx(m.pacing.levels[3],1.00),
          "pacing pattern is the Bond cadence (light..action)");

    // ── 2. JSON round-trip preserves the new fields ──────────────────────────
    Module rt = ModuleIO::from_string(ModuleIO::to_string(m));
    auto rkm = kp_map(rt);
    check(rkm["kp_disturb"].description == km["kp_disturb"].description, "round-trip keeps description");
    check(approx(rkm["kp_climax"].arc, 1.00), "round-trip keeps arc");
    check(rt.pacing.levels.size() == 4 && approx(rt.pacing.levels[3], 1.00),
          "round-trip keeps pacing pattern");

    // ── 3. Flat: each body scene sits exactly at its KP frenetic + arc ────────
    {
        PlanInputs in; in.scene_pattern = ScenePattern::Flat; in.target_words = 90000;
        ScaffoldPlan p = ModulePlanner::plan(m, in);
        bool flat_ok = true, arc_ok = true;
        for (auto& part : p.parts) for (auto& ch : part.chapters) for (auto& sc : ch.scenes) {
            const KeyPoint& k = km[sc.kp_id];
            if (!approx(sc.frenetic, k.frenetic)) flat_ok = false;
            if (!approx(sc.arc, k.arc))           arc_ok = false;
        }
        check(flat_ok, "Flat: scene frenetic == KP frenetic");
        check(arc_ok,  "Flat: scene arc == KP arc");
    }

    // ── 4. Pattern: continuous global phase, KP baseline as the centre ────────
    {
        PlanInputs in; in.scene_pattern = ScenePattern::BuildToSpike; in.target_words = 90000;
        ScaffoldPlan p = ModulePlanner::plan(m, in);
        double pmean = (m.pacing.levels[0]+m.pacing.levels[1]+m.pacing.levels[2]+m.pacing.levels[3]) / 4.0;
        int L = (int)m.pacing.levels.size();
        int phase = 0; bool patt_ok = true, arc_ok = true; bool any_diff = false;
        for (auto& part : p.parts) for (auto& ch : part.chapters) for (auto& sc : ch.scenes) {
            const KeyPoint& k = km[sc.kp_id];
            double mult = m.pacing.levels[phase % L] / pmean;
            double expect = std::min(1.0, std::max(0.0, k.frenetic * mult));
            if (!approx(sc.frenetic, expect, 1e-6)) patt_ok = false;
            if (!approx(sc.arc, k.arc))             arc_ok = false;
            if (!approx(sc.frenetic, k.frenetic, 1e-6)) any_diff = true;
            ++phase;
        }
        check(patt_ok,  "Pattern: scene frenetic == KP baseline x (level/mean), phase continuous");
        check(any_diff, "Pattern: scenes actually diverge from the flat baseline");
        check(arc_ok,   "Pattern: scene arc still == KP arc");
    }

    // ── 5. Growable bookends ──────────────────────────────────────────────────
    {
        PlanInputs in; in.prologue = true; in.prologue_scenes = 3;
        in.epilogue = true;  // default 1
        in.target_words = 90000;
        ScaffoldPlan p = ModulePlanner::plan(m, in);
        check(p.prologue.size() == 3, "prologue grows to 3 scenes");
        check(p.epilogue.size() == 1, "epilogue defaults to 1 scene");
        check(p.prologue.front().arc < 0.3, "prologue is low-tension (outside the arc)");

        PlanInputs in2; in2.prologue = true; in2.prologue_scenes = 3; in2.epilogue = true;
        in2.target_words = 90000;
        PlanInputs in0; in0.target_words = 90000;   // no bookends
        ScaffoldPlan p0 = ModulePlanner::plan(m, in0);
        check(p.total_scenes < p0.total_scenes,
              "bookends reserve off-budget (trim body scenes, not inflate)");
    }

    // ── 6. Spectrum: positioned bright primary anchors ────────────────────────
    check(ModuleIO::spectrum_hex(0.0) == "#1a5cff", "spectrum start is bright blue");
    check(ModuleIO::spectrum_hex(1.0) == "#7b2ff0", "spectrum end is purple");
    check(ModuleIO::spectrum_hex(0.38) == "#ffde00", "pure yellow lands at 0.38 (First Trials)");
    {
        auto pal = ModuleIO::keypoint_palette(m);
        check(pal.size() == 14, "palette has one swatch per KP");
        check(pal.front().first == "Opening Disturbance", "palette names == KP labels");
        check(pal.front().second == "#1a5cff", "first KP swatch is the ramp start");
    }

    std::printf("\npattern_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
