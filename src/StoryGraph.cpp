// ─────────────────────────────────────────────────────────────────────────────
// StoryGraph.cpp — the graph READING over the one iid body (s48 first cut).
//
// s47 left StoryGraph.hpp a block-in. s48 wires the one function the mind-map
// canvas needs: edges_from_backlinks — the bridge that turns the two edge stores
// the app ALREADY keeps into a single typed StoryEdge list the lenses render.
// No new edge store is created; "edges are read, not owned" (HANDOFF §3). The
// rest of the block-in (StoryGraph::read and its timeline projections) stays
// header-only until the timeline lens needs it.
//
// The only non-trivial logic — what KIND an edge takes — is the pure free
// function edge_kind_for_target, kept separate so it is sandbox-tested while the
// container-walk that calls it is mechanical (Scott-compiled GTK-side glue).
// ─────────────────────────────────────────────────────────────────────────────
#include "StoryGraph.hpp"

#include "DocumentModel.hpp"   // backlinks() + object_store(); pulls no GTK

namespace Folio {

// edge_kind_for_target is inline in StoryGraph.hpp (kept GTK-free + sandbox-
// tested). Union of the two existing edge indices over the one iid body:
//   1. s20 PROSE LINKS — DocumentModel::backlinks() is keyed by TARGET iid; each
//      BacklinkEntry names the SOURCE node + the source paragraph anchor. One
//      typed edge per entry (source → target), kind from the target's prefix.
//   2. s44 OBJECT RELATIONS — each store Object's outgoing_edges (resolved over
//      its template's relation fields) → one typed edge per target.
// De-duplicated by (from,to,kind): the same pair surfacing through both indices
// is one edge on the map, not two parallel lines.
std::vector<StoryEdge>
StoryGraph::edges_from_backlinks(const DocumentModel& model) {
    std::vector<StoryEdge> out;
    std::vector<std::string> seen;   // small N (a manuscript's links); linear is fine

    auto already = [&](const std::string& key) {
        for (const auto& k : seen) if (k == key) return true;
        seen.push_back(key);
        return false;
    };
    auto add = [&](const std::string& from, const std::string& to,
                   EdgeKind kind, const std::string& anchor) {
        if (from.empty() || to.empty() || from == to) return;
        const std::string key = from + '>' + to + '#' + std::to_string((int)kind);
        if (already(key)) return;
        out.push_back({ from, to, kind, anchor, "" });
    };

    // (1) prose-link backlinks: map<target_iid, vector<BacklinkEntry>>
    for (const auto& [target_iid, entries] : model.backlinks()) {
        const EdgeKind kind = edge_kind_for_target(target_iid);
        for (const BacklinkEntry& e : entries)
            add(e.source_iid, target_iid, kind, e.source_anchor);
    }

    // (2) object relations: every object's outgoing relation targets
    const ObjectStore& store = model.object_store();
    for (const Object& o : store.objects) {
        const Template* t = store.find_template(o.type);
        if (!t) continue;
        for (const std::string& target : o.outgoing_edges(*t))
            add(o.iid, target, edge_kind_for_target(target), "");
    }

    return out;
}

}  // namespace Folio
