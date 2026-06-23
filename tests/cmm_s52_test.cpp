// ─────────────────────────────────────────────────────────────────────────────
// cmm_s52_test.cpp — pure checks for the s52 owned-MM additions: node/edge
// removal (with incident-edge cleanup) and the CMMNode.color_idx round-trip.
// Extends the s50 cmm_test; same discipline — proven in the g++ sandbox before
// any GTK wiring leans on it.
// ─────────────────────────────────────────────────────────────────────────────
/*
  Build + run (sandbox g++):
    g++ -std=c++20 -Wall -Wextra -Werror -I include -I /home/claude/sbox \
        tests/cmm_s52_test.cpp src/CustomMindMap.cpp src/MindMap.cpp src/Iid.cpp \
        -o /tmp/cmm_s52_test && /tmp/cmm_s52_test

  Build + run (Fedora clang++):
    clang++ -std=c++20 -Wall -Wextra -Werror -I include \
        tests/cmm_s52_test.cpp src/CustomMindMap.cpp src/MindMap.cpp src/Iid.cpp \
        -o /tmp/cmm_s52_test && /tmp/cmm_s52_test
*/
#include "CustomMindMap.hpp"

#include <cassert>
#include <iostream>

using namespace Folio;

static int g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { \
    std::cerr << "FAIL line " << __LINE__ << ": " #cond "\n"; return 1; } } while (0)

int main() {
    // ── Build a small map: a centre + two leaves, two edges ──────────────────
    CMMDoc d;
    d.id = "ref_test";
    const std::string c = cmm_add_text(d, "Centre", 0, 0);
    const std::string a = cmm_add_text(d, "Leaf A", 100, 0);
    const std::string b = cmm_add_anchor(d, "chr_xyz", "Mara", 0, 100);
    const std::string e1 = cmm_add_edge(d, c, a, "lore");
    const std::string e2 = cmm_add_edge(d, c, b, "");      // structural
    CHECK(d.nodes.size() == 3);
    CHECK(d.edges.size() == 2);
    CHECK(!e1.empty() && !e2.empty());

    // ── Remove a leaf node → its incident edge goes with it ──────────────────
    CHECK(cmm_remove_node(d, a));
    CHECK(d.nodes.size() == 2);
    CHECK(d.edges.size() == 1);                            // e1 (c↔a) dropped
    CHECK(cmm_find_node(d, a) == nullptr);
    CHECK(cmm_find_node(d, c) != nullptr);                 // centre survives
    // the surviving edge is e2 (c↔b), still well-formed
    CHECK(d.edges.front().id == e2);
    CHECK(cmm_find_node(d, d.edges.front().from) != nullptr);
    CHECK(cmm_find_node(d, d.edges.front().to)   != nullptr);

    // ── Removing the centre drops the last edge too (no dangling endpoint) ───
    CHECK(cmm_remove_node(d, c));
    CHECK(d.nodes.size() == 1);                            // only the Anchor left
    CHECK(d.edges.empty());
    CHECK(cmm_remove_node(d, "cmn_nope") == false);        // unknown id

    // ── Removing an Anchor leaves the real object untouched (pointer was RO) ─
    const CMMNode* anchor = cmm_find_node(d, b);
    CHECK(anchor && anchor->kind == CMMNodeKind::Anchor && anchor->iid == "chr_xyz");
    CHECK(cmm_remove_node(d, b));
    CHECK(d.nodes.empty());

    // ── cmm_remove_edge removes just the edge, both nodes stay ───────────────
    CMMDoc d2;
    const std::string p = cmm_add_text(d2, "P", 0, 0);
    const std::string q = cmm_add_text(d2, "Q", 50, 0);
    const std::string e = cmm_add_edge(d2, p, q, "is-a");
    CHECK(cmm_remove_edge(d2, e));
    CHECK(d2.edges.empty());
    CHECK(d2.nodes.size() == 2);                           // endpoints untouched
    CHECK(cmm_remove_edge(d2, "cme_nope") == false);

    // ── color_idx round-trips losslessly; 0 omitted, >0 preserved ────────────
    CMMDoc d3;
    d3.id = "ref_color";
    const std::string n0 = cmm_add_text(d3, "uncoloured", 0, 0);
    const std::string n1 = cmm_add_text(d3, "coloured",  10, 0);
    for (auto& nn : d3.nodes) if (nn.id == n1) nn.color_idx = 4;
    const std::string blob = cmm_to_string(d3, false);
    CMMDoc rt = cmm_from_string(blob);
    const CMMNode* r0 = cmm_find_node(rt, n0);
    const CMMNode* r1 = cmm_find_node(rt, n1);
    CHECK(r0 && r0->color_idx == 0);
    CHECK(r1 && r1->color_idx == 4);
    // idempotent re-serialise
    CHECK(cmm_to_string(rt, false) == blob);

    std::cout << "cmm_s52_test: all " << g_checks << " checks passed\n";
    return 0;
}
