#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — SearchEngine.hpp
// Pure-logic search/replace engine. No GTK dependency.
// ─────────────────────────────────────────────────────────────────────────────
#include "DocumentModel.hpp"
#include <regex>
#include <string>
#include <vector>

namespace Folio {

// ─── SearchOptions ────────────────────────────────────────────────────────────

struct SearchOptions {
    bool case_sensitive = false;
    bool whole_word     = false;
    bool use_regex      = false;

    // Which fields to search
    bool search_title       = true;
    bool search_body        = true;
    bool search_synopsis    = true;
    bool search_notes       = true;
    bool search_description = true; // characters/places description field

    // Which sections to include
    bool sec_manuscript  = true;
    bool sec_characters  = true;
    bool sec_places      = true;
    bool sec_references  = true;
    bool sec_templates   = true;
};

// ─── SearchMatch ─────────────────────────────────────────────────────────────
// One match within a node's plain-text content.

struct SearchMatch {
    int  offset   = 0;   // byte offset in plain text
    int  length   = 0;   // byte length of match
    int  line     = 0;   // 1-based line number in plain text
    int  col      = 0;   // 1-based column (char offset on line)
    std::string field;          // which field: "Body", "Synopsis", "Note: X", "Description"
    std::string context;        // surrounding text snippet
    std::string context_before; // text before match (for bolding)
    std::string context_match;  // matched text
    std::string context_after;  // text after match
};

// ─── SearchResult ─────────────────────────────────────────────────────────────
// All matches within one node.

struct SearchResult {
    Section          section;
    std::vector<int> path;
    std::string      title;
    bool             match_in_title = false;
    std::vector<SearchMatch> body_matches; // matches in body plain text
};

// ─── ReplaceResult ───────────────────────────────────────────────────────────

struct ReplaceResult {
    int         replacements = 0;
    std::string new_html;   // updated HTML content (empty if no change)
    std::string error;      // non-empty if regex was invalid
};

// ─── SearchEngine ─────────────────────────────────────────────────────────────

class SearchEngine {
public:
    // Search the entire model. Returns one SearchResult per matching node.
    static std::vector<SearchResult> search(
        const DocumentModel& model,
        const std::string&   query,
        const SearchOptions& opts = {});

    // Search a single HTML string (body content of one node).
    // Returns matches in plain-text coordinates.
    static std::vector<SearchMatch> search_html(
        const std::string&   html,
        const std::string&   query,
        const SearchOptions& opts);

    // Search a plain-text string (title or already-stripped content).
    static std::vector<SearchMatch> search_plain(
        const std::string&   text,
        const std::string&   query,
        const SearchOptions& opts);

    // Replace all matches in one HTML string.
    // In regex mode, replacement may use $1, $2 … backreferences.
    // Returns new HTML and count of replacements made.
    static ReplaceResult replace_html(
        const std::string&   html,
        const std::string&   query,
        const std::string&   replacement,
        const SearchOptions& opts);

    // Replace all matches in a plain-text string.
    static ReplaceResult replace_plain(
        const std::string&   text,
        const std::string&   query,
        const std::string&   replacement,
        const SearchOptions& opts);

    // Strip HTML tags and decode basic entities → plain UTF-8.
    // Preserves paragraph breaks as newlines.
    static std::string html_to_plain(const std::string& html);

    // Build a std::regex from query + options.
    // Throws std::regex_error on invalid pattern.
    static std::regex build_regex(const std::string& query,
                                  const SearchOptions& opts);

    // Escape a plain string for use as a literal regex pattern.
    static std::string regex_escape(const std::string& s);

    // Extract a context snippet around a match in plain text.
    // context_chars: how many characters to include on each side.
    static std::string make_context(const std::string& text,
                                    int offset, int length,
                                    int context_chars = 60);

private:
    static void search_tree(const std::vector<BinderNode>& nodes,
                             Section section,
                             std::vector<int> path_prefix,
                             const std::string& query,
                             const SearchOptions& opts,
                             const std::regex& re,
                             std::vector<SearchResult>& out);
};

} // namespace Folio
