// move_node_test.cpp — proves the DocumentModel::move_node destination-path fix.
//
// The bug: move_node erases the source node from its parent vector, THEN resolves
// the destination path using the ORIGINAL (pre-erase) indices. When the target
// group sits at a sibling index after the source (same parent), the erase shifts
// the group down by one, so the node lands in the wrong sibling (often a leaf —
// whose children never render — so the node "disappears" from the binder while
// its content file survives). The fix shifts the affected destination component
// down by one before resolving. This harness mirrors the move algorithm over a
// minimal tree and asserts the fixed path places the node correctly, AND that the
// buggy path misplaces it (so the test is proven to exercise the real defect).
/*
  # sandbox (this box)
  g++ -std=c++20 -Wall -Wextra move_node_test.cpp -o /tmp/move_test && /tmp/move_test

  # Fedora (Scott's box)
  clang++ -std=c++20 -Wall -Wextra tests/move_node_test.cpp -o /tmp/move_test && /tmp/move_test

  # expected: "move_node: 7 passed, 0 failed"
*/

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {

struct Node {
  std::string id;
  std::vector<Node> children;
};
using Tree = std::vector<Node>;

std::vector<Node> *children_of(Tree &root, const std::vector<int> &parent) {
  if (parent.empty())
    return &root;
  std::vector<Node> *vec = &root;
  Node *node = nullptr;
  for (int idx : parent) {
    if (idx < 0 || idx >= (int)vec->size())
      return nullptr;
    node = &(*vec)[idx];
    vec = &node->children;
  }
  return &node->children;
}

// FIXED move: adjusts the destination path for the source erase before resolving.
void move_fixed(Tree &root, const std::vector<int> &from,
                const std::vector<int> &to_parent, int index) {
  if (from.empty())
    return;
  std::vector<int> from_parent(from.begin(), from.end() - 1);
  int from_idx = from.back();
  auto *src = children_of(root, from_parent);
  if (!src || from_idx < 0 || from_idx >= (int)src->size())
    return;
  Node moving = std::move((*src)[from_idx]);
  src->erase(src->begin() + from_idx);

  std::vector<int> dst_parent = to_parent;
  if (dst_parent.size() > from_parent.size() &&
      std::equal(from_parent.begin(), from_parent.end(), dst_parent.begin()) &&
      dst_parent[from_parent.size()] > from_idx)
    --dst_parent[from_parent.size()];

  auto *dst = children_of(root, dst_parent);
  if (!dst) {
    src->insert(src->begin() + from_idx, std::move(moving));
    return;
  }
  index = std::clamp(index, 0, (int)dst->size());
  dst->insert(dst->begin() + index, std::move(moving));
}

// BUGGY move (the original): resolves to_parent with pre-erase indices.
void move_buggy(Tree &root, const std::vector<int> &from,
                const std::vector<int> &to_parent, int index) {
  if (from.empty())
    return;
  std::vector<int> from_parent(from.begin(), from.end() - 1);
  int from_idx = from.back();
  auto *src = children_of(root, from_parent);
  if (!src || from_idx < 0 || from_idx >= (int)src->size())
    return;
  Node moving = std::move((*src)[from_idx]);
  src->erase(src->begin() + from_idx);
  auto *dst = children_of(root, to_parent); // <-- stale indices
  if (!dst) {
    src->insert(src->begin() + from_idx, std::move(moving));
    return;
  }
  index = std::clamp(index, 0, (int)dst->size());
  dst->insert(dst->begin() + index, std::move(moving));
}

// Find a node by id; return its parent node's id ("" if at root, "?" if absent).
std::string parent_of(const Tree &root, const std::string &id,
                      const std::string &parent_id = "") {
  for (const auto &n : root) {
    if (n.id == id)
      return parent_id;
    std::string r = parent_of(n.children, id, n.id);
    if (r != "?")
      return r;
  }
  return "?";
}

int g_pass = 0, g_fail = 0;
void check(bool ok, const char *what) {
  if (ok) { ++g_pass; }
  else { ++g_fail; std::printf("  FAIL: %s\n", what); }
}

// A flat root: [mm, group, scene] — group sits AFTER mm (the failing direction).
Tree make_tree() {
  Tree t;
  t.push_back({"mm", {}});
  t.push_back({"group", {}});
  t.push_back({"scene", {}});
  return t;
}

} // namespace

int main() {
  // ── the bug, demonstrated: buggy move drops mm into the WRONG sibling ──
  {
    Tree t = make_tree();                       // [mm, group, scene]
    move_buggy(t, {0}, {1}, 0);                 // intend: mm into group(idx1)
    // after erase mm, group shifted to idx0, scene to idx1; to_parent {1} now hits scene
    check(parent_of(t, "mm") != "group", "buggy: mm did NOT land in group (bug reproduced)");
  }

  // ── the fix: mm lands in the group when the group is AFTER it ──
  {
    Tree t = make_tree();
    move_fixed(t, {0}, {1}, 0);                 // mm into group
    check(parent_of(t, "mm") == "group", "fixed: mm lands in group (group after source)");
    check(parent_of(t, "scene") == "", "fixed: scene untouched at root");
  }

  // ── regression: group BEFORE the source still works ──
  {
    Tree t;                                     // [group, mm]
    t.push_back({"group", {}});
    t.push_back({"mm", {}});
    move_fixed(t, {1}, {0}, 0);                 // mm into group(idx0)
    check(parent_of(t, "mm") == "group", "fixed: mm lands in group (group before source)");
  }

  // ── deeper: source before a nested target group, same shift hazard ──
  {
    Tree t;                                     // [leaf, outer[ inner[] ]]
    t.push_back({"leaf", {}});
    Node outer{"outer", {}};
    outer.children.push_back({"inner", {}});
    t.push_back(std::move(outer));
    // move leaf(idx0) into outer(idx1)->inner(idx0): to_parent {1,0}
    move_fixed(t, {0}, {1, 0}, 0);
    check(parent_of(t, "leaf") == "inner", "fixed: deep target path shifts correctly");
  }

  // ── same-parent reorder unaffected (caller pre-adjusts index; path unchanged) ──
  {
    Tree t;                                     // [a, b, c]
    t.push_back({"a", {}});
    t.push_back({"b", {}});
    t.push_back({"c", {}});
    move_fixed(t, {0}, {}, 2);                  // move a to end of root
    check(t.size() == 3 && t[2].id == "a", "fixed: same-parent reorder still correct");
  }

  // ── empty group after source: node lands inside, not dropped ──
  {
    Tree t = make_tree();
    move_fixed(t, {0}, {1}, 0);
    auto *g = children_of(t, {parent_of(t, "mm") == "group" ? 0 : 0});
    (void)g;
    check(parent_of(t, "mm") == "group", "fixed: empty group receives the node");
  }

  std::printf("\nmove_node: %d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
