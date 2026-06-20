// ─────────────────────────────────────────────────────────────────────────────
// Folio — ModulePlanner.cpp   (s23)   Pure. GTK-free. See ModulePlanner.hpp.
// ─────────────────────────────────────────────────────────────────────────────
#include "ModulePlanner.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <string>

namespace Folio {
namespace ModulePlanner {
namespace {

// Largest-remainder apportionment: split `total` across `shares` (relative
// weights) so the parts sum EXACTLY to total, each part ≥ `floor_each`.
// Deterministic: ties broken by lower index. Returns one count per share.
std::vector<int> apportion(const std::vector<double>& shares, int total, int floor_each) {
    const int n = (int)shares.size();
    std::vector<int> out(n, 0);
    if (n == 0) return out;

    // Reserve the floor first; apportion the remainder by weight.
    int reserved = floor_each * n;
    if (total <= reserved) {
        // Not enough to exceed the floor everywhere — give everyone the floor
        // (the plan may then exceed `total`; callers size total ≥ n so this is
        // the degenerate small-book guard, not the normal path).
        std::fill(out.begin(), out.end(), floor_each);
        return out;
    }
    int rem = total - reserved;

    double sum = std::accumulate(shares.begin(), shares.end(), 0.0);
    if (sum <= 0.0) sum = (double)n;  // equal split if all weights zero

    std::vector<double> exact(n);
    std::vector<int>    base(n);
    for (int i = 0; i < n; ++i) {
        double w = (sum == (double)n && shares[i] == 0.0) ? 1.0 : shares[i];
        exact[i] = (double)rem * w / sum;
        base[i]  = (int)std::floor(exact[i]);
    }
    int assigned = std::accumulate(base.begin(), base.end(), 0);
    int leftover = rem - assigned;

    // Hand out the leftover to the largest fractional remainders.
    std::vector<int> idx(n);
    std::iota(idx.begin(), idx.end(), 0);
    std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) {
        return (exact[a] - base[a]) > (exact[b] - base[b]);
    });
    for (int i = 0; i < leftover; ++i) base[idx[i]] += 1;

    for (int i = 0; i < n; ++i) out[i] = base[i] + floor_each;
    return out;
}

// Chunk `count` contiguous items into `bins` near-equal contiguous sizes.
std::vector<int> chunk_evenly(int count, int bins) {
    std::vector<int> sizes;
    if (bins <= 0) { sizes.push_back(count); return sizes; }
    int base = count / bins, rem = count % bins;
    for (int i = 0; i < bins; ++i) sizes.push_back(base + (i < rem ? 1 : 0));
    return sizes;
}

// Pin-walled chunker (s29). The author's craft truth: a chapter ends on a pinned
// Key Point (a hinge — a crossing, a revelation; the page-turn beat). So pins are
// HARD chapter boundaries, not even-split artifacts. `walls` holds scene-run
// positions where a chapter MUST end (right after a pinned KP's scene). Between
// walls, each segment subdivides into ~even chapters sized toward the rough
// scenes-per-chapter (= count / target_total), round-to-nearest, min 1. Chapter
// count is therefore EMERGENT: the pins win and the count floats toward target.
// Two pins back-to-back yield a legitimately short chapter — preserved, not
// merged (the deliberate gut-punch chapter the design protects).
std::vector<int> chunk_pinned(int count, int target_total,
                              const std::vector<int>& walls) {
    std::vector<int> sizes;
    if (count <= 0) return sizes;
    target_total = std::max(1, target_total);
    const double rough = (double)count / (double)target_total;   // scenes/chapter

    // Segment cut points: 0, the internal walls (unique, in-range), count.
    std::vector<int> cuts;
    cuts.push_back(0);
    for (int w : walls) if (w > 0 && w < count) cuts.push_back(w);
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
    cuts.push_back(count);

    for (size_t i = 0; i + 1 < cuts.size(); ++i) {
        const int seg = cuts[i + 1] - cuts[i];
        if (seg <= 0) continue;
        int nchap = (rough > 0.0)
            ? (int)std::lround((double)seg / rough) : 1;
        nchap = std::clamp(nchap, 1, seg);          // never more chapters than scenes
        for (int s : chunk_evenly(seg, nchap)) sizes.push_back(s);
    }
    return sizes;
}


std::string roman(int n) {
    static const std::pair<int, const char*> tbl[] = {
        {1000,"M"},{900,"CM"},{500,"D"},{400,"CD"},{100,"C"},{90,"XC"},
        {50,"L"},{40,"XL"},{10,"X"},{9,"IX"},{5,"V"},{4,"IV"},{1,"I"}};
    std::string s;
    for (auto& [v, sym] : tbl) while (n >= v) { s += sym; n -= v; }
    return s;
}

} // namespace

ScaffoldPlan plan(const Module& m, const PlanInputs& in) {
    ScaffoldPlan out;
    out.front = m.deploy.front;
    out.back  = m.deploy.back;

    // Flatten the arc to an ordered KP list. Acts contribute ORDER only — they
    // are the colour band, never a structural container (four novels: chapters
    // and parts both cross act lines freely).
    std::vector<const KeyPoint*> kps;
    for (const auto& act : m.craft.acts)
        for (const auto& k : act.kps) kps.push_back(&k);
    const int nkp = (int)kps.size();
    if (nkp == 0) return out;

    // 1. scene allocation per KP — word-budget aware.
    //    Fixed KPs (e.g. the extended epilogue) are reserved off the top: their
    //    word cost comes out of the budget first, so an extended scene reduces
    //    the count of normal scenes rather than inflating the book. The rest of
    //    the budget distributes across the weight-driven KPs at the book average.
    const int avg = std::max(1, in.avg_scene_words);
    std::vector<int> kp_scenes(nkp, 0);

    long fixed_words = 0;
    // Optional cool bookends (Prologue / Epilogue) are reserved off the budget
    // like fixed scenes, so they trim normal scenes rather than inflate the book.
    // They sit OUTSIDE the chapters (their own root leaves). s24: each is a
    // growable SLICE of N scenes (a Cussler-style cold-open novella), not one.
    const int pro_n = in.prologue ? std::max(1, in.prologue_scenes) : 0;
    const int epi_n = in.epilogue ? std::max(1, in.epilogue_scenes) : 0;
    fixed_words += (long)(pro_n + epi_n) * avg;
    std::vector<int> weighted_idx;
    std::vector<double> weighted_w;
    for (int i = 0; i < nkp; ++i) {
        if (kps[i]->fixed_scenes > 0) {
            kp_scenes[i] = kps[i]->fixed_scenes;
            int sw = kps[i]->scene_words > 0 ? kps[i]->scene_words : avg;
            fixed_words += (long)kp_scenes[i] * sw;
        } else {
            weighted_idx.push_back(i);
            weighted_w.push_back(std::max(0.0, kps[i]->weight));
        }
    }

    int total = 0;
    for (int i = 0; i < nkp; ++i) total += kp_scenes[i];   // fixed so far

    if (!weighted_idx.empty()) {
        long remaining_words = std::max(0L, (long)in.target_words - fixed_words);
        int  rem_scenes = (int)std::lround((double)remaining_words / (double)avg);
        rem_scenes = std::max(rem_scenes, (int)weighted_idx.size()); // each ≥1
        std::vector<int> alloc = apportion(weighted_w, rem_scenes, /*floor*/1);
        for (size_t wi = 0; wi < weighted_idx.size(); ++wi) {
            kp_scenes[weighted_idx[wi]] = alloc[wi];
            total += alloc[wi];
        }
    }
    out.total_scenes = total;

    // 2b. Optional cool bookends — N scenes each, low frenetic + low arc, own
    //     tag colour (ramp endpoints). Outside the arc and outside the chapters.
    for (int s = 0; s < pro_n; ++s) {
        PlanScene p; p.kp_id = "kp_prologue"; p.kp_label = "Prologue";
        p.frenetic = 0.10; p.arc = 0.15; p.target_words = avg; p.color_idx = 1;  // blue (ramp start)
        out.prologue.push_back(p);
    }
    for (int s = 0; s < epi_n; ++s) {
        PlanScene e; e.kp_id = "kp_epilogue"; e.kp_label = "Epilogue";
        e.frenetic = 0.20; e.arc = 0.30; e.target_words = avg; e.color_idx = nkp; // purple (ramp end)
        out.epilogue.push_back(e);
    }

    // 3. Build the full ordered scene run across ALL Key Points. Acts play no
    //    structural role here — chapters chunk this one flat told-line. Per-scene
    //    pacing comes from the module's pacing pattern, cycled CONTINUOUSLY across
    //    the whole run (it crosses KP edges — pacing is not a KP property), each
    //    scene scaled around its KP's frenetic baseline so the KP fader stays the
    //    cluster's centre. Flat (or an empty/degenerate pattern) leaves scenes at
    //    the KP baseline. Each scene also carries its KP's arc (tension) value.
    const std::vector<double>& levels = m.pacing.levels;
    bool use_pattern = (in.scene_pattern == ScenePattern::BuildToSpike)
                       && levels.size() > 1;
    double pmean = 0.0;
    if (use_pattern) {
        pmean = std::accumulate(levels.begin(), levels.end(), 0.0)
                / (double)levels.size();
        if (pmean <= 0.0) use_pattern = false;
    }
    int scene_no = 0, phase = 0;
    std::vector<PlanScene> run;
    for (int kpi = 0; kpi < nkp; ++kpi) {
        int    n    = kp_scenes[kpi];
        double base = kps[kpi]->frenetic;
        double a    = kps[kpi]->arc;
        int    sw   = kps[kpi]->scene_words > 0 ? kps[kpi]->scene_words : avg;
        for (int s = 0; s < n; ++s) {
            double mult = use_pattern
                ? (levels[phase % (int)levels.size()] / pmean) : 1.0;
            PlanScene ps;
            ps.index        = ++scene_no;
            ps.kp_id        = kps[kpi]->id;
            ps.kp_label     = kps[kpi]->label;
            ps.frenetic     = std::clamp(base * mult, 0.0, 1.0);
            ps.arc          = a;
            ps.target_words = sw;
            ps.color_idx    = kps[kpi]->color_idx;
            ps.pin          = (kps[kpi]->fixed_scenes > 0);  // hinge = milestone
            run.push_back(ps);
            ++phase;
        }
    }

    // 4. Chunk the flat run into chapters, numbered CONTINUOUSLY 1..N. Author
    //    part specs (when present) set the chapter total; otherwise in.chapters.
    const bool use_parts =
        in.top != TopContainer::None && !in.part_specs.empty();
    int total_chapters = 0;
    if (use_parts)
        for (const auto& sp : in.part_specs) total_chapters += std::max(1, sp.chapters);
    else
        total_chapters = std::max(1, in.chapters);
    total_chapters = std::clamp(total_chapters, 1, std::max(1, (int)run.size()));

    std::vector<PlanChapter> chapters;
    {
        // Pin walls: a chapter ends right after each pinned KP's scene block.
        // cum tracks the scene-run position after each KP; a pinned KP that isn't
        // the final block plants a hard boundary there.
        std::vector<int> walls;
        int cum = 0;
        for (int kpi = 0; kpi < nkp; ++kpi) {
            cum += kp_scenes[kpi];
            if (kps[kpi]->fixed_scenes > 0 && cum < (int)run.size())
                walls.push_back(cum);
        }
        std::vector<int> sizes =
            chunk_pinned((int)run.size(), total_chapters, walls);
        int cursor = 0, chapter_no = 0;
        for (int c = 0; c < (int)sizes.size(); ++c) {
            PlanChapter ch;
            ch.label = "Chapter " + std::to_string(++chapter_no);
            for (int s = 0; s < sizes[c] && cursor < (int)run.size(); ++s)
                ch.scenes.push_back(run[cursor++]);
            if (!ch.scenes.empty()) chapters.push_back(std::move(ch));
        }
    }

    // 5. Group chapters into Parts/Books. Parts are optional, author-defined
    //    contiguous chapter ranges with their own titles; numbering stays
    //    continuous across parts. No specs (or top==None) ⇒ one unlabelled part
    //    (chapters live at the manuscript root).
    if (use_parts) {
        const char* word = (in.top == TopContainer::Book) ? "Book " : "Part ";
        int ci = 0;
        for (int pi = 0; pi < (int)in.part_specs.size(); ++pi) {
            const auto& sp = in.part_specs[pi];
            PlanPart part;
            part.label = sp.title.empty() ? (std::string(word) + roman(pi + 1)) : sp.title;
            int want = std::max(1, sp.chapters);
            for (int k = 0; k < want && ci < (int)chapters.size(); ++k)
                part.chapters.push_back(std::move(chapters[ci++]));
            if (!part.chapters.empty()) out.parts.push_back(std::move(part));
        }
        // Rounding remainder (if run was too short for the spec total) → last part.
        while (ci < (int)chapters.size()) {
            if (out.parts.empty()) out.parts.push_back(PlanPart{});
            out.parts.back().chapters.push_back(std::move(chapters[ci++]));
        }
    } else {
        PlanPart part;                  // one synthetic, unlabelled part
        part.chapters = std::move(chapters);
        out.parts.push_back(std::move(part));
    }
    return out;
}

} // namespace ModulePlanner
} // namespace Folio
