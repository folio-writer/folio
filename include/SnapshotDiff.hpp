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
};

} // namespace Folio
