#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — Importer.hpp
// Pure-logic import engine: TXT, Markdown, DOCX, ODT, RTF
// No GTK dependency.
// ─────────────────────────────────────────────────────────────────────────────
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Folio {

// ─── ImportOptions ────────────────────────────────────────────────────────────

struct ImportOptions {
    // ── Scene splitting ──────────────────────────────────────────────────────
    // Separator pattern used to split TXT / RTF / MD flat files into scenes.
    // Matched as a trimmed line (case-insensitive prefix match is NOT used —
    // exact trimmed-line equality).  Common values: "---", "* * *", "***", "#".
    // Empty = treat whole file as one scene.
    std::string separator = "---";

    // ── Markdown heading strategy ─────────────────────────────────────────────
    // When true: H1 lines become Group nodes, H2+ lines become Scene nodes.
    // When false: all heading lines are ignored (separator-only splitting).
    bool md_headings_as_hierarchy = true;

    // ── Title assumption ──────────────────────────────────────────────────────
    enum class TitleSource {
        FirstLine,   // use (and strip) the first non-empty text line
        Filename,    // derive from the filename (stem, spaces from underscores)
        Sequential,  // "Scene 1", "Scene 2", …
    };
    TitleSource title_source = TitleSource::FirstLine;

    // ── Folder-as-group ───────────────────────────────────────────────────────
    // When importing a folder: wrap all scenes in a Group named after the folder.
    bool folder_as_group = true;

    // ── High-fidelity conversion (s89) ────────────────────────────────────────
    // When true AND a LibreOffice binary (soffice/libreoffice) is on PATH, .docx
    // and .rtf files are first converted to .odt via `soffice --headless
    // --convert-to odt` and imported through the ODT path (which preserves
    // paragraph alignment + heading structure). Falls back silently to the
    // native parser when LibreOffice is absent or conversion fails. Almost every
    // Linux desktop ships LibreOffice, so this is the default for those formats.
    bool use_libreoffice = true;
};

// ─── ImportNode ───────────────────────────────────────────────────────────────

struct ImportNode {
    std::string title;
    std::string html;       // Folio internal HTML (paragraphs only)
    bool        is_group  = false;
    int         depth     = 0;  // 0 = top-level
};

// ─── ImportResult ─────────────────────────────────────────────────────────────

struct ImportResult {
    std::vector<ImportNode> nodes;
    std::string             error;   // non-empty on failure
    bool ok() const { return error.empty(); }
};

// ─── Importer ─────────────────────────────────────────────────────────────────

class Importer {
public:
    // Whether a LibreOffice binary (soffice/libreoffice) is on PATH — lets the
    // import dialog enable the high-fidelity .docx/.rtf conversion option. (s89)
    static bool libreoffice_available();

    // Import a single file.  Format is inferred from the extension.
    static ImportResult import_file(const std::string& path,
                                    const ImportOptions& opts = {});

    // Import every supported file inside a directory (non-recursive).
    // If opts.folder_as_group is true, the result begins with a Group node
    // whose title is the folder's basename.
    static ImportResult import_folder(const std::string& folder_path,
                                      const ImportOptions& opts = {});

    // Supported extensions (lower-case, including dot).
    static bool is_supported_extension(const std::string& ext);

private:
    // ── Per-format parsers (return flat text or structured nodes) ─────────────
    static ImportResult parse_txt(const std::string& text,
                                  const std::string& stem,
                                  const ImportOptions& opts);

    static ImportResult parse_md(const std::string& text,
                                 const std::string& stem,
                                 const ImportOptions& opts);

    static ImportResult parse_rtf(const std::string& rtf_bytes,
                                  const std::string& stem,
                                  const ImportOptions& opts);

    static ImportResult parse_docx(const std::vector<uint8_t>& zip_bytes,
                                   const std::string& stem,
                                   const ImportOptions& opts);

    static ImportResult parse_odt(const std::vector<uint8_t>& zip_bytes,
                                  const std::string& stem,
                                  const ImportOptions& opts);

    // ── Helpers ───────────────────────────────────────────────────────────────

    // Split plain text into scenes on separator lines; convert each chunk to
    // Folio HTML.  Applies title_source logic.
    static std::vector<ImportNode> split_plain(const std::string& plain,
                                               const std::string& stem,
                                               const ImportOptions& opts);

    // Convert a block of plain-text paragraphs to Folio internal HTML.
    static std::string plain_to_html(const std::string& plain);

    // Read a whole binary file into a byte vector.
    static std::vector<uint8_t> read_binary(const std::string& path);

    // Read a whole text file (UTF-8).
    static std::string read_text(const std::string& path);

    // Extract a named entry from a ZIP in memory.
    // Returns empty vector if not found.
    static std::vector<uint8_t> zip_extract_entry(const std::vector<uint8_t>& zip,
                                                   const std::string& entry_name);

    // Strip RTF control sequences → plain UTF-8 text.
    static std::string rtf_to_plain(const std::string& rtf);

    // ── LibreOffice high-fidelity conversion (s89) ────────────────────────────
    // Whether a LibreOffice binary is on PATH (for the import dialog to enable
    // the option). Returns the resolved program path, or "" if none found.
    static std::string libreoffice_program();
    // Split a search PATH string into directories (pure; sandbox-tested).
    static std::vector<std::string> split_search_path(const std::string& path);
    // Convert `input` to a temp .odt via the given LibreOffice program; returns
    // the produced .odt path (in a fresh temp dir the caller must clean up), or
    // "" on failure.
    static std::string convert_to_odt(const std::string& program,
                                      const std::string& input);
    // Try the convert→parse_odt path for a .docx/.rtf; std::nullopt → fall back
    // to the native parser. Cleans up its own temp dir.
    static std::optional<ImportResult> try_libreoffice_import(
        const std::string& path, const std::string& st, const ImportOptions& opts);

    // Parse word/document.xml → plain text (preserving paragraph breaks).
    static std::string docx_xml_to_plain(const std::string& xml);

    // Parse content.xml → plain text (preserving paragraph breaks).
    static std::string odt_xml_to_plain(const std::string& xml);

    // Derive a human-readable title from a filename stem.
    static std::string title_from_stem(const std::string& stem);

    // Return the lowercase extension of a path including the dot (".md" etc.)
    static std::string extension(const std::string& path);

    // Return the stem (filename without directory and extension).
    static std::string stem(const std::string& path);

    // Return the basename of a directory path.
    static std::string dir_basename(const std::string& path);
};

} // namespace Folio
