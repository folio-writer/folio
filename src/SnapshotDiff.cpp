// ─────────────────────────────────────────────────────────────────────────────
// Folio — SnapshotDiff.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include <SnapshotDiff.hpp>
#include <algorithm>

namespace Folio {

// ─────────────────────────────────────────────────────────────────────────────
// html_to_plain
// ─────────────────────────────────────────────────────────────────────────────

std::string SnapshotDiff::html_to_plain(const std::string& html) {
    if (html.find('<') == std::string::npos) return html;
    std::string out;
    out.reserve(html.size());
    bool in_tag = false;
    for (size_t i = 0; i < html.size(); ++i) {
        if (html[i] == '<') { in_tag = true; continue; }
        if (html[i] == '>') {
            in_tag = false;
            if (i + 1 < html.size()) out += ' ';
            continue;
        }
        if (!in_tag) out += html[i];
    }

    // Collapse multiple spaces/newlines
    std::string result;
    result.reserve(out.size());
    bool prev_space = false;
    for (char c : out) {
        bool is_space = (c == ' ' || c == '\n' || c == '\r' || c == '\t');
        if (is_space) {
            if (!prev_space) result += ' ';
            prev_space = true;
        } else {
            result += c;
            prev_space = false;
        }
    }

    size_t s = result.find_first_not_of(' ');
    size_t e = result.find_last_not_of(' ');
    return (s == std::string::npos) ? "" : result.substr(s, e - s + 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// split_words
// ─────────────────────────────────────────────────────────────────────────────

std::vector<std::string> SnapshotDiff::split_words(const std::string& text) {
    std::vector<std::string> tokens;
    std::string cur;
    for (char c : text) {
        if (c == ' ' || c == '\n' || c == '\t') {
            if (!cur.empty()) { tokens.push_back(cur); cur.clear(); }
            tokens.push_back(std::string(1, c));
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) tokens.push_back(cur);
    return tokens;
}

// ─────────────────────────────────────────────────────────────────────────────
// compute — Myers LCS word diff
// ─────────────────────────────────────────────────────────────────────────────

std::vector<DiffOp> SnapshotDiff::compute(const std::vector<std::string>& a,
                                           const std::vector<std::string>& b) {
    int n = (int)a.size(), m = (int)b.size();

    // Guard against O(N²) stall on very large inputs
    if (n > 4000 || m > 4000) {
        std::string at, bt;
        for (auto& s : a) at += s;
        for (auto& s : b) bt += s;
        return { {DiffOp::Kind::Delete, at}, {DiffOp::Kind::Insert, bt} };
    }

    // LCS table
    std::vector<std::vector<int>> dp(n + 1, std::vector<int>(m + 1, 0));
    for (int i = 1; i <= n; ++i)
        for (int j = 1; j <= m; ++j)
            dp[i][j] = (a[i-1] == b[j-1])
                     ? dp[i-1][j-1] + 1
                     : std::max(dp[i-1][j], dp[i][j-1]);

    // Backtrack
    std::vector<DiffOp> ops;
    int i = n, j = m;
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 && a[i-1] == b[j-1]) {
            ops.push_back({DiffOp::Kind::Equal,  a[i-1]}); --i; --j;
        } else if (j > 0 && (i == 0 || dp[i][j-1] >= dp[i-1][j])) {
            ops.push_back({DiffOp::Kind::Insert, b[j-1]}); --j;
        } else {
            ops.push_back({DiffOp::Kind::Delete, a[i-1]}); --i;
        }
    }
    std::reverse(ops.begin(), ops.end());

    // Merge consecutive same-kind ops
    std::vector<DiffOp> merged;
    for (auto& op : ops) {
        if (!merged.empty() && merged.back().kind == op.kind)
            merged.back().text += op.text;
        else
            merged.push_back(op);
    }
    return merged;
}

} // namespace Folio
