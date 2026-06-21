// ─────────────────────────────────────────────────────────────────────────────
// Folio — test_template_edit_dnd.cpp   (s43 — "The Front Door")
//
// Proves the pure brain behind the builder's drag-and-drop reorder:
// TemplateEdit::move_field_relative. The GTK DropTarget reads a cursor y to pick
// before/after and hands a (moved_id, target_id, place_after) triple here; this
// is where the §4 floor-buffer pin and the no-op detection live, so the test
// nails the invariants before any GTK is compiled.
//
//   • before / after a target reorders correctly
//   • the trailing RichText floor buffer NEVER moves and nothing lands after it
//   • dragging a field across a Heading marker is just a reorder (into/out of a
//     block is render-time grouping — no special case)
//   • self-drop and order-preserving drops report false (no spurious rebuild)
// ─────────────────────────────────────────────────────────────────────────────
/*
g++  -std=c++20 -I../include -I/home/claude/sbox -Wall -Wextra -o /tmp/t_tpl_dnd test_template_edit_dnd.cpp && /tmp/t_tpl_dnd
clang++ -std=c++20 -I../include -Wall -Wextra -Werror -o /tmp/t_tpl_dnd test_template_edit_dnd.cpp && /tmp/t_tpl_dnd
*/
#include "TemplateEdit.hpp"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace Folio;

namespace {
int g_pass = 0, g_fail = 0;
void check(bool cond, const std::string& what) {
    if (cond) { ++g_pass; }
    else { ++g_fail; std::cerr << "  FAIL: " << what << "\n"; }
}

// A field with an explicit id (skip the slug minting so the test reads cleanly).
FieldSchema fld(const std::string& id, FieldType t = FieldType::Text) {
    FieldSchema f; f.id = id; f.type = t; f.label = id; return f;
}

std::string order(const Template& t) {
    std::string s;
    for (const auto& f : t.fields) { if (!s.empty()) s += ','; s += f.id; }
    return s;
}

// a, b, c, then the trailing richtext buffer "buf".
Template make() {
    Template t;
    t.type_name = "Thing";
    t.fields = { fld("a"), fld("b"), fld("c"), fld("buf", FieldType::RichText) };
    return t;
}
}  // namespace

int main() {
    // ── before / after basic reorder ──────────────────────────────────────────
    {
        Template t = make();
        check(TemplateEdit::move_field_relative(t, "c", "a", /*after=*/false),
              "move c before a returns true");
        check(order(t) == "c,a,b,buf", "c lands before a -> c,a,b,buf");
    }
    {
        Template t = make();
        check(TemplateEdit::move_field_relative(t, "a", "c", /*after=*/true),
              "move a after c returns true");
        check(order(t) == "b,c,a,buf", "a lands after c -> b,c,a,buf");
    }
    {
        Template t = make();
        check(TemplateEdit::move_field_relative(t, "a", "b", /*after=*/true),
              "move a after b returns true");
        check(order(t) == "b,a,c,buf", "a lands after b -> b,a,c,buf");
    }

    // ── floor buffer is pinned (§4) ─────────────────────────────────────────────
    {
        Template t = make();
        check(!TemplateEdit::move_field_relative(t, "buf", "a", false),
              "the trailing buffer never moves (drag rejected)");
        check(order(t) == "a,b,c,buf", "order unchanged after rejected buffer drag");
    }
    {
        // Dropping a field AFTER the buffer must fold to BEFORE it: nothing may
        // sit below the floor buffer.
        Template t = make();
        check(TemplateEdit::move_field_relative(t, "a", "buf", /*after=*/true),
              "drop after buffer is accepted (folded to before)");
        check(order(t) == "b,c,a,buf", "a lands just above the buffer, not below it");
        check(TemplateEdit::is_trailing_buffer(t, t.fields.size() - 1),
              "buffer is still the trailing field");
    }
    {
        Template t = make();
        check(TemplateEdit::move_field_relative(t, "a", "buf", /*after=*/false),
              "drop before buffer accepted");
        check(order(t) == "b,c,a,buf", "a lands above buffer");
    }

    // ── Heading marker is just another row — crossing it is a plain reorder ─────
    {
        Template t;
        t.type_name = "Thing";
        t.fields = { fld("h1", FieldType::Heading), fld("a"),
                     fld("h2", FieldType::Heading), fld("b"),
                     fld("buf", FieldType::RichText) };
        // Drag b up under the first heading (into the first block).
        check(TemplateEdit::move_field_relative(t, "b", "a", /*after=*/true),
              "move b after a (into first block) returns true");
        check(order(t) == "h1,a,b,h2,buf", "b joins the first block -> h1,a,b,h2,buf");
        // Drag a heading itself (headings reorder like any row).
        check(TemplateEdit::move_field_relative(t, "h2", "h1", /*after=*/false),
              "a heading can be reordered");
        check(order(t) == "h2,h1,a,b,buf", "h2 lands before h1");
    }

    // ── no-op / self drops report false (no spurious rebuild) ───────────────────
    {
        Template t = make();
        check(!TemplateEdit::move_field_relative(t, "a", "a", false),
              "self-drop returns false");
        check(!TemplateEdit::move_field_relative(t, "a", "b", /*after=*/false),
              "a before b when a already precedes b is a no-op");
        check(order(t) == "a,b,c,buf", "order unchanged on no-op");
        check(!TemplateEdit::move_field_relative(t, "zzz", "a", false),
              "unknown moved id returns false");
        check(!TemplateEdit::move_field_relative(t, "a", "zzz", false),
              "unknown target id returns false");
    }

    // ── a template with NO trailing buffer (structured-only) still reorders ─────
    {
        Template t;
        t.type_name = "Bare";
        t.fields = { fld("a"), fld("b"), fld("c") };
        check(TemplateEdit::move_field_relative(t, "c", "a", false),
              "reorder works with no floor buffer");
        check(order(t) == "c,a,b", "c before a -> c,a,b");
        check(TemplateEdit::move_field_relative(t, "c", "b", /*after=*/true),
              "c after b accepted (no buffer to block)");
        check(order(t) == "a,b,c", "c lands last -> a,b,c");
    }

    std::cout << (g_fail == 0 ? "PASS" : "FAIL")
              << "  (" << g_pass << " checks, " << g_fail << " failed)\n";
    return g_fail == 0 ? 0 : 1;
}
