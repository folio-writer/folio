#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ExportScan.hpp — export coverage scan (F-PDF, s15)
//
// Walks the HTML content of the nodes included in an export and tallies which
// formatting tags actually occur, split into the two tiers the renderer treats
// differently (paragraph-level vs inline run), plus any element OUTSIDE the
// known inventory (a renderer gap to surface before producing output).
//
// Pure logic, dependency-free (operates on the serialized HTML strings that
// EditorHtmlSerializer produces — see node content). The export flow supplies
// the node HTML; a thin adapter (not here) feeds Exporter::SourceNode.content
// into scan_export().
//
// The known marker set mirrors what EditorHtmlSerializer emits — keep in sync
// with that file if the serialized vocabulary changes.
// ─────────────────────────────────────────────────────────────────────────────

#include <array>
#include <string>
#include <vector>

namespace Folio {

// The closed formatting inventory. Order groups inline first, then paragraph;
// is_paragraph_level() encodes the tier.
enum class FormatTag {
    // ── inline (run-level) ──
    Bold = 0,
    Italic,
    Underline,
    Strikethrough,
    ForegroundColor,
    BackgroundColor,
    Font,
    Link,
    NamedStyle,
    // ── paragraph-level ──
    Alignment,
    LineHeight,
    IndentLeft,
    IndentRight,
    OutlineLevel,
    ScreenplayElement,
    Anchor,
    COUNT
};

inline constexpr int FORMAT_TAG_COUNT = static_cast<int>(FormatTag::COUNT);

// Marker substring as emitted by EditorHtmlSerializer, used to detect each tag.
inline const char* tag_marker(FormatTag t) {
    switch (t) {
        case FormatTag::Bold:              return "<b>";
        case FormatTag::Italic:            return "<i>";
        case FormatTag::Underline:         return "<u>";
        case FormatTag::Strikethrough:     return "<s>";
        case FormatTag::ForegroundColor:   return "\"color:";        // span style, color first
        case FormatTag::BackgroundColor:   return "background-color:";
        case FormatTag::Font:              return "font-family:";
        case FormatTag::Link:              return "data-folio-link";
        case FormatTag::NamedStyle:        return "data-folio-style";
        case FormatTag::Alignment:         return "text-align:";
        case FormatTag::LineHeight:        return "line-height:";
        case FormatTag::IndentLeft:        return "margin-left:";
        case FormatTag::IndentRight:       return "margin-right:";
        case FormatTag::OutlineLevel:      return "data-ol";
        case FormatTag::ScreenplayElement: return "data-sp";
        case FormatTag::Anchor:            return "data-anchor";
        default:                           return "";
    }
}

inline const char* tag_name(FormatTag t) {
    switch (t) {
        case FormatTag::Bold:              return "bold";
        case FormatTag::Italic:            return "italic";
        case FormatTag::Underline:         return "underline";
        case FormatTag::Strikethrough:     return "strikethrough";
        case FormatTag::ForegroundColor:   return "foreground colour";
        case FormatTag::BackgroundColor:   return "background colour";
        case FormatTag::Font:              return "font family/size";
        case FormatTag::Link:              return "internal link";
        case FormatTag::NamedStyle:        return "named style";
        case FormatTag::Alignment:         return "alignment";
        case FormatTag::LineHeight:        return "line height";
        case FormatTag::IndentLeft:        return "left indent";
        case FormatTag::IndentRight:       return "right indent";
        case FormatTag::OutlineLevel:      return "outline level (heading)";
        case FormatTag::ScreenplayElement: return "screenplay element";
        case FormatTag::Anchor:            return "anchor";
        default:                           return "?";
    }
}

inline bool is_paragraph_level(FormatTag t) {
    return static_cast<int>(t) >= static_cast<int>(FormatTag::Alignment);
}

// ─── Scan report ─────────────────────────────────────────────────────────────
struct ScanReport {
    std::array<int, FORMAT_TAG_COUNT> occurrences{};   // total marker hits
    std::array<int, FORMAT_TAG_COUNT> node_hits{};     // nodes containing the tag
    int nodes_scanned = 0;
    std::vector<std::string> unknown_elements;         // element names outside the inventory

    int count(FormatTag t) const { return occurrences[static_cast<int>(t)]; }
    bool present(FormatTag t) const { return occurrences[static_cast<int>(t)] > 0; }
};

namespace detail {

inline int count_occurrences(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return 0;
    int n = 0;
    std::size_t pos = 0;
    while ((pos = hay.find(needle, pos)) != std::string::npos) {
        ++n;
        pos += needle.size();
    }
    return n;
}

// Elements the renderer's inventory accounts for. Anything else is flagged.
inline bool is_known_element(const std::string& name) {
    static const char* known[] = {"p", "div", "span", "b", "i", "u", "s", "a", "br"};
    for (const char* k : known) if (name == k) return true;
    return false;
}

// Collect element names (opening tags) that are not in the known set.
inline void collect_unknown_elements(const std::string& html,
                                     std::vector<std::string>& out) {
    std::size_t i = 0;
    while ((i = html.find('<', i)) != std::string::npos) {
        std::size_t j = i + 1;
        if (j < html.size() && html[j] == '/') { ++i; continue; }   // closing tag
        std::string name;
        while (j < html.size() &&
               ((html[j] >= 'a' && html[j] <= 'z') ||
                (html[j] >= 'A' && html[j] <= 'Z'))) {
            name += static_cast<char>(html[j] | 0x20);  // lowercase
            ++j;
        }
        if (!name.empty() && !is_known_element(name)) {
            bool seen = false;
            for (const auto& u : out) if (u == name) { seen = true; break; }
            if (!seen) out.push_back(name);
        }
        i = j;
    }
}

}  // namespace detail

// Tally one node's HTML into the report.
inline void scan_node(const std::string& html, ScanReport& r) {
    ++r.nodes_scanned;
    for (int i = 0; i < FORMAT_TAG_COUNT; ++i) {
        int c = detail::count_occurrences(html, tag_marker(static_cast<FormatTag>(i)));
        if (c > 0) {
            r.occurrences[i] += c;
            r.node_hits[i]   += 1;
        }
    }
    detail::collect_unknown_elements(html, r.unknown_elements);
}

// Scan a set of included node HTML strings.
inline ScanReport scan_export(const std::vector<std::string>& node_htmls) {
    ScanReport r;
    for (const auto& h : node_htmls) scan_node(h, r);
    return r;
}

// Human-readable report (for a log line, a dialog, or a diagnostic dump).
inline std::string format_report(const ScanReport& r) {
    std::string out = "Export coverage scan: " + std::to_string(r.nodes_scanned) +
                      " node(s)\n";
    out += "  Paragraph-level tags present:\n";
    for (int i = 0; i < FORMAT_TAG_COUNT; ++i) {
        auto t = static_cast<FormatTag>(i);
        if (is_paragraph_level(t) && r.present(t))
            out += std::string("    - ") + tag_name(t) + " (" +
                   std::to_string(r.count(t)) + ")\n";
    }
    out += "  Inline (run-level) tags present:\n";
    for (int i = 0; i < FORMAT_TAG_COUNT; ++i) {
        auto t = static_cast<FormatTag>(i);
        if (!is_paragraph_level(t) && r.present(t))
            out += std::string("    - ") + tag_name(t) + " (" +
                   std::to_string(r.count(t)) + ")\n";
    }
    if (!r.unknown_elements.empty()) {
        out += "  UNKNOWN elements (outside renderer inventory):\n";
        for (const auto& u : r.unknown_elements) out += "    - <" + u + ">\n";
    } else {
        out += "  No unknown elements — full inventory coverage.\n";
    }
    return out;
}

}  // namespace Folio
