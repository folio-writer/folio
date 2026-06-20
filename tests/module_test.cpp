// ─────────────────────────────────────────────────────────────────────────────
// Folio — tests/module_test.cpp   (s23)
// Pure layer (no GTK/glibmm). Verifies: (1) ModuleIO json round-trip is lossless
// for both built-ins; (2) the planner sums exactly, honours floors, stamps KP
// tag colours, and reserves prologue/epilogue off-budget; (3) prints the worked
// plan in Flat and BuildToSpike modes for eyeball.
//
// BUILD & RUN
//   sandbox (nlohmann vendored at /home/claude/sbox):
//     g++ -std=c++20 -Wall -Wextra -Werror -Iinclude -I/home/claude/sbox tests/module_test.cpp src/ModuleIO.cpp src/ModulePlanner.cpp -o /tmp/mtest && /tmp/mtest
//   fedora (nlohmann system-installed, clang):
//     clang++ -std=c++20 -Wall -Wextra -Werror -Iinclude tests/module_test.cpp src/ModuleIO.cpp src/ModulePlanner.cpp -o module_test && ./module_test
// ─────────────────────────────────────────────────────────────────────────────
#include "Module.hpp"
#include "ModuleIO.hpp"
#include "ModulePlanner.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <set>
#include <cstdlib>
#include <numeric>
#include <string>

using namespace Folio;

static int g_checks = 0, g_fail = 0;
#define CHECK(c) do { ++g_checks; if(!(c)){ ++g_fail; \
    std::printf("  FAIL: %s  (line %d)\n", #c, __LINE__);} } while(0)

// Deep-ish equality for round-trip verification.
static bool kp_eq(const KeyPoint& a, const KeyPoint& b) {
    return a.id==b.id && a.label==b.label && a.order==b.order &&
           std::fabs(a.frenetic-b.frenetic)<1e-9 && std::fabs(a.weight-b.weight)<1e-9 &&
           a.fixed_scenes==b.fixed_scenes && a.scene_words==b.scene_words;
}
static bool module_eq(const Module& a, const Module& b) {
    if (a.id!=b.id || a.name!=b.name || a.top!=b.top) return false;
    if (a.craft.kind!=b.craft.kind || a.craft.acts.size()!=b.craft.acts.size()) return false;
    for (size_t i=0;i<a.craft.acts.size();++i){
        const auto&x=a.craft.acts[i]; const auto&y=b.craft.acts[i];
        if (x.id!=y.id||x.label!=y.label||x.kps.size()!=y.kps.size()) return false;
        for (size_t k=0;k<x.kps.size();++k) if(!kp_eq(x.kps[k],y.kps[k])) return false;
    }
    if (a.deploy.front.size()!=b.deploy.front.size()) return false;
    if (a.deploy.back.size()!=b.deploy.back.size()) return false;
    return true;
}

static void test_roundtrip(const Module& m, const char* tag) {
    std::printf("[roundtrip] %s\n", tag);
    std::string s = ModuleIO::to_string(m, true);
    Module back = ModuleIO::from_string(s);
    CHECK(module_eq(m, back));
    // re-serialise: text stable
    CHECK(ModuleIO::to_string(back, true) == s);
}

static int plan_scene_total(const ScaffoldPlan& p) {
    int n=0; for (auto& part:p.parts) n += part.scene_count(); return n;
}

static void bar(double f) {            // 0..1 → 20-cell bar
    int n = (int)std::lround(f*20.0);
    for (int i=0;i<20;++i) std::putchar(i<n?'#':'.');
}

static void print_plan(const Module& m, const ScaffoldPlan& p, const char* tag) {
    std::printf("\n===== %s  (%s) =====\n", m.name.c_str(), tag);
    std::printf("total scenes: %d   parts: %zu\n", p.total_scenes, p.parts.size());

    // Front matter
    std::printf("FRONT: ");
    for (auto& r:p.front) std::printf("%s(%d,%s,%s) ", r.role.c_str(), r.rank,
                                       r.page_side.c_str(), r.src.c_str());
    std::printf("\n");

    for (auto& part:p.parts) {
        std::printf("\n%s  [%d scenes, %zu ch]\n",
            part.label.empty()? "(no part container)" : part.label.c_str(),
            part.scene_count(), part.chapters.size());
        // KP roll-up within this part (count + sample frenetic curve)
        // Walk scenes, group by kp.
        std::string cur; int cnt=0; double fsum=0; double fmin=9, fmax=-9; int sw=0;
        auto flush=[&](){
            if(cnt){ std::printf("    %-18s %2d sc  avg ", cur.c_str(), cnt);
                     bar(fsum/cnt);
                     std::printf("  [%.2f..%.2f]  @%dw\n", fmin, fmax, sw); }
        };
        for (auto& ch:part.chapters) for (auto& s:ch.scenes){
            if(s.kp_label!=cur){ flush(); cur=s.kp_label; cnt=0; fsum=0; fmin=9; fmax=-9; }
            ++cnt; fsum+=s.frenetic; fmin=std::min(fmin,s.frenetic); fmax=std::max(fmax,s.frenetic);
            sw=s.target_words;
        }
        flush();
    }
    std::printf("BACK: ");
    for (auto& r:p.back) std::printf("%s(%d) ", r.role.c_str(), r.rank);
    std::printf("\n");
}

static void test_plan_invariants(const Module& m) {
    PlanInputs in; in.target_words=90000; in.avg_scene_words=1000; in.chapters=30;
    ScaffoldPlan p = ModulePlanner::plan(m, in);
    // total scenes placed == reported total (none lost)
    CHECK(plan_scene_total(p) == p.total_scenes);
    // word budget: Σ scene target_words within one average scene of target
    long words=0; for(auto&part:p.parts) for(auto&ch:part.chapters) for(auto&s:ch.scenes)
        words += s.target_words;
    CHECK(std::labs(words - 90000) <= in.avg_scene_words);
    // chapters sum to request
    int chs=0; for(auto&part:p.parts) chs+=(int)part.chapters.size();
    CHECK(chs == 30);
    // each KP has ≥1 scene; fixed KPs have EXACTLY their fixed count + size
    for (auto& a:m.craft.acts) for (auto& k:a.kps){
        int c=0; int sw_seen=0;
        for(auto&part:p.parts) for(auto&ch:part.chapters) for(auto&s:ch.scenes)
            if(s.kp_id==k.id){ ++c; sw_seen=s.target_words; }
        CHECK(c>=1);
        if (k.fixed_scenes>0)  CHECK(c==k.fixed_scenes);
        if (k.scene_words>0)   CHECK(sw_seen==k.scene_words);
    }
    // scene indices are 1..N contiguous
    int expect=1; bool seq=true;
    for(auto&part:p.parts) for(auto&ch:part.chapters) for(auto&s:ch.scenes)
        if(s.index!=expect++) seq=false;
    CHECK(seq);
    // s23: every chapter scene carries its KP's tag colour (cool→hot band).
    {
        bool colored=true;
        for (auto& a:m.craft.acts) for (auto& k:a.kps) {
            for (auto& part:p.parts) for (auto& ch:part.chapters) for (auto& s:ch.scenes)
                if (s.kp_id==k.id && s.color_idx!=k.color_idx) colored=false;
        }
        CHECK(colored);
    }
}

static void test_bookends(const Module& m) {
    PlanInputs in; in.target_words=90000; in.avg_scene_words=1130;
    in.prologue=true; in.epilogue=true;
    ScaffoldPlan p = ModulePlanner::plan(m, in);
    CHECK(p.prologue.size()==1);
    CHECK(p.epilogue.size()==1);
    CHECK(p.prologue[0].color_idx==1);   // blue (ramp start)
    CHECK(p.epilogue[0].color_idx==14);  // purple (ramp end)
    // bookends are reserved off the budget: enabling them trims chapter scenes.
    PlanInputs bare = in; bare.prologue=false; bare.epilogue=false;
    CHECK(ModulePlanner::plan(m, bare).total_scenes >= p.total_scenes);
}

static void test_palette(const Module& m) {
    auto pal = ModuleIO::keypoint_palette(m);
    // one swatch per KP, named after it, in arc order — tag and colour fused.
    int nkp = 0; for (auto& a : m.craft.acts) nkp += (int)a.kps.size();
    CHECK((int)pal.size() == nkp);
    int i = 0;
    for (auto& a : m.craft.acts) for (auto& k : a.kps) {
        CHECK(pal[i].first == k.label);                 // name == KP
        CHECK(pal[i].second.size() == 7 && pal[i].second[0] == '#'); // #rrggbb
        CHECK(k.color_idx == i + 1);                    // color_idx lands on swatch
        ++i;
    }
    // spectrum runs blue→purple and every slice is distinct.
    CHECK(pal.front().second != pal.back().second);
    CHECK(ModuleIO::spectrum_hex(0.0) == "#2563eb");    // blue anchor
    CHECK(ModuleIO::spectrum_hex(1.0) == "#7c3aed");    // purple anchor
    std::set<std::string> uniq;
    for (auto& e : pal) uniq.insert(e.second);
    CHECK((int)uniq.size() == nkp);                     // no collisions
}

static void test_pattern_mean_preserved(const Module& m) {
    // BuildToSpike redistributes WITHIN a cluster around the KP frenetic. The
    // true invariant: it never RAISES the mean above the KP level, and where
    // there is headroom (frenetic <= 0.85) it preserves the mean tightly. At the
    // ceiling (frenetic ~1.0) spikes can't exceed 1.0 to offset the builds, so
    // the mean necessarily dips a little — expected, bounded, not a defect.
    PlanInputs in; in.target_words=90000; in.avg_scene_words=1000; in.chapters=30;
    in.scene_pattern = ScenePattern::BuildToSpike;
    ScaffoldPlan p = ModulePlanner::plan(m, in);
    for (auto& a:m.craft.acts) for (auto& k:a.kps){
        double sum=0; int c=0;
        for(auto&part:p.parts) for(auto&ch:part.chapters) for(auto&s:ch.scenes)
            if(s.kp_id==k.id){ sum+=s.frenetic; ++c; }
        if(c>0){ double mean=sum/c;
            CHECK(mean <= k.frenetic + 1e-6);              // never raises
            if (k.frenetic <= 0.85) CHECK(std::fabs(mean-k.frenetic) < 0.04);
            else                    CHECK(mean >= k.frenetic - 0.15); // bounded dip
        }
    }
}

// Parts are author-defined and decoupled from acts: N parts with thematic
// titles own CONTIGUOUS chapter ranges, chapters number continuously across
// them, and the scene total is unchanged from the no-parts plan.
static void test_part_specs(const Module& m) {
    PlanInputs base; base.target_words=110000; base.avg_scene_words=1130;
    int total_no_parts = ModulePlanner::plan(m, base).total_scenes;

    PlanInputs in = base;
    in.top = TopContainer::Part;
    in.part_specs = { {"On the Rebound",12}, {"Right Is Wrong",12},
                      {"Live and Learn",10}, {"In for a Penny",6} };
    ScaffoldPlan p = ModulePlanner::plan(m, in);

    CHECK(p.total_scenes == total_no_parts);          // parts don't change scene math
    CHECK(p.parts.size() == 4);                       // four author parts
    CHECK(p.parts[0].label == "On the Rebound");      // thematic titles, not "Part I · Act I"
    int chs=0, last=0; bool continuous=true;
    for (auto& part : p.parts)
        for (auto& ch : part.chapters) {
            ++chs;
            // chapter numbers run 1,2,3… continuously across parts
            int num = std::atoi(ch.label.c_str() + 8 /*"Chapter "*/);
            if (num != last + 1) continuous=false;
            last = num;
        }
    CHECK(chs == 40);                                 // 12+12+10+6
    CHECK(continuous);                                // numbering crosses part lines
}

int main() {
    Module ta = built_in_three_act();
    Module fk = built_in_folio_keypoints();

    test_roundtrip(ta, "three_act");
    test_roundtrip(fk, "folio_keypoints");
    test_plan_invariants(ta);
    test_plan_invariants(fk);
    test_pattern_mean_preserved(fk);
    test_part_specs(fk);
    test_bookends(fk);
    test_palette(fk);

    // Worked plans for eyeball — at Scott's signature scene size.
    PlanInputs flat;  flat.target_words=90000; flat.avg_scene_words=1130; flat.chapters=16;
    print_plan(fk, ModulePlanner::plan(fk, flat), "Flat — chapters at root (top=None default)");

    PlanInputs spike = flat; spike.scene_pattern = ScenePattern::BuildToSpike; spike.spike_cycle=3;
    print_plan(fk, ModulePlanner::plan(fk, spike), "BuildToSpike — per-scene guide, KP mean preserved");

    // Author-defined Parts (decoupled from acts): four thematic parts.
    PlanInputs parts; parts.target_words=110000; parts.avg_scene_words=1130;
    parts.top = TopContainer::Part;
    parts.part_specs = { {"On the Rebound",12}, {"Right Is Wrong",12},
                         {"Live and Learn",10}, {"In for a Penny",6} };
    print_plan(fk, ModulePlanner::plan(fk, parts), "Four author Parts — continuous chapters");

    std::printf("\n----- %d checks, %d failed -----\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
