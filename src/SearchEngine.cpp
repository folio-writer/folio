// ─────────────────────────────────────────────────────────────────────────────
// Folio — SearchEngine.cpp
// ─────────────────────────────────────────────────────────────────────────────
#include "SearchEngine.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace Folio {

// ─────────────────────────────────────────────────────────────────────────────
// html_to_plain
// ─────────────────────────────────────────────────────────────────────────────

std::string SearchEngine::html_to_plain(const std::string& html) {
    std::string out;
    out.reserve(html.size());
    bool in_tag      = false;
    bool last_was_nl = false;
    std::string tag_buf;

    // Encode a Unicode codepoint as UTF-8
    auto cp_to_utf8 = [](unsigned long cp) -> std::string {
        std::string s;
        if      (cp < 0x80)    { s += (char)cp; }
        else if (cp < 0x800)   { s += (char)(0xC0|(cp>>6));  s += (char)(0x80|(cp&0x3F)); }
        else if (cp < 0x10000) { s += (char)(0xE0|(cp>>12)); s += (char)(0x80|((cp>>6)&0x3F)); s += (char)(0x80|(cp&0x3F)); }
        return s;
    };

    // Decode &name; &#decimal; &#xhex; entities
    auto decode_entity = [&](const std::string& e) -> std::string {
        if (e == "&amp;")  return "&";
        if (e == "&lt;")   return "<";
        if (e == "&gt;")   return ">";
        if (e == "&quot;") return "\"";
        if (e == "&apos;") return "'";
        if (e == "&nbsp;" || e == "&#160;") return "\xc2\xa0"; // NBSP as UTF-8
        if (e.size() > 3 && e[1] == '#') {
            try {
                unsigned long cp = 0;
                if (e[2] == 'x' || e[2] == 'X')
                    cp = std::stoul(e.substr(3, e.size()-4), nullptr, 16);
                else
                    cp = std::stoul(e.substr(2, e.size()-3));
                return cp_to_utf8(cp);
            } catch (...) {}
        }
        return ""; // unknown entity — drop rather than leave noise
    };

    size_t i = 0;
    while (i < html.size()) {
        unsigned char c = (unsigned char)html[i];

        if (c == '<') {
            in_tag = true;
            tag_buf.clear();
            ++i; continue;
        }

        if (c == '>') {
            in_tag = false;
            // Identify semantic block/break tags (lowercase, strip attributes)
            std::string tl = tag_buf;
            for (char& ch : tl) ch = (char)std::tolower((unsigned char)ch);
            auto sp = tl.find(' ');
            std::string tname = (sp != std::string::npos) ? tl.substr(0, sp) : tl;

            if (tname == "/p" || tname == "p" || tname == "br" || tname == "br/") {
                if (!last_was_nl) { out += '\n'; last_was_nl = true; }
            }
            tag_buf.clear();
            ++i; continue;
        }

        if (in_tag) { tag_buf += (char)c; ++i; continue; }

        // Entity decoding
        if (c == '&') {
            size_t semi = html.find(';', i);
            if (semi != std::string::npos && semi - i <= 10) {
                std::string decoded = decode_entity(html.substr(i, semi - i + 1));
                out += decoded;
                last_was_nl = (!decoded.empty() && decoded.back() == '\n');
                i = semi + 1;
                continue;
            }
        }

        out += (char)c;
        last_was_nl = (c == '\n');
        ++i;
    }

    while (!out.empty() && out.back() == '\n') out.pop_back();
    return out;
}
// ─────────────────────────────────────────────────────────────────────────────
// regex_escape
// ─────────────────────────────────────────────────────────────────────────────

std::string SearchEngine::regex_escape(const std::string& s) {
    static const std::string special = R"(\.+*?^${}()|[]/)";
    std::string out;
    out.reserve(s.size() * 2);
    for (char c : s) {
        if (special.find(c) != std::string::npos) out += '\\';
        out += c;
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// build_regex
// ─────────────────────────────────────────────────────────────────────────────

std::regex SearchEngine::build_regex(const std::string& query,
                                      const SearchOptions& opts) {
    std::regex_constants::syntax_option_type flags =
        std::regex_constants::ECMAScript;
    if (!opts.case_sensitive)
        flags |= std::regex_constants::icase;

    std::string pattern = opts.use_regex ? query : regex_escape(query);
    if (opts.whole_word)
        pattern = "\\b" + pattern + "\\b";

    return std::regex(pattern, flags);
}

// ─────────────────────────────────────────────────────────────────────────────
// make_context
// ─────────────────────────────────────────────────────────────────────────────

std::string SearchEngine::make_context(const std::string& text,
                                        int offset, int length,
                                        int context_chars) {
    int sz = (int)text.size();
    int lo = std::max(0, offset - context_chars);
    int hi = std::min(sz, offset + length + context_chars);

    std::string snippet;
    if (lo > 0) snippet += "\u2026"; // ellipsis prefix
    snippet += text.substr(lo, hi - lo);
    if (hi < sz) snippet += "\u2026"; // ellipsis suffix

    // Replace newlines with spaces for display
    for (char& c : snippet) if (c == '\n') c = ' ';
    return snippet;
}

// ─────────────────────────────────────────────────────────────────────────────
// search_plain
// ─────────────────────────────────────────────────────────────────────────────

std::vector<SearchMatch> SearchEngine::search_plain(
    const std::string&   text,
    const std::string&   query,
    const SearchOptions& opts)
{
    std::vector<SearchMatch> matches;
    if (query.empty() || text.empty()) return matches;

    // Build line-start offset table for fast line/col lookup
    std::vector<int> line_starts;
    line_starts.push_back(0);
    for (int i = 0; i < (int)text.size(); ++i)
        if (text[i] == '\n') line_starts.push_back(i + 1);

    auto line_col = [&](int offset) -> std::pair<int,int> {
        // Binary search for the line containing offset
        int lo = 0, hi = (int)line_starts.size() - 1;
        while (lo < hi) {
            int mid = (lo + hi + 1) / 2;
            if (line_starts[mid] <= offset) lo = mid;
            else hi = mid - 1;
        }
        return { lo + 1, offset - line_starts[lo] + 1 }; // 1-based
    };

    const int ctx = 30; // chars either side

    try {
        auto re = build_regex(query, opts);
        auto begin = std::sregex_iterator(text.begin(), text.end(), re);
        auto end   = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            SearchMatch m;
            m.offset = (int)it->position();
            m.length = (int)it->length();

            auto [ln, col] = line_col(m.offset);
            m.line = ln;
            m.col  = col;

            int sz  = (int)text.size();
            int lo  = std::max(0, m.offset - ctx);
            int hi  = std::min(sz, m.offset + m.length + ctx);

            auto clean = [](std::string s) {
                for (char& c : s) if (c == '\n') c = ' ';
                return s;
            };

            m.context_before = (lo > 0 ? "\u2026" : "") +
                               clean(text.substr(lo, m.offset - lo));
            m.context_match  = clean(text.substr(m.offset, m.length));
            m.context_after  = clean(text.substr(m.offset + m.length,
                                                  hi - (m.offset + m.length))) +
                               (hi < sz ? "\u2026" : "");
            m.context = m.context_before + m.context_match + m.context_after;

            matches.push_back(std::move(m));
        }
    } catch (const std::regex_error&) {}
    return matches;
}

// ─────────────────────────────────────────────────────────────────────────────
// search_html
// ─────────────────────────────────────────────────────────────────────────────

std::vector<SearchMatch> SearchEngine::search_html(
    const std::string&   html,
    const std::string&   query,
    const SearchOptions& opts)
{
    return search_plain(html_to_plain(html), query, opts);
}

// ─────────────────────────────────────────────────────────────────────────────
// replace_plain
// ─────────────────────────────────────────────────────────────────────────────

ReplaceResult SearchEngine::replace_plain(
    const std::string& text,
    const std::string& query,
    const std::string& replacement,
    const SearchOptions& opts)
{
    ReplaceResult r;
    if (query.empty()) return r;

    try {
        auto re = build_regex(query, opts);

        // Count matches first
        auto begin = std::sregex_iterator(text.begin(), text.end(), re);
        auto end   = std::sregex_iterator();
        r.replacements = (int)std::distance(begin, end);

        if (r.replacements > 0) {
            // std::regex_replace handles $1, $2 backreferences natively
            r.new_html = std::regex_replace(text, re, replacement);
        }
    } catch (const std::regex_error& e) {
        r.error = e.what();
    }
    return r;
}

// ─────────────────────────────────────────────────────────────────────────────
// replace_html
// Replace matches in the plain-text representation, then re-embed the
// changed text back into the HTML paragraph structure.
//
// Strategy: split HTML into <p>…</p> chunks, strip each to plain text,
// apply replacement, re-escape and re-wrap. Inline tags (bold, italic etc.)
// within a matched paragraph are dropped in the replaced paragraph — this
// is acceptable for a search/replace operation.
// ─────────────────────────────────────────────────────────────────────────────

ReplaceResult SearchEngine::replace_html(
    const std::string& html,
    const std::string& query,
    const std::string& replacement,
    const SearchOptions& opts)
{
    ReplaceResult r;
    if (query.empty()) return r;

    // Build regex once; validate before processing
    std::regex re;
    try {
        re = build_regex(query, opts);
    } catch (const std::regex_error& e) {
        r.error = e.what();
        return r;
    }

    // Escape plain text back into HTML text content.
    auto html_esc = [](const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (unsigned char c : s) {
            if      (c == '&') out += "&amp;";
            else if (c == '<') out += "&lt;";
            else if (c == '>') out += "&gt;";
            else               out += static_cast<char>(c);
        }
        return out;
    };
    // Decode the entities we re-emit, so matching runs against real characters.
    auto decode_basic = [](const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (std::size_t k = 0; k < s.size();) {
            if (s[k] == '&') {
                if      (s.compare(k, 5, "&amp;")  == 0) { out += '&';  k += 5; continue; }
                else if (s.compare(k, 4, "&lt;")   == 0) { out += '<';  k += 4; continue; }
                else if (s.compare(k, 4, "&gt;")   == 0) { out += '>';  k += 4; continue; }
                else if (s.compare(k, 6, "&quot;") == 0) { out += '"';  k += 6; continue; }
                else if (s.compare(k, 5, "&#39;")  == 0) { out += '\''; k += 5; continue; }
            }
            out += s[k++];
        }
        return out;
    };

    // Walk the HTML, replacing ONLY the text between tags. Every tag (paragraph
    // wrappers with their attributes, headings, outline/screenplay markers, and
    // inline bold/italic/etc.) is copied through verbatim, so formatting and
    // paragraph structure are preserved and no stray newline is ever introduced.
    // A match that straddles an inline-tag boundary won't be caught — an accepted
    // limitation, far better than flattening the paragraph to plain text.
    std::string out;
    out.reserve(html.size());
    std::string run;   // the current text run between tags

    auto flush_run = [&]() {
        if (run.empty()) return;
        const std::string decoded = decode_basic(run);
        int reps = 0;
        try {
            reps = static_cast<int>(std::distance(
                std::sregex_iterator(decoded.begin(), decoded.end(), re),
                std::sregex_iterator()));
        } catch (...) {
            reps = 0;
        }
        if (reps > 0) {
            std::string replaced;
            try {
                replaced = std::regex_replace(decoded, re, replacement);
            } catch (...) {
                out += run;          // regex_replace failed — leave the run as-is
                run.clear();
                return;
            }
            out += html_esc(replaced);
            r.replacements += reps;
        } else {
            out += run;              // unchanged — keep original escaping intact
        }
        run.clear();
    };

    for (std::size_t i = 0; i < html.size();) {
        if (html[i] == '<') {
            flush_run();
            const std::size_t et = html.find('>', i);
            if (et == std::string::npos) { out += html.substr(i); break; }
            out += html.substr(i, et - i + 1);   // copy the tag verbatim
            i = et + 1;
        } else {
            run += html[i++];
        }
    }
    flush_run();

    if (r.replacements > 0)
        r.new_html = out;

    return r;
}

// ─────────────────────────────────────────────────────────────────────────────
// search_tree  — recursive helper
// ─────────────────────────────────────────────────────────────────────────────

void SearchEngine::search_tree(
    const std::vector<BinderNode>& nodes,
    Section                        section,
    std::vector<int>               path_prefix,
    const std::string&             query,
    const SearchOptions&           opts,
    const std::regex&              re,
    std::vector<SearchResult>&     out)
{
    // Helper: tag matches with a field label and append to a vector
    auto tag_and_append = [](std::vector<SearchMatch>& dest,
                              std::vector<SearchMatch>  src,
                              const std::string&        field) {
        for (auto& m : src) { m.field = field; dest.push_back(std::move(m)); }
    };

    for (int i = 0; i < (int)nodes.size(); ++i) {
        const BinderNode& n = nodes[i];
        std::vector<int> path = path_prefix;
        path.push_back(i);

        SearchResult result;
        result.section = section;
        result.path    = path;
        result.title   = n.title;

        // Title
        if (opts.search_title) {
            auto tm = search_plain(n.title, query, opts);
            if (!tm.empty()) result.match_in_title = true;
        }

        // Body
        if (opts.search_body && !n.content.empty())
            tag_and_append(result.body_matches,
                           search_html(n.content, query, opts), "Body");

        // Synopsis
        if (opts.search_synopsis && !n.synopsis.empty())
            tag_and_append(result.body_matches,
                           search_plain(n.synopsis, query, opts), "Synopsis");

        // Notes
        if (opts.search_notes) {
            for (auto& note : n.notes) {
                std::string field = note.title.empty() ? "Note" : "Note: " + note.title;
                if (!note.title.empty())
                    tag_and_append(result.body_matches,
                                   search_plain(note.title, query, opts), field + " (title)");
                if (!note.body.empty())
                    tag_and_append(result.body_matches,
                                   search_plain(note.body, query, opts), field);
            }
        }

        // Description (characters/places)
        if (opts.search_description && !n.description.empty())
            tag_and_append(result.body_matches,
                           search_plain(n.description, query, opts), "Description");

        if (result.match_in_title || !result.body_matches.empty())
            out.push_back(std::move(result));

        // Recurse into children (groups)
        if (!n.children.empty())
            search_tree(n.children, section, path, query, opts, re, out);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// search  — public entry point
// ─────────────────────────────────────────────────────────────────────────────

std::vector<SearchResult> SearchEngine::search(
    const DocumentModel& model,
    const std::string&   query,
    const SearchOptions& opts)
{
    std::vector<SearchResult> results;
    if (query.empty()) return results;

    // Build regex once (validates pattern)
    std::regex re;
    try {
        re = build_regex(query, opts);
    } catch (const std::regex_error&) {
        return results; // invalid pattern
    }

    if (opts.sec_manuscript)
        search_tree(model.root(Section::Manuscript),  Section::Manuscript, {}, query, opts, re, results);
    if (opts.sec_characters)
        search_tree(model.root(Section::Characters),  Section::Characters, {}, query, opts, re, results);
    if (opts.sec_places)
        search_tree(model.root(Section::Places),      Section::Places,     {}, query, opts, re, results);
    if (opts.sec_references)
        search_tree(model.root(Section::References),  Section::References, {}, query, opts, re, results);
    if (opts.sec_templates)
        search_tree(model.root(Section::Templates),   Section::Templates,  {}, query, opts, re, results);

    return results;
}

} // namespace Folio
