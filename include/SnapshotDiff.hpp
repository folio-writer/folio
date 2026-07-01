// ─────────────────────────────────────────────────────────────────────────────
// Folio — SnapshotDiff.hpp
// Self-contained word-level diff engine used by the snapshot review/diff
// dialogs.  Extracted from Inspector so it can be used and tested independently.
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include <string>
#include <vector>

namespace Folio {

// ─── DiffOp ──────────────────────────────────────────────────────────────────

struct DiffOp {
    enum class Kind { Equal, Insert, Delete } kind;
    std::string text;
};

// ─── DiffRow ─────────────────────────────────────────────────────────────────
// One aligned row of a side-by-side (split) diff. The left pane shows the OLD
// version, the right the NEW; equal rows carry both, a pure delete has a blank
// right (right_no == 0), a pure insert a blank left (left_no == 0). A Change row
// pairs an old paragraph with a new one and carries per-word ops so the UI can
// strike the removed words on the left and colour the added words on the right —
// left_ops is the old line as Equal|Delete tokens, right_ops the new line as
// Equal|Insert tokens. Line numbers are 1-based paragraph numbers; 0 means "no
// line on this side" (a filler cell).
struct DiffRow {
    enum class Kind { Equal, Delete, Insert, Change } kind;
    int         left_no  = 0;   // 1-based old paragraph number, or 0 (blank)
    int         right_no = 0;   // 1-based new paragraph number, or 0 (blank)
    std::string left;           // old paragraph text ("" for a pure insert)
    std::string right;          // new paragraph text ("" for a pure delete)
    std::vector<DiffOp> left_ops;   // Change rows: old line as Equal|Delete word tokens
    std::vector<DiffOp> right_ops;  // Change rows: new line as Equal|Insert word tokens
};

// ─── SnapshotDiff ─────────────────────────────────────────────────────────────

class SnapshotDiff {
public:
    // Strip HTML tags and collapse whitespace to get plain text for diffing.
    static std::string html_to_plain(const std::string& html);

    // Tokenise plain text into word + whitespace tokens.
    static std::vector<std::string> split_words(const std::string& text);

    // Myers LCS word-level diff of two token sequences.
    // Falls back to a single delete+insert for very large inputs (>4000 tokens).
    static std::vector<DiffOp> compute(const std::vector<std::string>& a,
                                       const std::vector<std::string>& b);

    // HTML → paragraphs ("lines" for the side-by-side view). Block boundaries
    // (</p>, <br>, headings, list items, …) become line breaks; inline tags are
    // stripped; common entities are decoded; intra-line whitespace is collapsed
    // and empty paragraphs dropped. This PRESERVES the paragraph structure that
    // html_to_plain deliberately flattens.
    static std::vector<std::string> html_to_lines(const std::string& html);

    // Paragraph-level aligned diff for the side-by-side view. LCS over whole
    // paragraphs; a run of deletes immediately followed by a run of inserts is
    // paired 1:1 into Change rows (each carrying intra-line word ops), the
    // overflow staying as pure delete/insert rows. Falls back to one big
    // delete+insert block past a large line count.
    static std::vector<DiffRow> diff_rows(const std::vector<std::string>& a,
                                          const std::vector<std::string>& b);
};

} // namespace Folio
