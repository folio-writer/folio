// ─────────────────────────────────────────────────────────────────────────────
// TEST_snapshot_diff.cpp — pure-layer tests for the side-by-side snapshot diff.
// Verifies html_to_lines (paragraph structure preserved) and diff_rows (aligned
// rows, 1-based line numbers, delete/insert/change pairing, similarity alignment,
// intra-line word ops). GTK-free; runs in the sandbox and under Fedora clang++.
// ─────────────────────────────────────────────────────────────────────────────
/*
  Build & run (sandbox g++):
    g++ -std=c++20 -Wall -Wextra -Wconversion -Wshadow -I../include
        TEST_snapshot_diff.cpp ../src/SnapshotDiff.cpp -o /tmp/tsd && /tmp/tsd

  Build & run (Fedora clang++):
    clang++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I../include
        TEST_snapshot_diff.cpp ../src/SnapshotDiff.cpp -o /tmp/tsd && /tmp/tsd
*/

#include <SnapshotDiff.hpp>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace Folio;

namespace {

int g_pass = 0, g_fail = 0;
void check(bool cond, const std::string& what) {
    if (cond) { ++g_pass; }
    else      { ++g_fail; std::cout << "  FAIL: " << what << "\n"; }
}

[[maybe_unused]] const char* kind_name(DiffRow::Kind k) {
    switch (k) {
        case DiffRow::Kind::Equal:  return "Equal ";
        case DiffRow::Kind::Delete: return "Delete";
        case DiffRow::Kind::Insert: return "Insert";
        case DiffRow::Kind::Change: return "Change";
    }
    return "?";
}

// Render left_ops / right_ops with [-del-] and {+ins+} markers so the intra-line
// word diff is visible in plain text.
std::string mark(const std::vector<DiffOp>& ops, const std::string& fallback) {
    if (ops.empty()) return fallback;
    std::string out;
    for (const auto& op : ops) {
        if      (op.kind == DiffOp::Kind::Delete) out += "[-" + op.text + "-]";
        else if (op.kind == DiffOp::Kind::Insert) out += "{+" + op.text + "+}";
        else                                      out += op.text;
    }
    return out;
}

std::string numcell(int no) {
    if (no == 0) return "   ";
    std::string s = std::to_string(no);
    while (s.size() < 3) s = " " + s;
    return s;
}

void dump_side_by_side(const std::vector<DiffRow>& rows) {
    std::cout << "  SNAPSHOT" << std::string(38, ' ') << "|  CURRENT\n";
    std::cout << "  " << std::string(44, '-') << "|" << std::string(44, '-') << "\n";
    for (const auto& r : rows) {
        std::string L = numcell(r.left_no)  + " " +
            (r.kind == DiffRow::Kind::Change ? mark(r.left_ops, r.left) : r.left);
        std::string R = numcell(r.right_no) + " " +
            (r.kind == DiffRow::Kind::Change ? mark(r.right_ops, r.right) : r.right);
        if (L.size() > 44) L = L.substr(0, 41) + "...";
        while (L.size() < 44) L += ' ';
        std::cout << "  " << L << "| " << R << "\n";
    }
}

// ── html_to_lines ────────────────────────────────────────────────────────────
void test_html_to_lines() {
    std::cout << "html_to_lines:\n";

    auto a = SnapshotDiff::html_to_lines("<p>First para.</p>\n<p>Second para.</p>");
    check(a.size() == 2, "two <p> → two lines");
    check(a[0] == "First para." && a[1] == "Second para.", "paragraph text extracted");

    auto b = SnapshotDiff::html_to_lines("<p>Line with <b>bold</b> and <i>italic</i> inline.</p>");
    check(b.size() == 1, "inline tags do NOT break the line");
    check(b[0] == "Line with bold and italic inline.", "inline tags stripped, one line");

    auto c = SnapshotDiff::html_to_lines("<p>Before<br>after break</p>");
    check(c.size() == 2, "<br> breaks a line");

    auto d = SnapshotDiff::html_to_lines("<p>Tom &amp; Jerry said &quot;hi&quot;</p>");
    check(d.size() == 1 && d[0] == "Tom & Jerry said \"hi\"", "entities decoded");

    auto e = SnapshotDiff::html_to_lines("<p>   spaced   out   </p><p></p><p>kept</p>");
    check(e.size() == 2, "whitespace collapsed; empty paragraph dropped");
    check(e[0] == "spaced out", "intra-line whitespace collapsed + trimmed");

    auto f = SnapshotDiff::html_to_lines("no tags at all, just text");
    check(f.size() == 1 && f[0] == "no tags at all, just text", "plain text → one line");

    std::cout << "\n";
}

// ── diff_rows: pure insert / delete / equal ──────────────────────────────────
void test_rows_basic() {
    std::cout << "diff_rows (basic alignment + line numbers):\n";

    std::vector<std::string> snap = {"Alpha", "Beta", "Gamma"};
    std::vector<std::string> curr = {"Alpha", "Beta", "Gamma"};
    auto same = SnapshotDiff::diff_rows(snap, curr);
    check(same.size() == 3, "identical → 3 equal rows");
    bool all_eq = true;
    for (const auto& r : same)
        if (r.kind != DiffRow::Kind::Equal) all_eq = false;
    check(all_eq, "all rows Equal");
    check(same[2].left_no == 3 && same[2].right_no == 3, "equal rows number both sides");

    // Insert a paragraph in the middle of Current.
    std::vector<std::string> s2 = {"Alpha", "Gamma"};
    std::vector<std::string> c2 = {"Alpha", "Beta", "Gamma"};
    auto ins = SnapshotDiff::diff_rows(s2, c2);
    check(ins.size() == 3, "one insertion → 3 rows");
    check(ins[1].kind == DiffRow::Kind::Insert, "middle row is Insert");
    check(ins[1].left_no == 0, "inserted row has blank left (no old line number)");
    check(ins[1].right_no == 2, "inserted row keeps the new line number");
    check(ins[2].left_no == 2 && ins[2].right_no == 3,
          "numbers stay independent per side after an insert");

    // Delete a paragraph from Current.
    std::vector<std::string> s3 = {"Alpha", "Beta", "Gamma"};
    std::vector<std::string> c3 = {"Alpha", "Gamma"};
    auto del = SnapshotDiff::diff_rows(s3, c3);
    check(del.size() == 3, "one deletion → 3 rows");
    check(del[1].kind == DiffRow::Kind::Delete, "middle row is Delete");
    check(del[1].left_no == 2 && del[1].right_no == 0, "deleted row: left numbered, right blank");

    std::cout << "\n";
}

// ── diff_rows: change pairing + intra-line word ops ──────────────────────────
void test_rows_change() {
    std::cout << "diff_rows (change pairing + intra-line ops):\n";

    std::vector<std::string> snap = {"The quick brown fox jumps."};
    std::vector<std::string> curr = {"The quick red fox leaps high."};
    auto rows = SnapshotDiff::diff_rows(snap, curr);
    check(rows.size() == 1, "one paragraph edited → a single Change row");
    check(rows[0].kind == DiffRow::Kind::Change, "row is Change");
    check(rows[0].left_no == 1 && rows[0].right_no == 1, "change row numbers both sides");

    // left_ops should contain "The quick" (equal) and "brown" (delete), no inserts.
    bool left_has_del = false, left_has_ins = false;
    for (const auto& op : rows[0].left_ops) {
        if (op.kind == DiffOp::Kind::Delete && op.text.find("brown") != std::string::npos)
            left_has_del = true;
        if (op.kind == DiffOp::Kind::Insert) left_has_ins = true;
    }
    check(left_has_del, "left_ops marks 'brown' as deleted");
    check(!left_has_ins, "left_ops carries NO inserts (old side only)");

    bool right_has_ins = false, right_has_del = false;
    for (const auto& op : rows[0].right_ops) {
        if (op.kind == DiffOp::Kind::Insert && op.text.find("red") != std::string::npos)
            right_has_ins = true;
        if (op.kind == DiffOp::Kind::Delete) right_has_del = true;
    }
    check(right_has_ins, "right_ops marks 'red' as inserted");
    check(!right_has_del, "right_ops carries NO deletes (new side only)");

    std::cout << "\n";
}

// ── overflow: unequal delete/insert run lengths ──────────────────────────────
void test_rows_overflow() {
    std::cout << "diff_rows (unequal run overflow):\n";

    // 1 old paragraph EDITED into 3, where exactly one new paragraph is clearly the
    // edited version of the old (high word overlap) → 1 Change + 2 pure Inserts.
    std::vector<std::string> snap = {"The hero walked into the room."};
    std::vector<std::string> curr = {"The hero strode into the bright room.",
                                     "He paused at the door.", "Then he left."};
    auto rows = SnapshotDiff::diff_rows(snap, curr);
    check(rows.size() == 3, "1→3 with one similar → 3 rows");
    check(rows[0].kind == DiffRow::Kind::Change, "the similar new paragraph pairs as Change");
    check(rows[1].kind == DiffRow::Kind::Insert && rows[2].kind == DiffRow::Kind::Insert,
          "the two unrelated new paragraphs are pure Inserts");
    check(rows[1].left_no == 0 && rows[2].left_no == 0, "overflow inserts: left blank");

    // Dissimilar 1→1 replacement must NOT be forced into a Change — it's a real
    // delete + insert (a paragraph swapped for an unrelated one).
    std::vector<std::string> s2 = {"Apples grow on trees."};
    std::vector<std::string> c2 = {"The economy contracted sharply."};
    auto r2 = SnapshotDiff::diff_rows(s2, c2);
    check(r2.size() == 2, "unrelated 1→1 → separate Delete + Insert (not a Change)");
    check(r2[0].kind == DiffRow::Kind::Delete && r2[1].kind == DiffRow::Kind::Insert,
          "unrelated swap stays Delete then Insert");

    std::cout << "\n";
}

// ── end-to-end: HTML in, aligned rows out, rendered ──────────────────────────
void test_end_to_end() {
    std::cout << "end-to-end (HTML snapshots → side-by-side):\n";

    std::string snapshot_html =
        "<p>Chapter One</p>"
        "<p>It was a bright cold day in April.</p>"
        "<p>The clocks were striking thirteen.</p>"
        "<p>Winston pushed open the glass doors.</p>";

    std::string current_html =
        "<p>Chapter One</p>"
        "<p>It was a bright cold day in April.</p>"
        "<p>Winston pushed open the heavy glass doors quickly.</p>"
        "<p>A new closing paragraph.</p>";

    auto snap_lines = SnapshotDiff::html_to_lines(snapshot_html);
    auto curr_lines = SnapshotDiff::html_to_lines(current_html);
    check(snap_lines.size() == 4, "snapshot → 4 paragraphs");
    check(curr_lines.size() == 4, "current → 4 paragraphs");

    auto rows = SnapshotDiff::diff_rows(snap_lines, curr_lines);
    // First two lines identical → Equal; the "clocks" line deleted; the "Winston"
    // line changed; a new closing paragraph inserted.
    check(rows[0].kind == DiffRow::Kind::Equal, "Chapter One equal");
    check(rows[1].kind == DiffRow::Kind::Equal, "April line equal");
    // Similarity alignment must NOT mispair the deleted 'clocks' line with the
    // edited 'Winston' line: 'clocks' is a pure Delete, 'Winston' a real Change.
    check(rows[2].kind == DiffRow::Kind::Delete && rows[2].right_no == 0,
          "'clocks' line is a pure Delete (blank right), not a mispaired Change");
    check(rows[3].kind == DiffRow::Kind::Change && rows[3].left_no == 4 && rows[3].right_no == 3,
          "'Winston' line is a Change, correctly numbered per side");
    check(rows[4].kind == DiffRow::Kind::Insert && rows[4].left_no == 0,
          "new closing paragraph is a pure Insert (blank left)");
    std::cout << "\n";

    dump_side_by_side(rows);
    std::cout << "\n";
}

}  // namespace

int main() {
    std::cout << "\n=== TEST_snapshot_diff ===\n\n";
    test_html_to_lines();
    test_rows_basic();
    test_rows_change();
    test_rows_overflow();
    test_end_to_end();

    std::cout << "──────────────────────────────\n";
    std::cout << "passed: " << g_pass << "   failed: " << g_fail << "\n";
    return g_fail == 0 ? 0 : 1;
}
