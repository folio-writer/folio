// ─────────────────────────────────────────────────────────────────────────────
// Folio — ModuleMaterializer.cpp   (s23 — Layer 2)   See ModuleMaterializer.hpp.
// ─────────────────────────────────────────────────────────────────────────────
#include "ModuleMaterializer.hpp"
#include "DocumentModel.hpp"

namespace Folio {
namespace ModuleMaterializer {

namespace {
// Stamp the fields a created node carries: KP tag, word target, tag colour,
// and the per-scene structure energies (frenetic pacing + story arc; s24).
void stamp(DocumentModel& model, const std::vector<int>& path, const PlanScene& s) {
    if (BinderNode* n = model.node_at(Section::Manuscript, path)) {
        n->kp_id       = s.kp_id;
        n->kp_label    = s.kp_label;
        n->word_target = s.target_words;
        n->color_idx   = s.color_idx;
        n->frenetic    = s.frenetic;
        n->arc         = s.arc;
        n->pin         = s.pin;
    }
}
} // namespace

std::string materialize(DocumentModel& model, const ScaffoldPlan& plan) {
    const Section sec = Section::Manuscript;
    const std::vector<int> root{};            // manuscript top level
    std::string first_iid;
    int scene_no = 0;                          // continuous "Scene N" numbering

    auto remember_first = [&](const std::vector<int>& p) {
        if (first_iid.empty())
            if (BinderNode* n = model.node_at(sec, p)) first_iid = n->iid;
    };

    // Cool single-scene bookend at the manuscript root (own leaf, named for role).
    auto add_bookend = [&](const std::vector<PlanScene>& book, const char* name) {
        if (book.empty()) return;
        std::vector<int> p = model.add_leaf(sec, root, name);
        stamp(model, p, book.front());
        remember_first(p);
    };

    add_bookend(plan.prologue, "Prologue");

    for (const auto& part : plan.parts) {
        // Part/Book container — skipped when the plan carries no label
        // (TopContainer::None ⇒ chapters live at the manuscript root).
        const bool has_part = !part.label.empty();
        std::vector<int> chapters_parent = root;
        if (has_part) {
            std::vector<int> part_path = model.add_group(sec, root, part.label);
            remember_first(part_path);
            chapters_parent = part_path;
        }

        for (const auto& ch : part.chapters) {
            // Chapter group — name is "Chapter N" (the planner numbers it); the
            // optional "- [Title]" is the author's to add later (export subhead).
            std::vector<int> ch_path = model.add_group(sec, chapters_parent, ch.label);
            remember_first(ch_path);
            for (const auto& s : ch.scenes) {
                // Scene name = "Scene N" (continuous). The KP rides as a tag
                // (kp_id) + tag colour, NOT as the visible name.
                std::string title = "Scene " + std::to_string(++scene_no);
                std::vector<int> sc_path = model.add_leaf(sec, ch_path, title);
                stamp(model, sc_path, s);
                remember_first(sc_path);
            }
        }
    }

    add_bookend(plan.epilogue, "Epilogue");
    return first_iid;
}

} // namespace ModuleMaterializer
} // namespace Folio
