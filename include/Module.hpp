#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — Module.hpp   (s23 — project-pattern anatomy; pure, GTK-free)
//
// A "module" (Scott's "project pattern"/"mod") is the declared anatomy a project
// of a given kind must have — DESIGN §5.4. It spans two axes:
//   • craft  — the story form (Key Points grouped into acts → Parts).
//   • deploy — the deliverable (front/back matter roles; the §5.3 spine table).
//
// Modules live as JSON files in ~/.local/share/folio/modules/ (the global
// library / "stamp"). Applying a module copies its anatomy INTO the project
// bundle (the "instance", which then travels — §5.4). This header is the data
// model + the planner's output (ScaffoldPlan); ModuleIO does json⇄struct and
// ModulePlanner turns a module + a few inputs into a Part/Chapter/Scene plan.
//
// Terminology: "Key Point" (KP), never "signpost" (that is Bell's word; Folio
// uses its own). The unwired StoryGraph `signpost_id` becomes `kp_id` when wired.
// ─────────────────────────────────────────────────────────────────────────────

#include <string>
#include <vector>

namespace Folio {

// Top-level body container the craft anatomy forms. A 3-act pattern forms three
// of these; "None" = chapters live at the manuscript root (no act containers).
enum class TopContainer { Part, Book, None };

inline const char* top_container_to_str(TopContainer t) {
    switch (t) { case TopContainer::Book: return "book";
                 case TopContainer::None: return "none";
                 default: return "part"; }
}
inline TopContainer top_container_from_str(const std::string& s) {
    if (s == "book") return TopContainer::Book;
    if (s == "none") return TopContainer::None;
    return TopContainer::Part;
}

// ── Key Point — one beat of the arc ──────────────────────────────────────────
// Two INDEPENDENT dimensions (Scott, s23):
//   frenetic — action/pacing energy, 0=lull(cool) .. 1=frenzy(hot). Drives the
//              cool→hot→cool colour and the "stock-market" curve; the Climax is
//              the apex. NOT derived from weight.
//   weight   — scene-count share. Scenes "expand to fill" the target by weight;
//              a KP can be short-and-hot or long-and-warm.
struct KeyPoint {
    std::string id;          // "kp_climax"
    std::string label;       // "Climax"   (the tag the scene wears)
    int         order = 0;   // 1-based, across the whole arc
    // ── Two independent per-KP energy dimensions (the mixing-board faders) ─────
    // frenetic — the KP's PACING baseline (the round fader). The cluster's mean
    //   action energy; the per-scene pacing pattern (Module::pacing) rides UNDER
    //   this at scene resolution, oscillating around it (s24).
    // arc      — the KP's STORY-ARC value (the diamond fader): dramatic tension,
    //   rises to the Climax apex. Independent of frenetic — a beat can be high
    //   tension yet low pacing (the lull before the storm) (s24).
    double      frenetic = 0.0;
    double      arc      = 0.0;
    double      weight   = 1.0;  // scene-count share (the channel WIDTH on the board)
    // s23: scenes are NOT uniform size. A KP may pin an exact count and/or an
    // extended per-scene word size. The epilogue is the canonical case — one
    // extended cool-down scene (~2-3K), the release cap, not a 1K cluster.
    int         fixed_scenes = 0;  // exact count, exempt from weight expansion (0 = by weight)
    int         scene_words  = 0;  // per-scene word size; 0 = book average
    // s24: colour is purely POSITIONAL — spectrum_hex() sampled at the KP's place
    // in the arc, NOT derived from frenetic. color_idx is just the KP's order
    // (1-based) so each KP owns its own swatch in the spectrum palette; tag name
    // and swatch name are the same string and cannot drift.
    int         color_idx    = 0;
    // s24: the meaning of the beat — what it DOES — so the editor teaches as it
    // goes and a saved module is self-documenting (e.g. handed to a protégé).
    std::string description{};
};

// ── Act — a group of KPs that becomes one top-level container (Part/Book) ─────
struct Act {
    std::string id;          // "act2"
    std::string label;       // "Confrontation"
    std::vector<KeyPoint> kps;
};

struct CraftAnatomy {
    std::string      kind;   // "folio_keypoints" | "three_act"
    std::vector<Act> acts;
};

// ── Matter role — one front/back-matter part (the §5.3 spine table as data) ──
struct MatterRole {
    std::string role;                 // "blurb","title","copyright","about_author"…
    int         rank = 0;             // forced compile order
    std::string page_side = "recto";  // "recto" | "verso" | "any"
    std::string src       = "authored"; // "authored" (emit) | "generated" (render)
};

struct DeployAnatomy {
    std::string             kind;   // "kdp"
    std::vector<MatterRole> front;  // ranked
    std::vector<MatterRole> back;   // ranked
};

// ── Pacing pattern — the per-SCENE rhythm, a module-level property (s24) ──────
// Frenetic isn't only a per-KP baseline; the individual pacing varies scene to
// scene in a repeating rhythm — the James Bond cadence: light, light, medium,
// action, repeat. This is the author's signature micro-rhythm and it travels
// with the module (Cussler's vs a literary writer's differ). `levels` is the
// repeating sequence of per-scene energy levels; the planner cycles it across
// the whole flat scene run (continuously, crossing KP edges — pacing is NOT a
// KP property) and scales each scene around its KP's frenetic baseline, so the
// KP fader stays the cluster's mean. An empty or single-entry pattern = flat.
struct PacingPattern {
    std::vector<double> levels;   // e.g. {light, light, medium, action}
};

// ── Module — the whole pattern ───────────────────────────────────────────────
struct Module {
    std::string   id;
    std::string   name;
    TopContainer  top = TopContainer::Part;
    CraftAnatomy  craft;
    DeployAnatomy deploy;
    PacingPattern pacing;   // s24 — the scene-to-scene pacing rhythm (Bond cadence)
};

// ── Scene-frenetic pattern — the optional per-scene energy guide ──────────────
// Flat: every scene in a KP sits at the KP's frenetic (the averaged baseline).
// BuildToSpike: redistribute energy WITHIN each cluster (builds below, spikes
// above) while preserving the KP frenetic as the cluster MEAN — Gardner's
// "lulls and frenzy" as a guide, never a verdict. Mean-preserving so "scenes are
// averaged over the KP" stays literally true.
enum class ScenePattern { Flat, BuildToSpike };

inline const char* scene_pattern_to_str(ScenePattern p) {
    return p == ScenePattern::BuildToSpike ? "build_to_spike" : "flat";
}
inline ScenePattern scene_pattern_from_str(const std::string& s) {
    return s == "build_to_spike" ? ScenePattern::BuildToSpike : ScenePattern::Flat;
}

// ── Planner inputs (gathered by the dialog) ───────────────────────────────────
// s23: an author-defined structural Part/Book — a thematic title plus a
// contiguous run of chapters. Parts are OPTIONAL and ORTHOGONAL to acts: four
// of Scott's novels showed Parts ≠ acts (thematic titles, any count — 0 or 4 —
// continuous chapter numbering across them, never one-Part-per-act). An empty
// part_specs list means no Part layer at all (chapters at the manuscript root).
struct PlanPartSpec {
    std::string title;      // e.g. "On the Rebound"; "" → "Part N"
    int         chapters = 1;
};

struct PlanInputs {
    long         target_words    = 90000;
    int          avg_scene_words = 1130;   // Scott's signature scene (4-novel mean)
    int          chapters        = 10;     // ~7-8 scenes/ch (Scott's tagged novels); used when part_specs empty
    TopContainer top             = TopContainer::None; // default: no Part/Book layer
    ScenePattern scene_pattern   = ScenePattern::Flat;
    int          spike_cycle     = 3;       // BuildToSpike: scenes per build→spike cycle
    bool         prologue        = false;   // optional cool bookend before the arc
    bool         epilogue        = false;
    int          prologue_scenes = 1;       // s24: bookends are growable SLICES, not
    int          epilogue_scenes = 1;       //      a single scene (Cussler cold-open)
    // Optional author-defined Parts (contiguous chapter ranges). When non-empty
    // and top != None, these set the Part structure AND the chapter total;
    // chapters number continuously across all parts.
    std::vector<PlanPartSpec> part_specs;
};

// ── ScaffoldPlan — the planner's output; Layer 2 materialises this to nodes ──
struct PlanScene {
    int         index    = 0;    // 1-based reading-order position in the book
    std::string kp_id;           // the KP this scene serves
    std::string kp_label;
    double      frenetic = 0.0;  // per-scene pacing energy (KP baseline × pacing pattern)
    double      arc      = 0.0;  // per-scene story-arc/tension (= its KP's arc value)
    int         target_words = 0;// this scene's word target (extended KPs differ)
    int         color_idx = 0;   // KP tag colour, 1-based into project palette
    bool        pin      = false;// s29: this scene IS a pinned hinge (fixed_scenes
                                 // KP) — a milestone the author writes first.
};
struct PlanChapter {
    std::string            label;   // "Chapter 7"
    std::vector<PlanScene> scenes;
};
struct PlanPart {
    std::string              label;     // "Part I · Setup"  ("" when TopContainer::None)
    std::vector<PlanChapter> chapters;
    int scene_count() const {
        int n = 0; for (auto& c : chapters) n += (int)c.scenes.size(); return n;
    }
};
struct ScaffoldPlan {
    int                     total_scenes = 0;
    std::vector<PlanScene>  prologue;    // 0 or 1 cool single-scene bookend
    std::vector<PlanPart>   parts;       // one synthetic part (label "") if top==None
    std::vector<PlanScene>  epilogue;    // 0 or 1 cool single-scene bookend
    std::vector<MatterRole> front;       // pass-through, ranked
    std::vector<MatterRole> back;
};

// ── Built-in modules (shipped defaults; the library seeds from these) ─────────
Module built_in_three_act();       // safe generic default (no IP)
Module built_in_folio_keypoints(); // Folio's own 12-KP arc

} // namespace Folio
