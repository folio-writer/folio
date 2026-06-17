#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — Exporter.hpp
// Pure-logic export engine: TXT, RTF, HTML, Markdown, EPUB
// No GTK dependency — can be tested independently.
// ─────────────────────────────────────────────────────────────────────────────
#include <cstdint>
#include <string>
#include <vector>

namespace Folio {

// ─── ExportOptions ────────────────────────────────────────────────────────────
struct ExportOptions {
    // Format
    enum class Format { TXT, RTF, HTML, Markdown, EPUB, DOCX, ODT } format = Format::RTF;

    // Output mode
    enum class Mode { Combined, Zipped } mode = Mode::Combined;

    // Scene separator (inserted between scenes, not after the last one)
    std::string scene_separator = "* * *";
    bool        separator_own_line = true;   // wrap separator in blank lines

    // Group handling
    bool page_break_on_group    = true;   // page break before group heading
    // Group heading style
    enum class GroupHeadingStyle {
        AsIs,           // use group title as-is
        AutoNumber,     // "Chapter 1" (ignores title)
        AutoNumberTitle,// "Chapter 1: The Beginning"
        NoHeading       // no heading, content only
    };
    GroupHeadingStyle group_heading_style = GroupHeadingStyle::AsIs;
    std::string       group_heading_word  = "Chapter"; // Chapter/Part/Book/Section
    bool include_group_content  = true;   // render group body text

    // First-line indent (from prefs)
    bool first_line_indent    = false;
    int  first_line_indent_px = 32;          // pixels → converted to twips on export

    // Flatten formatting (RTF only — overrides per-para styles)
    bool        flatten         = false;
    std::string flatten_font    = "Times New Roman";
    int         flatten_size_pt = 12;        // points
    double      flatten_line_spacing = 2.0;  // multiplier
    int         flatten_margin_pt    = 72;   // left/right margin in points (1 inch)
    int         flatten_para_space_pt = 0;   // space-after paragraph in points

    // Cover image (EPUB only)
    bool        include_cover        = true;
    std::string cover_thumbnail_b64; // base64 PNG — from DocumentModel::cover_thumbnail
};

// ─── ExportEntry ─────────────────────────────────────────────────────────────
// A single file to write into the ZIP, or the combined output.
struct ExportEntry {
    std::string filename; // e.g. "01_Prologue.rtf"
    std::string content;  // raw bytes
};

// ─── Exporter ────────────────────────────────────────────────────────────────
// Each SourceNode represents a scene or group to export.
class Exporter {
public:
    struct SourceNode {
        std::string title;
        std::string html_content;
        bool        is_group = false;
        int         depth    = 0;    // nesting depth (0 = top-level)
    };
    // Convert a list of SourceNodes to a single combined string (TXT or RTF).
    static std::string compile_combined(const std::vector<SourceNode>& nodes,
                                        const ExportOptions& opts);

    // Convert a list of SourceNodes to per-scene entries.
    // Groups produce no entry of their own; their title is used as a
    // filename prefix for the scenes inside them.
    static std::vector<ExportEntry> compile_entries(
        const std::vector<SourceNode>& nodes,
        const ExportOptions& opts);

    // Write a collection of entries into a ZIP archive at `path`.
    // Returns empty string on success, error message on failure.
    static std::string write_zip(const std::vector<ExportEntry>& entries,
                                 const std::string& path);

    // Convenience: strip HTML tags, decode basic entities → plain text.
    static std::string html_to_plain(const std::string& html);

    // Convert HTML content to RTF body (inline bold/italic/underline preserved).
    static std::string html_to_rtf_body(const std::string& html,
                                        const ExportOptions& opts);

    // Convert HTML content to Markdown inline markup.
    static std::string html_to_markdown(const std::string& html);

    // Convert HTML content to clean XHTML body paragraphs (for HTML/EPUB).
    static std::string html_to_xhtml_body(const std::string& html);

    // Build a standalone HTML document from nodes.
    static std::string compile_html(const std::vector<SourceNode>& nodes,
                                    const ExportOptions& opts,
                                    const std::string& title = "");

    // Build a Markdown document from nodes.
    static std::string compile_markdown(const std::vector<SourceNode>& nodes,
                                        const ExportOptions& opts);

    // Build a complete EPUB 3 ZIP archive, returned as raw bytes.
    static std::vector<uint8_t> compile_epub(const std::vector<SourceNode>& nodes,
                                              const ExportOptions& opts,
                                              const std::string& title = "",
                                              const std::string& author = "");

    // Build a complete DOCX ZIP archive, returned as raw bytes.
    static std::vector<uint8_t> compile_docx(const std::vector<SourceNode>& nodes,
                                              const ExportOptions& opts,
                                              const std::string& title = "",
                                              const std::string& author = "");

    // Build a complete ODT ZIP archive, returned as raw bytes.
    static std::vector<uint8_t> compile_odt(const std::vector<SourceNode>& nodes,
                                             const ExportOptions& opts,
                                             const std::string& title = "",
                                             const std::string& author = "");

private:
    // RTF helpers
    static std::string rtf_header(const ExportOptions& opts);
    static std::string rtf_escape(const std::string& s);
    static std::string sanitise_filename(const std::string& title, int idx,
                                         const std::string& ext);
};

} // namespace Folio
