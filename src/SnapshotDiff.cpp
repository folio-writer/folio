// ─────────────────────────────────────────────────────────────────────────────
// Folio — SnapshotDiff.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include <SnapshotDiff.hpp>
#include <algorithm>
#include <unordered_set>

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

// ─────────────────────────────────────────────────────────────────────────────
// html_to_lines  — HTML → paragraph list (preserves block structure)
// ─────────────────────────────────────────────────────────────────────────────
namespace {

// Block-level tags whose open/close boundary is a paragraph break. Inline tags
// (b, i, u, span, a, em, strong, …) are NOT here — they're stripped without a
// break so a bolded word stays on the same line.
bool is_block_tag(const std::string& name) {
    static const char* kBlock[] = {
        "p", "div", "br", "h1", "h2", "h3", "h4", "h5", "h6",
        "li", "ul", "ol", "blockquote", "pre", "hr", "tr", "table", "section"};
    for (const char* b : kBlock)
        if (name == b) return true;
    return false;
}

// Decode the handful of entities that actually show up in prose. Anything else is
// left verbatim (better than dropping it).
std::string decode_entities(const std::string& s) {
    static const std::pair<const char*, const char*> kEnt[] = {
        {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""},
        {"&#39;", "'"}, {"&apos;", "'"}, {"&nbsp;", " "}, {"&mdash;", "\xe2\x80\x94"},
        {"&ndash;", "\xe2\x80\x93"}, {"&hellip;", "\xe2\x80\xa6"},
        {"&lsquo;", "\xe2\x80\x98"}, {"&rsquo;", "\xe2\x80\x99"},
        {"&ldquo;", "\xe2\x80\x9c"}, {"&rdquo;", "\xe2\x80\x9d"}};
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] == '&') {
            bool hit = false;
            for (const auto& [ent, rep] : kEnt) {
                const std::size_t len = std::char_traits<char>::length(ent);
                if (s.compare(i, len, ent) == 0) {
                    out += rep;
                    i += len;
                    hit = true;
                    break;
                }
            }
            if (hit) continue;
        }
        out += s[i++];
    }
    return out;
}

// Collapse internal runs of whitespace to single spaces and trim the ends.
std::string collapse_trim(const std::string& s) {
    std::string t;
    t.reserve(s.size());
    bool prev_space = false;
    for (char c : s) {
        const bool sp = (c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
                         c == '\f' || c == '\v');
        if (sp) {
            if (!prev_space && !t.empty()) t += ' ';
            prev_space = true;
        } else {
            t += c;
            prev_space = false;
        }
    }
    while (!t.empty() && t.back() == ' ') t.pop_back();
    return t;
}

}  // namespace

std::vector<std::string> SnapshotDiff::html_to_lines(const std::string& html) {
    std::string work;
    work.reserve(html.size());
    for (std::size_t i = 0; i < html.size();) {
        if (html[i] == '<') {
            const std::size_t j = html.find('>', i);
            if (j == std::string::npos) break;  // malformed tail — stop
            // Extract the tag name (skip a leading '/').
            std::size_t p = i + 1;
            if (p < j && html[p] == '/') ++p;
            std::string name;
            while (p < j) {
                const char c = html[p];
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9'))
                    name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                else
                    break;
                ++p;
            }
            if (is_block_tag(name)) work += '\n';   // block boundary → line break
            i = j + 1;                              // (inline tags just vanish)
        } else {
            work += html[i++];
        }
    }
    work = decode_entities(work);

    std::vector<std::string> lines;
    std::string cur;
    auto flush = [&]() {
        const std::string t = collapse_trim(cur);
        if (!t.empty()) lines.push_back(t);
        cur.clear();
    };
    for (char c : work) {
        if (c == '\n') flush();
        else           cur += c;
    }
    flush();
    return lines;
}

// ─────────────────────────────────────────────────────────────────────────────
// diff_rows  — paragraph-level aligned diff for the side-by-side view
// ─────────────────────────────────────────────────────────────────────────────
namespace {

// Split a merged word-op list into the left column (Equal|Delete) and the right
// column (Equal|Insert), so a Change row can render the old line with deletions
// and the new line with insertions.
void split_change_ops(const std::vector<DiffOp>& ops, std::vector<DiffOp>& left,
                      std::vector<DiffOp>& right) {
    for (const auto& op : ops) {
        if (op.kind == DiffOp::Kind::Equal) {
            left.push_back(op);
            right.push_back(op);
        } else if (op.kind == DiffOp::Kind::Delete) {
            left.push_back(op);
        } else {  // Insert
            right.push_back(op);
        }
    }
}

// Word-overlap (Jaccard) similarity of two paragraphs, ignoring whitespace
// tokens. Used to decide whether a deleted line and an inserted line are "the
// same paragraph, edited" (→ a Change row) or two unrelated paragraphs.
double line_similarity(const std::string& x, const std::string& y) {
    std::unordered_set<std::string> sx, sy;
    for (auto& w : SnapshotDiff::split_words(x))
        if (w != " " && w != "\t" && w != "\n") sx.insert(w);
    for (auto& w : SnapshotDiff::split_words(y))
        if (w != " " && w != "\t" && w != "\n") sy.insert(w);
    if (sx.empty() && sy.empty()) return 1.0;
    if (sx.empty() || sy.empty()) return 0.0;
    std::size_t inter = 0;
    for (const auto& w : sx)
        if (sy.count(w)) ++inter;
    const std::size_t uni = sx.size() + sy.size() - inter;
    return static_cast<double>(inter) / static_cast<double>(uni);
}

// The similarity floor for treating a delete/insert pair as one edited paragraph
// rather than an unrelated delete + insert. Below this they align to their own
// rows (blank on the other side).
constexpr double kPairFloor = 0.34;

// Align a run of deleted paragraphs against a run of inserted ones, emitting rows
// in reading order. A secondary similarity-weighted LCS pairs the paragraphs that
// are genuinely the same-edited (→ Change rows with intra-line word ops); the
// unmatched deletes become Delete rows and unmatched inserts Insert rows. This is
// what stops "line 3 deleted" from being mispaired with "line 3 edited".
void emit_change_block(const std::vector<int>& dels, const std::vector<int>& inss,
                       const std::vector<std::string>& a,
                       const std::vector<std::string>& b,
                       std::vector<DiffRow>& rows) {
    const std::size_t D = dels.size(), I = inss.size();

    auto pure_delete = [&](int ai) {
        rows.push_back({DiffRow::Kind::Delete, ai + 1, 0,
                        a[static_cast<std::size_t>(ai)], "", {}, {}});
    };
    auto pure_insert = [&](int bj) {
        rows.push_back({DiffRow::Kind::Insert, 0, bj + 1, "",
                        b[static_cast<std::size_t>(bj)], {}, {}});
    };
    auto emit_change = [&](int ai, int bj) {
        const std::string& lt = a[static_cast<std::size_t>(ai)];
        const std::string& rt = b[static_cast<std::size_t>(bj)];
        DiffRow row{DiffRow::Kind::Change, ai + 1, bj + 1, lt, rt, {}, {}};
        split_change_ops(SnapshotDiff::compute(SnapshotDiff::split_words(lt),
                                               SnapshotDiff::split_words(rt)),
                         row.left_ops, row.right_ops);
        rows.push_back(row);
    };

    if (D == 0) { for (int bj : inss) pure_insert(bj); return; }
    if (I == 0) { for (int ai : dels) pure_delete(ai); return; }

    // Pathological block size → fall back to positional pairing (bounded work).
    if (D * I > 4000) {
        const std::size_t paired = std::min(D, I);
        for (std::size_t p = 0; p < paired; ++p) emit_change(dels[p], inss[p]);
        for (std::size_t p = paired; p < D; ++p) pure_delete(dels[p]);
        for (std::size_t p = paired; p < I; ++p) pure_insert(inss[p]);
        return;
    }

    // sim[i][j] = similarity of dels[i] vs inss[j] when it clears the floor, else 0.
    std::vector<std::vector<double>> sim(D, std::vector<double>(I, 0.0));
    for (std::size_t i = 0; i < D; ++i)
        for (std::size_t j = 0; j < I; ++j) {
            const double s = line_similarity(a[static_cast<std::size_t>(dels[i])],
                                             b[static_cast<std::size_t>(inss[j])]);
            sim[i][j] = (s >= kPairFloor) ? s : 0.0;
        }

    // Similarity-weighted LCS: maximise total paired similarity while keeping the
    // deletes and inserts in order (a pairing may not cross another pairing).
    std::vector<std::vector<double>> dp(D + 1, std::vector<double>(I + 1, 0.0));
    for (std::size_t i = 1; i <= D; ++i)
        for (std::size_t j = 1; j <= I; ++j) {
            double best = std::max(dp[i - 1][j], dp[i][j - 1]);
            if (sim[i - 1][j - 1] > 0.0)
                best = std::max(best, dp[i - 1][j - 1] + sim[i - 1][j - 1]);
            dp[i][j] = best;
        }

    // Backtrack forward: reconstruct the ordered sequence of pair / delete / insert.
    struct Act { int ai; int bj; };  // bj == -1 → pure delete; ai == -1 → pure insert
    std::vector<Act> acts;
    {
        std::size_t i = D, j = I;
        while (i > 0 && j > 0) {
            if (sim[i - 1][j - 1] > 0.0 &&
                dp[i][j] == dp[i - 1][j - 1] + sim[i - 1][j - 1]) {
                acts.push_back({dels[i - 1], inss[j - 1]});  // pair
                --i; --j;
            } else if (dp[i][j - 1] >= dp[i - 1][j]) {
                acts.push_back({-1, inss[j - 1]});           // insert (prefer on tie)
                --j;
            } else {
                acts.push_back({dels[i - 1], -1});           // delete
                --i;
            }
        }
        while (i > 0) { acts.push_back({dels[i - 1], -1}); --i; }
        while (j > 0) { acts.push_back({-1, inss[j - 1]}); --j; }
        std::reverse(acts.begin(), acts.end());
    }

    for (const auto& act : acts) {
        if (act.ai >= 0 && act.bj >= 0) emit_change(act.ai, act.bj);
        else if (act.ai >= 0)           pure_delete(act.ai);
        else                            pure_insert(act.bj);
    }
}

}  // namespace

std::vector<DiffRow> SnapshotDiff::diff_rows(const std::vector<std::string>& a,
                                             const std::vector<std::string>& b) {
    const int n = static_cast<int>(a.size());
    const int m = static_cast<int>(b.size());
    std::vector<DiffRow> rows;

    // Guard: past a large paragraph count, skip the O(L^2) LCS and show the whole
    // thing as one removed block then one added block (still correct, just coarse).
    if (n > 2000 || m > 2000) {
        for (int i = 0; i < n; ++i)
            rows.push_back({DiffRow::Kind::Delete, i + 1, 0, a[static_cast<std::size_t>(i)], "", {}, {}});
        for (int j = 0; j < m; ++j)
            rows.push_back({DiffRow::Kind::Insert, 0, j + 1, "", b[static_cast<std::size_t>(j)], {}, {}});
        return rows;
    }

    // LCS over whole paragraphs.
    std::vector<std::vector<int>> dp(static_cast<std::size_t>(n + 1),
                                     std::vector<int>(static_cast<std::size_t>(m + 1), 0));
    for (int i = 1; i <= n; ++i)
        for (int j = 1; j <= m; ++j)
            dp[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] =
                (a[static_cast<std::size_t>(i - 1)] == b[static_cast<std::size_t>(j - 1)])
                    ? dp[static_cast<std::size_t>(i - 1)][static_cast<std::size_t>(j - 1)] + 1
                    : std::max(dp[static_cast<std::size_t>(i - 1)][static_cast<std::size_t>(j)],
                               dp[static_cast<std::size_t>(i)][static_cast<std::size_t>(j - 1)]);

    // Backtrack into a forward line-op list of {kind, ai, bj}.
    struct LineOp { DiffRow::Kind kind; int ai; int bj; };
    std::vector<LineOp> ops;
    {
        int i = n, j = m;
        while (i > 0 || j > 0) {
            if (i > 0 && j > 0 &&
                a[static_cast<std::size_t>(i - 1)] == b[static_cast<std::size_t>(j - 1)]) {
                ops.push_back({DiffRow::Kind::Equal, i - 1, j - 1});
                --i; --j;
            } else if (j > 0 && (i == 0 ||
                       dp[static_cast<std::size_t>(i)][static_cast<std::size_t>(j - 1)] >=
                       dp[static_cast<std::size_t>(i - 1)][static_cast<std::size_t>(j)])) {
                ops.push_back({DiffRow::Kind::Insert, -1, j - 1});
                --j;
            } else {
                ops.push_back({DiffRow::Kind::Delete, i - 1, -1});
                --i;
            }
        }
        std::reverse(ops.begin(), ops.end());
    }

    // Walk the line-ops. Equal → one aligned row. A maximal run of Deletes that is
    // immediately followed by a run of Inserts is PAIRED 1:1 into Change rows (each
    // carrying intra-line word ops); the overflow stays as pure delete/insert rows.
    std::size_t k = 0;
    while (k < ops.size()) {
        if (ops[k].kind == DiffRow::Kind::Equal) {
            const std::string& t = a[static_cast<std::size_t>(ops[k].ai)];
            rows.push_back({DiffRow::Kind::Equal, ops[k].ai + 1, ops[k].bj + 1, t, t, {}, {}});
            ++k;
            continue;
        }
        // Gather the delete run, then the insert run that abuts it, and align them
        // by similarity (pairs the same-edited paragraphs; leaves the rest as pure
        // delete/insert rows in reading order).
        std::vector<int> dels, inss;
        while (k < ops.size() && ops[k].kind == DiffRow::Kind::Delete)
            dels.push_back(ops[k++].ai);
        while (k < ops.size() && ops[k].kind == DiffRow::Kind::Insert)
            inss.push_back(ops[k++].bj);

        emit_change_block(dels, inss, a, b, rows);
    }
    return rows;
}

} // namespace Folio
