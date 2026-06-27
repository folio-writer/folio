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
#include "Gallery.hpp"         // instrument_object_links — the 4th-source scan
#include "ImagePool.hpp"       // image_pool().all() for image→object links

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

    // (3) image→object links: each live pool fragment's links (the author's
    //     "this image depicts X"). from = the ast_ fragment, to = the object —
    //     so gallery_objects_of (forward) and gallery_images_of (reverse) read.
    for (const ImageFragment& f : model.image_pool().all()) {
        if (f.deleted) continue;
        for (const std::string& target : f.links)
            add(f.iid, target, edge_kind_for_target(target), "");
    }

    // (4) instrument→object associations: an owned instrument (Gallery, Mind Map)
    //     that is ABOUT objects, with the link stored in its node BODY as
    //     structured JSON — NOT prose, so sources (1)–(3) never saw it and the
    //     Map never drew it. from = the instrument NODE, to = the object; both are
    //     real binder nodes, so the Map renders the edge and its hover highlight/
    //     dim apply with no view-side change (instrument_object_links is the pure,
    //     sandbox-tested scan). Journals are absent by design: their entry links
    //     are prose folio-links, already unioned in by source (1).
    for (const BinderNode* n : model.all_node_ptrs()) {
        if (!n || n->kind != BinderKind::Reference) continue;
        for (const std::string& target :
                 instrument_object_links(n->template_id, n->content))
            add(n->iid, target, edge_kind_for_target(target), "");
    }

    // (5) timeline-authored scene→subject links: explicit per-scene subject edges
    //     written by the Relationship Timeline's sweep (s80, option 2 — the prose
    //     is never touched; the node owns its links, mirroring image fragments).
    //     from = the scene, to = the subject; read here so the timeline re-reads
    //     its own writes through the one projection ("edges are read, never owned").
    for (const BinderNode* n : model.all_node_ptrs()) {
        if (!n || n->subject_links.empty()) continue;
        for (const std::string& target : n->subject_links)
            add(n->iid, target, edge_kind_for_target(target), "");
    }

    return out;
}

}  // namespace Folio
