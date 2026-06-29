// ─────────────────────────────────────────────────────────────────────────────
// test_kind_convert.cpp — Scene ↔ Group conversion rule (s89)
//
// WHAT THIS PROVES
//   1. offered_conversion(section, kind, has_children) — the pure rule that
//      decides which conversion (if any) the Sidebar offers for a node:
//        • Manuscript-only (a Group in Characters/Places/References is a section
//          folder, not a chapter; never offer it Scene there).
//        • Scene → Group always (a leaf gaining the capacity to own children).
//        • Group → Scene only when CHILDLESS (so the result is a clean leaf with
//          no child move — no binder-vector realloc, no BinderNode* invalidation).
//   2. The flip is a role toggle on ONE identity: applying the conversion changes
//      only `kind`; the iid, content, and children vector are preserved.  This is
//      the property that lets every folio-link / backlink keyed by iid keep
//      resolving with nothing to migrate.
//
// WHY A MIRRORED COPY
//   DocumentModel.hpp defines the real offered_conversion() but transitively
//   pulls <glibmm.h> (via FolioPrefs.hpp), so it can't compile in the GTK-free
//   sandbox.  The enums + rule below are a verbatim mirror of the shipping code;
//   if you edit the truth table in DocumentModel.hpp, update it here too.
//
// ─────────────────────────────────────────────────────────────────────────────
/*
  BUILD + RUN

  Sandbox (g++):
    g++ -std=c++20 -Wall -Wextra -o /tmp/test_kind_convert \
        tests/test_kind_convert.cpp && /tmp/test_kind_convert

  Fedora (clang++ 21, project flags):
    clang++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow \
        -o /tmp/test_kind_convert tests/test_kind_convert.cpp && \
        /tmp/test_kind_convert
*/

#include <cstdio>
#include <string>
#include <vector>

// ── Mirror of the shipping enums (DocumentModel.hpp) ─────────────────────────
enum class Section { Manuscript, Characters, Places, References, Templates, Trash };
enum class BinderKind { Group, Scene, Character, Place, Reference, Template };

// ── Mirror of the shipping rule (DocumentModel.hpp :: offered_conversion) ────
enum class KindConversion { None, ToGroup, ToScene };

KindConversion offered_conversion(Section section, BinderKind kind,
                                  bool has_children) {
    if (section != Section::Manuscript) return KindConversion::None;
    if (kind == BinderKind::Scene)      return KindConversion::ToGroup;
    if (kind == BinderKind::Group && !has_children)
        return KindConversion::ToScene;
    return KindConversion::None;
}

// ── Mirror of the shipping flip (DocumentModel::convert_node_kind body) ──────
// A minimal node carrying just the fields whose preservation we assert.
struct Node {
    BinderKind  kind;
    std::string iid;
    std::string content;
    std::vector<Node> children;
};

// Returns true if a conversion was applied (mirrors convert_node_kind).
bool apply_convert(Section section, Node& n) {
    switch (offered_conversion(section, n.kind, !n.children.empty())) {
        case KindConversion::ToGroup: n.kind = BinderKind::Group; return true;
        case KindConversion::ToScene: n.kind = BinderKind::Scene; return true;
        case KindConversion::None:    return false;
    }
    return false;
}

// ── tiny harness ─────────────────────────────────────────────────────────────
static int g_pass = 0, g_fail = 0;
static const char* kc_name(KindConversion k) {
    switch (k) { case KindConversion::ToGroup: return "ToGroup";
                 case KindConversion::ToScene: return "ToScene";
                 default: return "None"; }
}
static void check(const char* what, KindConversion got, KindConversion want) {
    if (got == want) { ++g_pass; }
    else { ++g_fail; std::printf("  FAIL %s: got %s want %s\n",
                                 what, kc_name(got), kc_name(want)); }
}
static void check_bool(const char* what, bool got, bool want) {
    if (got == want) { ++g_pass; }
    else { ++g_fail; std::printf("  FAIL %s: got %d want %d\n", what, got, want); }
}

int main() {
    using KC = KindConversion;
    using BK = BinderKind;

    // ── 1. Manuscript Scene → always ToGroup (children state irrelevant) ──────
    check("ms scene, no children",   offered_conversion(Section::Manuscript, BK::Scene, false), KC::ToGroup);
    check("ms scene, has children",  offered_conversion(Section::Manuscript, BK::Scene, true),  KC::ToGroup);

    // ── 2. Manuscript Group → ToScene only when childless ─────────────────────
    check("ms group, no children",   offered_conversion(Section::Manuscript, BK::Group, false), KC::ToScene);
    check("ms group, has children",  offered_conversion(Section::Manuscript, BK::Group, true),  KC::None);

    // ── 3. Manuscript non-Scene/Group leaves are never convertible ────────────
    check("ms character",            offered_conversion(Section::Manuscript, BK::Character, false), KC::None);
    check("ms place",                offered_conversion(Section::Manuscript, BK::Place,     false), KC::None);
    check("ms reference",            offered_conversion(Section::Manuscript, BK::Reference, false), KC::None);
    check("ms template",             offered_conversion(Section::Manuscript, BK::Template,  false), KC::None);

    // ── 4. Manuscript-only: Groups elsewhere are folders, never offered Scene ─
    check("chars group childless",   offered_conversion(Section::Characters, BK::Group, false), KC::None);
    check("places group children",   offered_conversion(Section::Places,     BK::Group, true),  KC::None);
    check("refs group childless",    offered_conversion(Section::References,  BK::Group, false), KC::None);
    check("tpl group childless",     offered_conversion(Section::Templates,   BK::Group, false), KC::None);
    check("trash group childless",   offered_conversion(Section::Trash,       BK::Group, false), KC::None);
    // Defensive: a Scene outside Manuscript (malformed) is still not converted.
    check("chars scene (defensive)", offered_conversion(Section::Characters, BK::Scene, false), KC::None);
    check("trash scene (defensive)", offered_conversion(Section::Trash,      BK::Scene, false), KC::None);

    // ── 5. Flip preserves identity (iid + content + children) ─────────────────
    {
        Node scene{ BK::Scene, "scn_k3f9a2b7", "<p>prose body</p>", {} };
        bool ok = apply_convert(Section::Manuscript, scene);
        check_bool("scene→group applied",        ok, true);
        check_bool("scene→group kind is Group",  scene.kind == BK::Group, true);
        check_bool("scene→group iid preserved",  scene.iid == "scn_k3f9a2b7", true);
        check_bool("scene→group content kept",   scene.content == "<p>prose body</p>", true);
        check_bool("scene→group children empty", scene.children.empty(), true);
    }
    {
        Node group{ BK::Group, "grp_q8w1e4r2", "<p>chapter preface</p>", {} };
        bool ok = apply_convert(Section::Manuscript, group);
        check_bool("group→scene applied",        ok, true);
        check_bool("group→scene kind is Scene",  group.kind == BK::Scene, true);
        check_bool("group→scene iid preserved",  group.iid == "grp_q8w1e4r2", true);
        check_bool("group→scene content kept",   group.content == "<p>chapter preface</p>", true);
    }
    {
        // Group with children: not offered → flip is a no-op, kind unchanged.
        Node parent{ BK::Group, "grp_parent01", "", {} };
        parent.children.push_back(Node{ BK::Scene, "scn_child001", "", {} });
        bool ok = apply_convert(Section::Manuscript, parent);
        check_bool("group(with kids)→scene no-op",  ok, false);
        check_bool("group(with kids) kind unchanged", parent.kind == BK::Group, true);
        check_bool("group(with kids) child intact",   parent.children.size() == 1, true);
    }

    std::printf("\n%s: %d passed, %d failed\n",
                g_fail == 0 ? "PASS" : "FAIL", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
