// ─────────────────────────────────────────────────────────────────────────────
// cmm_test.cpp — pure-layer checks for the owned mind-map document (s50 slice 1).
// Covers: id minting, node/edge construction + guards, glyph derivation, subjects
// (many-to-many forward side), category recents, the frame stamp (incl. subject
// seeding), and a full json round-trip. GTK-free; runs in the g++ sandbox.
// ─────────────────────────────────────────────────────────────────────────────
/*
  g++ -std=c++20 -Wall -Wextra -Werror -I include -I /home/claude/sbox \
      tests/cmm_test.cpp src/CustomMindMap.cpp src/MindMap.cpp src/Iid.cpp \
      -o /tmp/cmm_test && /tmp/cmm_test

  (Fedora/clang, in-tree:)
  clang++ -std=c++20 -Wall -Wextra -Werror -I include \
      tests/cmm_test.cpp src/CustomMindMap.cpp src/MindMap.cpp src/Iid.cpp \
      -o /tmp/cmm_test && /tmp/cmm_test
*/
#include "CustomMindMap.hpp"

#include <cmath>
#include <cstdio>
#include <string>

using namespace Folio;

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do {                                           \
    if (cond) { ++g_pass; }                                             \
    else { ++g_fail; std::printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); } \
} while (0)

static bool approx(double a, double b, double eps = 1e-6) { return std::fabs(a - b) < eps; }

int main() {
    // ── id minting: shape + uniqueness ──────────────────────────────────────
    {
        const std::string a = cmm_make_node_id(), b = cmm_make_node_id();
        const std::string e = cmm_make_edge_id();
        CHECK(a.rfind("cmn_", 0) == 0, "node id has cmn_ prefix");
        CHECK(e.rfind("cme_", 0) == 0, "edge id has cme_ prefix");
        CHECK(a != b, "node ids are unique");
        // doc-local ids are deliberately NOT IidKinds:
        CHECK(iid_kind_of(a) == IidKind::Unknown, "cmn_ is not a project IidKind");
    }

    // ── construction + glyph derivation ─────────────────────────────────────
    {
        CMMDoc d; d.id = "ref_doc00001"; d.name = "Bene Gesserit";
        const std::string topic = cmm_add_text(d, "Bene Gesserit", 450, 300);
        const std::string voice = cmm_add_text(d, "The Voice", 775, 150, "vocal control of others");
        CHECK(d.nodes.size() == 2, "two text nodes added");
        CHECK(cmm_find_node(d, topic) != nullptr, "node lookup finds by id");
        CHECK(cmm_find_node(d, "cmn_nope") == nullptr, "node lookup misses unknown");

        const CMMNode* vn = cmm_find_node(d, voice);
        CHECK(vn && vn->body == "vocal control of others", "text body round-trips in-memory");
        CHECK(vn && cmm_node_glyph(*vn) == MapGlyph::Card, "text node glyph is Card");

        // an Anchor borrows its target's kind glyph
        const std::string anc = cmm_add_anchor(d, "scn_gomjabbar", "Gom Jabbar", 175, 565);
        const CMMNode* an = cmm_find_node(d, anc);
        CHECK(an && an->kind == CMMNodeKind::Anchor, "anchor node kind");
        CHECK(an && cmm_node_glyph(*an) == map_glyph_for(IidKind::Scene),
              "anchor glyph follows its target kind (Scene→Square)");
    }

    // ── edges: categorised, guarded, and they warm the recents ──────────────
    {
        CMMDoc d;
        const std::string a = cmm_add_text(d, "A", 0, 0);
        const std::string b = cmm_add_text(d, "B", 100, 0);
        const std::string e1 = cmm_add_edge(d, a, b, "lore");
        CHECK(!e1.empty() && d.edges.size() == 1, "edge added between two nodes");
        CHECK(d.edges[0].category == "lore", "edge carries its typed category");
        CHECK(!d.recent_categories.empty() && d.recent_categories.front() == "lore",
              "adding a categorised edge warms recents");

        CHECK(cmm_add_edge(d, a, a, "x").empty(),       "self-edge rejected");
        CHECK(cmm_add_edge(d, a, "cmn_ghost", "x").empty(), "edge to unknown node rejected");
        CHECK(cmm_add_edge(d, a, b, "").size() > 0,      "uncategorised (structural) edge allowed");
        CHECK(d.recent_categories.size() == 1,           "empty category does not pollute recents");
    }

    // ── subjects: many-to-many forward side, deduped ────────────────────────
    {
        CMMDoc d;
        CHECK(cmm_add_subject(d, "scn_a"), "add subject scn_a");
        CHECK(cmm_add_subject(d, "chr_paul"), "add subject chr_paul");
        CHECK(!cmm_add_subject(d, "scn_a"), "duplicate subject rejected");
        CHECK(d.subject_iids.size() == 2, "two distinct subjects (many-to-many)");
        CHECK(cmm_has_subject(d, "chr_paul"), "has_subject true for present");
        CHECK(!cmm_has_subject(d, "plc_x"), "has_subject false for absent");
        CHECK(cmm_remove_subject(d, "scn_a"), "remove present subject");
        CHECK(!cmm_remove_subject(d, "scn_a"), "remove-again is false");
        CHECK(d.subject_iids.size() == 1, "one subject remains");
    }

    // ── recents: front, dedupe, cap ─────────────────────────────────────────
    {
        CMMDoc d;
        cmm_note_category(d, "lore");
        cmm_note_category(d, "skills");
        cmm_note_category(d, "lore");                 // re-note moves to front, no dup
        CHECK(d.recent_categories.size() == 2, "recents deduped on re-note");
        CHECK(d.recent_categories.front() == "lore", "re-noted category jumps to front");
        for (int i = 0; i < 20; ++i) cmm_note_category(d, "c" + std::to_string(i));
        CHECK(static_cast<int>(d.recent_categories.size()) == kCmmRecentCap, "recents capped");
    }

    // ── frame stamp: subject seeding + radial fan + structural links ─────────
    {
        // a 4-slot frame (the W-labels minus the centre "What")
        MindMapFrame f;
        f.name = "Five W's";
        for (const char* lbl : { "Who", "Where", "When", "Why" }) {
            FrameSlot s; s.label = lbl; f.slots.push_back(s);
        }

        CMMDoc d;
        const std::string center = cmm_stamp_frame(d, f, 400, 300, 160, "scn_scene1");
        // centre is an Anchor on the subject, and the subject got registered
        const CMMNode* c = cmm_find_node(d, center);
        CHECK(c && c->kind == CMMNodeKind::Anchor && c->iid == "scn_scene1",
              "stamped centre is an Anchor on the subject");
        CHECK(cmm_has_subject(d, "scn_scene1"), "stamping with a subject registers it on the doc");
        CHECK(d.nodes.size() == 1 + 4, "centre + four slot nodes");
        CHECK(d.edges.size() == 4, "four structural centre→slot edges");
        for (const CMMEdge& e : d.edges) CHECK(e.category.empty(), "stamp edges are uncategorised");
        // slots sit on the ring around the centre
        bool all_on_ring = true;
        for (const CMMNode& n : d.nodes) {
            if (n.id == center) continue;
            const double r = std::hypot(n.x - 400.0, n.y - 300.0);
            if (!approx(r, 160.0, 1e-3)) all_on_ring = false;
        }
        CHECK(all_on_ring, "slot nodes fanned on the ring radius");
        CHECK(d.recent_categories.empty(), "structural stamp edges leave recents clean");

        // subject-less stamp → centre is a Text node named center_label
        CMMDoc d2;
        const std::string c2 = cmm_stamp_frame(d2, f, 0, 0, 120, "", "Topic");
        const CMMNode* cn2 = cmm_find_node(d2, c2);
        CHECK(cn2 && cn2->kind == CMMNodeKind::Text && cn2->title == "Topic",
              "subject-less stamp centres a named Text node");
        CHECK(d2.subject_iids.empty(), "subject-less stamp registers no subject");
    }

    // ── full json round-trip (lossless for owned data) ──────────────────────
    {
        CMMDoc d; d.id = "ref_bg"; d.name = "Bene Gesserit";
        d.viewport.pan_x = 12.5; d.viewport.pan_y = -7.0; d.viewport.zoom = 1.75;
        cmm_add_subject(d, "scn_scene1");
        cmm_add_subject(d, "chr_paul");
        const std::string topic = cmm_add_text(d, "Bene Gesserit", 450, 300, "the sisterhood");
        const std::string skill = cmm_add_text(d, "The Voice", 775, 150);
        const std::string anc   = cmm_add_anchor(d, "scn_scene1", "Gom Jabbar", 175, 565);
        cmm_add_edge(d, topic, skill, "skills");
        cmm_add_edge(d, topic, anc, "depicts", /*directed=*/true);

        const std::string text = cmm_to_string(d, /*pretty=*/false);
        const CMMDoc r = cmm_from_string(text);

        CHECK(r.id == d.id && r.name == d.name, "doc id/name survive round-trip");
        CHECK(r.subject_iids == d.subject_iids, "subjects survive round-trip (order kept)");
        CHECK(r.nodes.size() == d.nodes.size(), "node count survives");
        CHECK(r.edges.size() == d.edges.size(), "edge count survives");
        CHECK(approx(r.viewport.zoom, 1.75) && approx(r.viewport.pan_x, 12.5)
              && approx(r.viewport.pan_y, -7.0), "viewport survives round-trip");

        // a specific node + the anchor's bridge survive
        const CMMNode* rt = cmm_find_node(r, topic);
        CHECK(rt && rt->title == "Bene Gesserit" && rt->body == "the sisterhood"
              && approx(rt->x, 450) && approx(rt->y, 300), "text node fields survive");
        const CMMNode* ra = cmm_find_node(r, anc);
        CHECK(ra && ra->kind == CMMNodeKind::Anchor && ra->iid == "scn_scene1",
              "anchor target iid survives (the bridge to truth)");

        // a directed, categorised edge survives with its flags
        bool found_directed = false;
        for (const CMMEdge& e : r.edges)
            if (e.category == "depicts" && e.directed && e.from == topic && e.to == anc)
                found_directed = true;
        CHECK(found_directed, "directed categorised edge survives with endpoints + flags");

        // idempotent re-serialise (stable form)
        CHECK(cmm_to_string(r, false) == text, "re-serialising the parsed doc is identical");
    }

    std::printf("\nCustomMindMap pure checks: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
