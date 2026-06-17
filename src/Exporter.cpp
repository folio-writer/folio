// ─────────────────────────────────────────────────────────────────────────────
// Folio — Exporter.cpp
// ─────────────────────────────────────────────────────────────────────────────
#include "Exporter.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <glibmm/base64.h>
#include <sstream>
#include <zlib.h>

namespace Folio {

// ─────────────────────────────────────────────────────────────────────────────
// html_to_plain  — strip tags, decode basic entities
// ─────────────────────────────────────────────────────────────────────────────
std::string Exporter::html_to_plain(const std::string& html) {
    std::string out;
    out.reserve(html.size());
    bool in_tag = false;
    bool last_was_newline = false;

    auto decode_entity = [](const std::string& e) -> std::string {
        if (e == "&amp;")  return "&";
        if (e == "&lt;")   return "<";
        if (e == "&gt;")   return ">";
        if (e == "&quot;") return "\"";
        if (e == "&apos;") return "'";
        if (e == "&#160;" || e == "&nbsp;") return " ";
        return e; // unknown — keep as-is
    };

    size_t i = 0;
    while (i < html.size()) {
        char c = html[i];
        if (c == '<') {
            // Check for </p> or <p> — convert to newlines
            if (i + 3 < html.size() && html.substr(i, 4) == "</p>") {
                if (!last_was_newline) { out += '\n'; last_was_newline = true; }
                i += 4; continue;
            }
            in_tag = true; ++i; continue;
        }
        if (c == '>') { in_tag = false; ++i; continue; }
        if (in_tag) { ++i; continue; }

        // Entity
        if (c == '&') {
            size_t semi = html.find(';', i);
            if (semi != std::string::npos && semi - i <= 8) {
                out += decode_entity(html.substr(i, semi - i + 1));
                i = semi + 1;
                last_was_newline = false;
                continue;
            }
        }
        out += c;
        last_was_newline = (c == '\n');
        ++i;
    }
    // Trim trailing newlines
    while (!out.empty() && out.back() == '\n') out.pop_back();
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// RTF helpers
// ─────────────────────────────────────────────────────────────────────────────

std::string Exporter::rtf_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() * 2);

    // Decode UTF-8 to codepoints
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];

        // ASCII range
        if (c < 0x80) {
            if      (c == '\\') out += "\\\\";
            else if (c == '{')  out += "\\{";
            else if (c == '}')  out += "\\}";
            else                out += (char)c;
            ++i; continue;
        }

        // Decode UTF-8 codepoint
        uint32_t cp = 0;
        int bytes = 0;
        if      ((c & 0xE0) == 0xC0) { cp = c & 0x1F; bytes = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; bytes = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; bytes = 4; }
        else { out += '?'; ++i; continue; } // invalid byte

        for (int b = 1; b < bytes && i + b < s.size(); ++b)
            cp = (cp << 6) | ((unsigned char)s[i + b] & 0x3F);
        i += bytes;

        // Map common typographic characters to RTF named sequences
        // These are universally supported and avoid \uN? rendering issues
        switch (cp) {
            // Smart quotes
            case 0x2018: out += "\\'91"; break;  // left single quotation mark  '
            case 0x2019: out += "\\'92"; break;  // right single quotation mark '
            case 0x201C: out += "\\'93"; break;  // left double quotation mark  "
            case 0x201D: out += "\\'94"; break;  // right double quotation mark "
            case 0x201A: out += "\\'82"; break;  // single low-9 quotation mark ‚
            case 0x201E: out += "\\'84"; break;  // double low-9 quotation mark „
            // Dashes
            case 0x2013: out += "\\'96"; break;  // en dash –
            case 0x2014: out += "\\'97"; break;  // em dash —
            // Ellipsis
            case 0x2026: out += "\\'85"; break;  // horizontal ellipsis …
            // Spaces
            case 0x00A0: out += "\\~";   break;  // non-breaking space
            case 0x2009: out += " ";     break;  // thin space → regular space
            // Bullets
            case 0x2022: out += "\\'95"; break;  // bullet •
            // All other non-ASCII: RTF Unicode escape with ANSI fallback
            default:
                if (cp <= 0x7FFF) {
                    out += "\\u" + std::to_string((int32_t)cp) + "?";
                } else {
                    // RTF \u takes signed 16-bit; for codepoints > 32767 use negative
                    out += "\\u" + std::to_string((int32_t)cp - 65536) + "?";
                }
                break;
        }
    }
    return out;
}

std::string Exporter::rtf_header(const ExportOptions& opts) {
    std::string font = opts.flatten ? opts.flatten_font : "Times New Roman";
    int size_hpt = (opts.flatten ? opts.flatten_size_pt : 12) * 2; // half-points

    // Margin in twips (1 pt = 20 twips, 1 inch = 1440 twips)
    int margin_tw = opts.flatten ? opts.flatten_margin_pt * 20 : 1440;

    std::string hdr;
    hdr += "{\\rtf1\\ansi\\ansicpg1252\\deff0\n";
    hdr += "{\\fonttbl{\\f0\\froman\\fcharset0 " + rtf_escape(font) + ";}}\n";
    hdr += "{\\colortbl;\\red0\\green0\\blue0;}\n";
    hdr += "\\widowctrl\\hyphauto\n";
    // Page margins
    hdr += "\\margl" + std::to_string(margin_tw) +
           "\\margr" + std::to_string(margin_tw) +
           "\\margt1440\\margb1440\n";
    // Default paragraph formatting
    if (opts.flatten) {
        int space_tw = opts.flatten_para_space_pt * 20;
        int sl = (int)(opts.flatten_line_spacing * 240.0);
        int fi_tw = (opts.first_line_indent && opts.first_line_indent_px > 0)
                    ? opts.first_line_indent_px * 15 : 0;
        hdr += "\\f0\\fs" + std::to_string(size_hpt) +
               "\\sl" + std::to_string(sl) + "\\slmult1" +
               (fi_tw ? "\\fi" + std::to_string(fi_tw) : "") +
               "\\sa" + std::to_string(space_tw) + "\n";
    } else {
        int fi_tw = (opts.first_line_indent && opts.first_line_indent_px > 0)
                    ? opts.first_line_indent_px * 15 : 0;
        hdr += "\\f0\\fs24\\sl480\\slmult1" +
               (fi_tw ? "\\fi" + std::to_string(fi_tw) : "") + "\n";
    }
    return hdr;
}

// ─────────────────────────────────────────────────────────────────────────────
// html_to_rtf_body  — convert HTML content to RTF paragraph runs
// ─────────────────────────────────────────────────────────────────────────────
std::string Exporter::html_to_rtf_body(const std::string& html,
                                         const ExportOptions& opts) {
    if (html.empty()) return "\\par\n";

    std::string out;
    bool bold = false, italic = false, underline = false, strike = false;
    bool in_tag = false;
    std::string tag_buf;
    std::string text_buf;

    auto flush_text = [&]() {
        if (!text_buf.empty()) {
            // Decode entities then RTF-escape
            std::string plain = html_to_plain("<p>" + text_buf + "</p>");
            out += rtf_escape(plain);
            text_buf.clear();
        }
    };

    // First-line indent in twips (1px ≈ 15 twips at 96dpi/72pt)
    std::string fi_str;
    if (opts.first_line_indent && opts.first_line_indent_px > 0)
        fi_str = "\\fi" + std::to_string(opts.first_line_indent_px * 15);

    auto open_para = [&]() {
        out += "\\pard\\sl480\\slmult1" + fi_str + " ";
    };
    auto close_para = [&]() {
        flush_text();
        // Close any open inline formatting
        if (bold)      { out += "\\b0 "; bold = false; }
        if (italic)    { out += "\\i0 "; italic = false; }
        if (underline) { out += "\\ul0 "; underline = false; }
        if (strike)    { out += "\\strike0 "; strike = false; }
        out += "\\par\n";
    };

    [[maybe_unused]] bool first_para = true;
    size_t i = 0;
    bool in_para = false;

    while (i < html.size()) {
        char c = html[i];
        if (c == '<') {
            in_tag = true;
            tag_buf.clear();
            ++i; continue;
        }
        if (c == '>') {
            in_tag = false;
            // Process tag
            std::string t = tag_buf;
            // Lowercase
            for (auto& ch : t) ch = (char)std::tolower((unsigned char)ch);
            bool closing = (!t.empty() && t[0] == '/');
            if (closing) t = t.substr(1);
            // Strip attributes for tag name
            std::string tname = t.substr(0, t.find(' '));

            if (tname == "p") {
                if (!closing) {
                    if (in_para) close_para();
                    open_para();
                    in_para = true;
                    first_para = false;
                } else {
                    if (in_para) close_para();
                    in_para = false;
                }
            } else if (tname == "b" || tname == "strong") {
                flush_text();
                if (!closing) { out += "\\b "; bold = true; }
                else          { out += "\\b0 "; bold = false; }
            } else if (tname == "i" || tname == "em") {
                flush_text();
                if (!closing) { out += "\\i "; italic = true; }
                else          { out += "\\i0 "; italic = false; }
            } else if (tname == "u") {
                flush_text();
                if (!closing) { out += "\\ul "; underline = true; }
                else          { out += "\\ul0 "; underline = false; }
            } else if (tname == "s") {
                flush_text();
                if (!closing) { out += "\\strike "; strike = true; }
                else          { out += "\\strike0 "; strike = false; }
            }
            // Ignore span, div, etc.
            ++i; continue;
        }
        if (in_tag) { tag_buf += c; ++i; continue; }

        // Regular text or entity
        if (c == '&') {
            size_t semi = html.find(';', i);
            if (semi != std::string::npos && semi - i <= 8) {
                text_buf += html.substr(i, semi - i + 1);
                i = semi + 1;
                continue;
            }
        }
        text_buf += c;
        ++i;
    }
    if (in_para) close_para();
    if (out.empty()) out = "\\par\n";
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// sanitise_filename
// ─────────────────────────────────────────────────────────────────────────────
std::string Exporter::sanitise_filename(const std::string& title, int idx,
                                          const std::string& ext) {
    std::string s;
    // Zero-pad index
    char pfx[8];
    std::snprintf(pfx, sizeof(pfx), "%02d_", idx);
    s += pfx;
    for (unsigned char c : title) {
        if (std::isalnum(c) || c == '-' || c == '_') s += (char)c;
        else if (c == ' ')  s += '_';
        // skip other chars
    }
    if (s.size() <= 3) s += "scene"; // fallback
    s += "." + ext;
    return s;
}

// Returns the heading text for a group node given options and counter.
// counter is incremented per top-level group.
static std::string group_heading_text(const Folio::Exporter::SourceNode& node,
                                       const Folio::ExportOptions& opts,
                                       int counter) {
    using GHS = Folio::ExportOptions::GroupHeadingStyle;
    switch (opts.group_heading_style) {
        case GHS::AsIs:
            return node.title;
        case GHS::AutoNumber:
            return opts.group_heading_word + " " + std::to_string(counter);
        case GHS::AutoNumberTitle:
            if (node.title.empty())
                return opts.group_heading_word + " " + std::to_string(counter);
            return opts.group_heading_word + " " + std::to_string(counter) + ": " + node.title;
        case GHS::NoHeading:
        default:
            return "";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// compile_combined
// ─────────────────────────────────────────────────────────────────────────────
std::string Exporter::compile_combined(const std::vector<SourceNode>& nodes,
                                         const ExportOptions& opts) {
    bool is_rtf = (opts.format == ExportOptions::Format::RTF);
    std::string out;

    if (is_rtf) out = rtf_header(opts);

    bool first_content = true;
    int group_counter = 0;

    for (const auto& node : nodes) {
        if (node.is_group) {
            bool has_heading = (opts.group_heading_style != ExportOptions::GroupHeadingStyle::NoHeading);
            if (!has_heading && !opts.include_group_content) continue;

            if (is_rtf && opts.page_break_on_group && !first_content)
                out += "\\page\n";

            std::string heading = group_heading_text(node, opts, ++group_counter);
            if (!heading.empty()) {
                if (is_rtf) {
                    int hd = (node.depth == 0) ? 36 : 28;
                    out += "\\pard\\qc\\b\\fs" + std::to_string(hd) + " "
                         + rtf_escape(heading)
                         + "\\b0\\fs24\\par\n\\pard\\par\n";
                } else {
                    std::string under(heading.size(), node.depth == 0 ? '=' : '-');
                    out += "\n" + heading + "\n" + under + "\n\n";
                }
                first_content = false;
            }

            if (opts.include_group_content && !node.html_content.empty()) {
                if (is_rtf) out += html_to_rtf_body(node.html_content, opts);
                else        out += html_to_plain(node.html_content) + "\n";
                first_content = false;
            }
            continue;
        }

        if (!first_content) {
            std::string sep = opts.scene_separator;
            if (!sep.empty()) {
                if (is_rtf) {
                    if (opts.separator_own_line)
                        out += "\\pard\\qc\\par\n";
                    out += "\\pard\\qc " + rtf_escape(sep) + "\\par\n";
                    if (opts.separator_own_line)
                        out += "\\pard\\par\n";
                } else {
                    if (opts.separator_own_line) out += "\n\n";
                    out += sep;
                    if (opts.separator_own_line) out += "\n\n";
                }
            }
        }

        if (is_rtf) out += html_to_rtf_body(node.html_content, opts);
        else {
            std::string plain = html_to_plain(node.html_content);
            if (!plain.empty()) out += plain + "\n";
        }

        first_content = false;
    }

    if (is_rtf) out += "}\n";
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// compile_entries  — one entry per scene
// ─────────────────────────────────────────────────────────────────────────────
std::vector<ExportEntry> Exporter::compile_entries(
        const std::vector<SourceNode>& nodes,
        const ExportOptions& opts) {
    std::string ext;
    switch (opts.format) {
        case ExportOptions::Format::RTF:      ext = "rtf"; break;
        case ExportOptions::Format::HTML:     ext = "html"; break;
        case ExportOptions::Format::Markdown: ext = "md"; break;
        default:                              ext = "txt"; break;
    }
    std::vector<ExportEntry> entries;
    int idx = 1;

    for (const auto& node : nodes) {
        if (node.is_group) continue;

        ExportEntry e;
        e.filename = sanitise_filename(node.title, idx++, ext);

        ExportOptions single = opts;
        single.mode = ExportOptions::Mode::Combined;

        switch (opts.format) {
            case ExportOptions::Format::RTF:
                e.content  = rtf_header(single);
                e.content += html_to_rtf_body(node.html_content, single);
                e.content += "}\n";
                break;
            case ExportOptions::Format::HTML: {
                std::vector<SourceNode> one = {node};
                e.content = compile_html(one, single, node.title);
                break;
            }
            case ExportOptions::Format::Markdown: {
                std::vector<SourceNode> one = {node};
                e.content = compile_markdown(one, single);
                break;
            }
            default:
                e.content = html_to_plain(node.html_content);
                break;
        }
        entries.push_back(std::move(e));
    }
    return entries;
}

// ─────────────────────────────────────────────────────────────────────────────
// write_zip  — minimal ZIP using zlib DEFLATE
// ─────────────────────────────────────────────────────────────────────────────
// Helpers to write little-endian integers into a byte buffer
static void write_u16(std::vector<uint8_t>& buf, uint16_t v) {
    buf.push_back(v & 0xFF);
    buf.push_back((v >> 8) & 0xFF);
}
static void write_u32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(v & 0xFF);
    buf.push_back((v >> 8) & 0xFF);
    buf.push_back((v >> 16) & 0xFF);
    buf.push_back((v >> 24) & 0xFF);
}
static void write_bytes(std::vector<uint8_t>& buf, const void* src, size_t n) {
    const auto* p = static_cast<const uint8_t*>(src);
    buf.insert(buf.end(), p, p + n);
}

std::string Exporter::write_zip(const std::vector<ExportEntry>& entries,
                                  const std::string& path) {
    struct LocalHeader {
        uint32_t local_offset;
        uint32_t crc32;
        uint32_t compressed_size;
        uint32_t uncompressed_size;
        std::string filename;
        std::vector<uint8_t> compressed_data;
    };

    std::vector<uint8_t> zip;
    std::vector<LocalHeader> headers;

    // MS-DOS epoch time (fixed: 2000-01-01 00:00:00)
    uint16_t dos_time = 0;
    uint16_t dos_date = (20 << 9) | (1 << 5) | 1; // 2000-01-01

    for (const auto& entry : entries) {
        LocalHeader lh;
        lh.filename = entry.filename;
        lh.local_offset = (uint32_t)zip.size();
        lh.uncompressed_size = (uint32_t)entry.content.size();

        // CRC32
        lh.crc32 = (uint32_t)crc32(0,
            reinterpret_cast<const Bytef*>(entry.content.data()),
            (uInt)entry.content.size());

        // DEFLATE compress
        uLongf bound = compressBound((uLong)entry.content.size());
        std::vector<uint8_t> cbuf(bound);
        uLongf csize = bound;
        int zret = compress2(cbuf.data(), &csize,
            reinterpret_cast<const Bytef*>(entry.content.data()),
            (uLong)entry.content.size(), Z_DEFAULT_COMPRESSION);

        // If compress fails or doesn't help, store uncompressed
        bool deflated = (zret == Z_OK && csize < entry.content.size());
        uint16_t method;
        if (deflated) {
            // zlib adds a 2-byte header and 4-byte checksum — strip them
            // to get raw DEFLATE stream (ZIP method 8)
            lh.compressed_data = std::vector<uint8_t>(
                cbuf.begin() + 2, cbuf.begin() + csize - 4);
            lh.compressed_size = (uint32_t)lh.compressed_data.size();
            method = 8; // DEFLATE
        } else {
            lh.compressed_data = std::vector<uint8_t>(
                entry.content.begin(), entry.content.end());
            lh.compressed_size = lh.uncompressed_size;
            method = 0; // STORE
        }

        // Local file header signature
        write_u32(zip, 0x04034b50);
        write_u16(zip, 20);          // version needed
        write_u16(zip, 0);           // flags
        write_u16(zip, method);
        write_u16(zip, dos_time);
        write_u16(zip, dos_date);
        write_u32(zip, lh.crc32);
        write_u32(zip, lh.compressed_size);
        write_u32(zip, lh.uncompressed_size);
        write_u16(zip, (uint16_t)lh.filename.size());
        write_u16(zip, 0); // extra field length
        write_bytes(zip, lh.filename.data(), lh.filename.size());
        write_bytes(zip, lh.compressed_data.data(), lh.compressed_data.size());

        headers.push_back(std::move(lh));
    }

    // Central directory
    uint32_t cd_start = (uint32_t)zip.size();
    for (const auto& lh : headers) {
        uint16_t method = lh.compressed_size < lh.uncompressed_size ? 8 : 0;
        write_u32(zip, 0x02014b50);  // central dir signature
        write_u16(zip, 20);          // version made by
        write_u16(zip, 20);          // version needed
        write_u16(zip, 0);           // flags
        write_u16(zip, method);
        write_u16(zip, dos_time);
        write_u16(zip, dos_date);
        write_u32(zip, lh.crc32);
        write_u32(zip, lh.compressed_size);
        write_u32(zip, lh.uncompressed_size);
        write_u16(zip, (uint16_t)lh.filename.size());
        write_u16(zip, 0); // extra
        write_u16(zip, 0); // comment
        write_u16(zip, 0); // disk start
        write_u16(zip, 0); // internal attr
        write_u32(zip, 0); // external attr
        write_u32(zip, lh.local_offset);
        write_bytes(zip, lh.filename.data(), lh.filename.size());
    }
    uint32_t cd_size = (uint32_t)zip.size() - cd_start;

    // End of central directory
    write_u32(zip, 0x06054b50);
    write_u16(zip, 0);  // disk number
    write_u16(zip, 0);  // disk with CD
    write_u16(zip, (uint16_t)headers.size());
    write_u16(zip, (uint16_t)headers.size());
    write_u32(zip, cd_size);
    write_u32(zip, cd_start);
    write_u16(zip, 0);  // comment length

    // Write to file
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return "Cannot open output file: " + path;
    size_t written = fwrite(zip.data(), 1, zip.size(), f);
    fclose(f);
    if (written != zip.size()) return "Write error: " + path;
    return ""; // success
}

// ─────────────────────────────────────────────────────────────────────────────
// html_to_xhtml_body  — produce clean XHTML for EPUB
// Strips all inline style spans (font, size, color, line-height) and
// data-folio-style spans, preserving only semantic markup: b, i, u, s, p.
// E-readers apply their own typography; injecting font/size overrides causes
// inconsistent rendering across Kindle, Kobo, Apple Books, etc.
// ─────────────────────────────────────────────────────────────────────────────
std::string Exporter::html_to_xhtml_body(const std::string& html) {
    if (html.empty()) return "<p></p>";

    std::string out;
    bool in_tag = false;
    std::string tag_buf;

    // Tags we keep (semantic only)
    auto is_kept_tag = [](const std::string& tn) -> bool {
        return tn == "b" || tn == "strong" || tn == "i" || tn == "em" ||
               tn == "u" || tn == "s" || tn == "p";
    };

    size_t i = 0;
    while (i < html.size()) {
        char c = html[i];
        if (c == '<') { in_tag = true; tag_buf.clear(); ++i; continue; }
        if (c == '>') {
            in_tag = false;
            std::string t = tag_buf;
            // Lowercase tag name only (leave attribute case alone for now)
            std::string tn_lower;
            for (size_t j = 0; j < t.size(); ++j) {
                char ch = (char)std::tolower((unsigned char)t[j]);
                if (ch == ' ' || ch == '/' ) { if (j == 0) tn_lower += ch; break; }
                tn_lower += ch;
            }
            bool closing = (!tn_lower.empty() && tn_lower[0] == '/');
            std::string tn = closing ? tn_lower.substr(1) : tn_lower;

            if (tn == "p") {
                // Strip style attributes from <p> — just emit <p> or </p>
                out += closing ? "</p>" : "<p>";
            } else if (is_kept_tag(tn)) {
                out += closing ? ("</" + tn + ">") : ("<" + tn + ">");
            }
            // All span tags (style=, data-folio-style=) are dropped silently
            ++i; continue;
        }
        if (in_tag) { tag_buf += c; ++i; continue; }

        // Pass-through text and entities unchanged (they're already XML-safe
        // since buffer_to_html html-escapes them)
        out += c;
        ++i;
    }

    if (out.empty()) out = "<p></p>";
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// html_to_markdown  — convert stored HTML inline markup to Markdown
// ─────────────────────────────────────────────────────────────────────────────
std::string Exporter::html_to_markdown(const std::string& html) {
    if (html.empty()) return "";
    std::string out;
    bool in_tag = false;
    std::string tag_buf;
    std::string text_buf;
    [[maybe_unused]] bool bold = false, italic = false, underline = false, strike = false;

    auto flush = [&]() {
        if (!text_buf.empty()) {
            // Decode entities
            std::string plain = html_to_plain("<p>" + text_buf + "</p>");
            // Escape markdown special chars in plain text
            std::string escaped;
            for (char c : plain) {
                if (c == '*' || c == '_' || c == '\\' || c == '`' || c == '#')
                    escaped += '\\';
                escaped += c;
            }
            out += escaped;
            text_buf.clear();
        }
    };

    size_t i = 0;
    bool in_para = false;
    while (i < html.size()) {
        char c = html[i];
        if (c == '<') { in_tag = true; tag_buf.clear(); ++i; continue; }
        if (c == '>') {
            in_tag = false;
            std::string t = tag_buf;
            for (auto& ch : t) ch = (char)std::tolower((unsigned char)ch);
            bool closing = !t.empty() && t[0] == '/';
            if (closing) t = t.substr(1);
            std::string tn = t.substr(0, t.find(' '));

            if (tn == "p") {
                if (!closing) {
                    if (in_para) { flush(); out += "\n\n"; }
                    in_para = true;
                } else {
                    flush();
                    in_para = false;
                }
            } else if (tn == "b" || tn == "strong") {
                flush(); out += "**"; bold = !closing;
            } else if (tn == "i" || tn == "em") {
                flush(); out += "_"; italic = !closing;
            } else if (tn == "u") {
                flush(); // underline not standard MD — use as-is
            } else if (tn == "s") {
                flush(); out += "~~"; strike = !closing;
            }
            ++i; continue;
        }
        if (in_tag) { tag_buf += c; ++i; continue; }
        if (c == '&') {
            size_t semi = html.find(';', i);
            if (semi != std::string::npos && semi - i <= 8) {
                text_buf += html.substr(i, semi - i + 1);
                i = semi + 1; continue;
            }
        }
        text_buf += c;
        ++i;
    }
    flush();
    // Trim
    while (!out.empty() && out.back() == '\n') out.pop_back();
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// compile_html
// ─────────────────────────────────────────────────────────────────────────────
std::string Exporter::compile_html(const std::vector<SourceNode>& nodes,
                                     const ExportOptions& opts,
                                     const std::string& title) {
    std::string body;
    bool first = true;
    int html_group_counter = 0;
    for (const auto& node : nodes) {
        if (node.is_group) {
            bool has_heading = (opts.group_heading_style != ExportOptions::GroupHeadingStyle::NoHeading);
            if (!has_heading && !opts.include_group_content) continue;
            std::string heading = group_heading_text(node, opts, ++html_group_counter);
            if (!heading.empty()) {
                std::string tag = node.depth == 0 ? "h1" : "h2";
                body += "<" + tag + " class=\"group-title\">"
                      + rtf_escape(heading) + "</" + tag + ">\n";
                first = false;
            }
            if (opts.include_group_content && !node.html_content.empty()) {
                body += "<section class=\"group-content\">"
                      + node.html_content + "</section>\n";
                first = false;
            }
            continue;
        }
        if (!first && !opts.scene_separator.empty()) {
            if (opts.separator_own_line)
                body += "<p class=\"separator\">" +
                        html_to_plain(opts.scene_separator) + "</p>\n";
        }
        body += "<section class=\"scene\">\n";
        if (!node.title.empty())
            body += "<h2 class=\"scene-title\">" + rtf_escape(node.title) + "</h2>\n";
        // Strip data-folio-style spans (opaque to browsers) but keep style= spans
        std::string content = node.html_content;
        {
            std::string cleaned;
            bool in_t = false;
            std::string tbuf;
            for (size_t ci = 0; ci < content.size(); ++ci) {
                char ch = content[ci];
                if (ch == '<') { in_t = true; tbuf.clear(); continue; }
                if (ch == '>') {
                    in_t = false;
                    // Check if it's a data-folio-style span — drop it
                    if (tbuf.find("data-folio-style") != std::string::npos) continue;
                    cleaned += '<'; cleaned += tbuf; cleaned += '>';
                    continue;
                }
                if (in_t) { tbuf += ch; continue; }
                cleaned += ch;
            }
            content = std::move(cleaned);
        }
        body += content.empty() ? "<p></p>" : content;
        body += "\n</section>\n";
        first = false;
    }

    std::string css;
    if (opts.flatten) {
        css += "body { font-family: '" + opts.flatten_font + "'; "
             + "font-size: " + std::to_string(opts.flatten_size_pt) + "pt; "
             + "line-height: " + std::to_string(opts.flatten_line_spacing) + "; "
             + "margin: " + std::to_string(opts.flatten_margin_pt) + "pt; }\n";
        css += "p { margin-bottom: " + std::to_string(opts.flatten_para_space_pt) + "pt; }\n";
    } else {
        css = "body { font-family: Georgia, serif; font-size: 12pt; "
              "line-height: 1.8; max-width: 40em; margin: 2em auto; }\n"
              "p { margin: 0 0 0.5em 0; }\n";
    }
    if (opts.first_line_indent && opts.first_line_indent_px > 0)
        css += "p { text-indent: " + std::to_string(opts.first_line_indent_px) + "px; }\n";
    css += ".separator { text-align: center; margin: 2em 0; }\n"
           ".scene-title { font-size: 1em; font-weight: bold; margin-bottom: 1em; }\n"
           ".group-title { text-align: center; margin: 2em 0 1em 0; }\n"
           ".group-content { margin-bottom: 2em; }\n";

    std::string html;
    html += "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n"
            "<meta charset=\"UTF-8\"/>\n"
            "<title>" + (title.empty() ? "Export" : rtf_escape(title)) + "</title>\n"
            "<style>\n" + css + "</style>\n"
            "</head>\n<body>\n";
    if (!title.empty())
        html += "<h1>" + rtf_escape(title) + "</h1>\n";
    html += body;
    html += "</body>\n</html>\n";
    return html;
}

// ─────────────────────────────────────────────────────────────────────────────
// compile_markdown
// ─────────────────────────────────────────────────────────────────────────────
std::string Exporter::compile_markdown(const std::vector<SourceNode>& nodes,
                                         const ExportOptions& opts) {
    std::string out;
    bool first = true;
    int md_group_counter = 0;
    for (const auto& node : nodes) {
        if (node.is_group) {
            bool has_heading = (opts.group_heading_style != ExportOptions::GroupHeadingStyle::NoHeading);
            if (!has_heading && !opts.include_group_content) continue;
            std::string heading = group_heading_text(node, opts, ++md_group_counter);
            if (!heading.empty()) {
                std::string hashes = node.depth == 0 ? "#" : "##";
                out += "\n" + hashes + " " + heading + "\n\n";
                first = false;
            }
            if (opts.include_group_content && !node.html_content.empty()) {
                out += html_to_markdown(node.html_content) + "\n\n";
                first = false;
            }
            continue;
        }
        if (!first) {
            std::string sep = opts.scene_separator.empty() ? "---" : opts.scene_separator;
            out += "\n\n" + sep + "\n\n";
        }
        if (!node.title.empty())
            out += "## " + node.title + "\n\n";
        out += html_to_markdown(node.html_content);
        out += "\n";
        first = false;
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// compile_epub  — EPUB 3 ZIP
// ─────────────────────────────────────────────────────────────────────────────
std::vector<uint8_t> Exporter::compile_epub(const std::vector<SourceNode>& nodes,
                                               const ExportOptions& opts,
                                               const std::string& title,
                                               const std::string& author) {
    std::string safe_title = title.empty() ? "Untitled" : title;
    std::string safe_author = author.empty() ? "Unknown" : author;

    // Build a flat ordered list of items to put in the EPUB spine.
    // Each item gets a sequential file index regardless of type.
    struct EpubItem { std::string file_id; std::string href; std::string nav_title;
                      std::string xhtml; bool is_group; };
    std::vector<EpubItem> items;
    int item_idx = 0;
    int epub_group_counter = 0;

    for (const auto& node : nodes) {
        if (node.is_group) {
            bool has_heading = (opts.group_heading_style != ExportOptions::GroupHeadingStyle::NoHeading);
            if (!has_heading && !opts.include_group_content) continue;
            std::string heading = group_heading_text(node, opts, ++epub_group_counter);
            ++item_idx;
            char id[32], fname[64];
            std::snprintf(id,    sizeof(id),    "item%03d", item_idx);
            std::snprintf(fname, sizeof(fname), "text/item%03d.xhtml", item_idx);

            std::string htag = node.depth == 0 ? "h1" : "h2";
            std::string xhtml =
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                "<!DOCTYPE html>\n"
                "<html xmlns=\"http://www.w3.org/1999/xhtml\">\n"
                "<head><meta charset=\"UTF-8\"/>\n"
                "<link rel=\"stylesheet\" type=\"text/css\" href=\"../stylesheet.css\"/>\n"
                "<title>" + rtf_escape(heading.empty() ? safe_title : heading) + "</title>\n"
                "</head>\n<body>\n";
            if (!heading.empty())
                xhtml += "<" + htag + " class=\"group-title\">" + rtf_escape(heading) + "</" + htag + ">\n";
            if (opts.include_group_content && !node.html_content.empty())
                xhtml += html_to_xhtml_body(node.html_content);
            xhtml += "\n</body>\n</html>\n";

            std::string nav_title = heading.empty()
                ? (node.title.empty() ? "Part " + std::to_string(epub_group_counter) : node.title)
                : heading;
            items.push_back({id, fname, nav_title, xhtml, true});
        } else {
            ++item_idx;
            char id[32], fname[64];
            std::snprintf(id,    sizeof(id),    "item%03d", item_idx);
            std::snprintf(fname, sizeof(fname), "text/item%03d.xhtml", item_idx);

            std::string body_content = node.html_content.empty()
                ? "<p></p>" : html_to_xhtml_body(node.html_content);
            // Prepend scene separator if not the first scene and separator set
            std::string sep_html;
            if (item_idx > 1 && !opts.scene_separator.empty()) {
                sep_html = "<p class=\"scene-sep\">"
                         + html_to_plain(opts.scene_separator) + "</p>\n";
                if (opts.separator_own_line) sep_html = "<p></p>\n" + sep_html + "<p></p>\n";
            }
            std::string xhtml =
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                "<!DOCTYPE html>\n"
                "<html xmlns=\"http://www.w3.org/1999/xhtml\">\n"
                "<head><meta charset=\"UTF-8\"/>\n"
                "<link rel=\"stylesheet\" type=\"text/css\" href=\"../stylesheet.css\"/>\n"
                "<title>" + rtf_escape(node.title.empty() ? safe_title : node.title) + "</title>\n"
                "</head>\n<body>\n";
            if (!node.title.empty()) xhtml += "<h2>" + rtf_escape(node.title) + "</h2>\n";
            xhtml += sep_html + body_content + "\n</body>\n</html>\n";

            items.push_back({id, fname,
                node.title.empty() ? ("Scene " + std::to_string(item_idx)) : node.title,
                xhtml, false});
        }
    }

    // ── Build EPUB file entries ───────────────────────────────────────────────
    std::vector<ExportEntry> entries;
    entries.push_back({"mimetype", "application/epub+zip"});

    entries.push_back({"META-INF/container.xml",
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">\n"
        "  <rootfiles>\n"
        "    <rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>\n"
        "  </rootfiles>\n"
        "</container>\n"});

    // ── Cover image (optional) ────────────────────────────────────────────────
    bool has_cover = !opts.cover_thumbnail_b64.empty();
    std::string cover_png_bytes;
    if (has_cover) {
        cover_png_bytes = Glib::Base64::decode(opts.cover_thumbnail_b64);
        // Add raw PNG as a binary entry
        entries.push_back({"OEBPS/images/cover.png", cover_png_bytes});
        // Cover XHTML page — first in spine
        std::string cover_xhtml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<!DOCTYPE html>\n"
            "<html xmlns=\"http://www.w3.org/1999/xhtml\">\n"
            "<head><meta charset=\"UTF-8\"/>\n"
            "<title>Cover</title>\n"
            "<style>body{margin:0;padding:0;text-align:center;}"
            "img{max-width:100%;max-height:100%;}</style>\n"
            "</head>\n"
            "<body>\n"
            "<img src=\"../images/cover.png\" alt=\"Cover\"/>\n"
            "</body>\n</html>\n";
        entries.push_back({"OEBPS/text/cover.xhtml", cover_xhtml});
    }

    // nav.xhtml — TOC
    std::string nav = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE html>\n"
        "<html xmlns=\"http://www.w3.org/1999/xhtml\" xmlns:epub=\"http://www.idpf.org/2007/ops\">\n"
        "<head><meta charset=\"UTF-8\"/><title>Table of Contents</title></head>\n"
        "<body><nav epub:type=\"toc\" id=\"toc\">\n<ol>\n";
    for (const auto& it : items)
        nav += "  <li><a href=\"" + it.href + "\">" + rtf_escape(it.nav_title) + "</a></li>\n";
    nav += "</ol>\n</nav>\n</body>\n</html>\n";
    entries.push_back({"OEBPS/nav.xhtml", nav});

    // stylesheet.css
    std::string css = "body { font-family: Georgia, serif; font-size: 1em; line-height: 1.8; }\n"
                      "p { margin: 0 0 0.5em 0; text-indent: 1.5em; }\n"
                      "p:first-child { text-indent: 0; }\n"
                      "h1 { font-size: 1.6em; margin: 2em 0 1em 0; text-align: center; }\n"
                      "h2 { font-size: 1.2em; margin: 1.5em 0 0.5em 0; }\n"
                      ".group-title { page-break-before: always; }\n"
                      ".separator { text-align: center; margin: 2em 0; }\n"
                      ".scene-sep { text-align: center; margin: 1.5em 0; text-indent: 0; }\n";
    if (opts.flatten) {
        css += "body { font-size: " + std::to_string(opts.flatten_size_pt) + "pt; "
             + "line-height: " + std::to_string(opts.flatten_line_spacing) + "; }\n"
             + "p { margin-bottom: " + std::to_string(opts.flatten_para_space_pt) + "pt; }\n";
    }
    entries.push_back({"OEBPS/stylesheet.css", css});

    // Per-item XHTML files
    for (const auto& it : items)
        entries.push_back({"OEBPS/" + it.href, it.xhtml});

    // content.opf
    std::string opf =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<package version=\"3.0\" xmlns=\"http://www.idpf.org/2007/opf\" unique-identifier=\"uid\">\n"
        "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n"
        "  <dc:title>" + rtf_escape(safe_title) + "</dc:title>\n"
        "  <dc:creator>" + rtf_escape(safe_author) + "</dc:creator>\n"
        "  <dc:language>en</dc:language>\n"
        "  <meta property=\"dcterms:modified\">2000-01-01T00:00:00Z</meta>\n"
        "  <dc:identifier id=\"uid\">folio-export</dc:identifier>\n";
    if (has_cover)
        opf += "  <meta name=\"cover\" content=\"cover-img\"/>\n";
    opf += "</metadata>\n"
        "<manifest>\n"
        "  <item id=\"nav\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" properties=\"nav\"/>\n"
        "  <item id=\"css\" href=\"stylesheet.css\" media-type=\"text/css\"/>\n";
    if (has_cover) {
        opf += "  <item id=\"cover-img\" href=\"images/cover.png\""
               " media-type=\"image/png\" properties=\"cover-image\"/>\n";
        opf += "  <item id=\"cover-page\" href=\"text/cover.xhtml\""
               " media-type=\"application/xhtml+xml\"/>\n";
    }
    for (const auto& it : items)
        opf += "  <item id=\"" + it.file_id + "\" href=\"" + it.href
             + "\" media-type=\"application/xhtml+xml\"/>\n";
    opf += "</manifest>\n<spine>\n";
    if (has_cover)
        opf += "  <itemref idref=\"cover-page\"/>\n";
    for (const auto& it : items)
        opf += "  <itemref idref=\"" + it.file_id + "\"/>\n";
    opf += "</spine>\n</package>\n";
    entries.push_back({"OEBPS/content.opf", opf});

    // ── Write ZIP (mimetype must be STORED uncompressed at offset 0) ──────────
    // We write mimetype as STORE, everything else normally via write_zip logic.
    // Implement inline to handle STORE for first entry.
    std::vector<uint8_t> zip;

    auto write_u16z = [&](uint16_t v) {
        zip.push_back(v & 0xFF); zip.push_back((v >> 8) & 0xFF);
    };
    auto write_u32z = [&](uint32_t v) {
        zip.push_back(v & 0xFF); zip.push_back((v >> 8) & 0xFF);
        zip.push_back((v >> 16) & 0xFF); zip.push_back((v >> 24) & 0xFF);
    };
    auto write_bz = [&](const void* src, size_t n) {
        const auto* p = static_cast<const uint8_t*>(src);
        zip.insert(zip.end(), p, p + n);
    };

    uint16_t dos_time = 0, dos_date = (20 << 9) | (1 << 5) | 1;

    struct ZEntry { uint32_t offset; uint32_t crc; uint32_t csz; uint32_t usz;
                    uint16_t method; std::string name; };
    std::vector<ZEntry> zentries;

    for (size_t ei = 0; ei < entries.size(); ++ei) {
        const auto& e = entries[ei];
        ZEntry ze;
        ze.name   = e.filename;
        ze.offset = (uint32_t)zip.size();
        ze.usz    = (uint32_t)e.content.size();
        ze.crc    = (uint32_t)crc32(0,
            reinterpret_cast<const Bytef*>(e.content.data()), (uInt)e.content.size());

        // mimetype (index 0) must be STORED
        if (ei == 0) {
            ze.method = 0; // STORE
            ze.csz    = ze.usz;
            write_u32z(0x04034b50); write_u16z(20); write_u16z(0);
            write_u16z(0); write_u16z(dos_time); write_u16z(dos_date);
            write_u32z(ze.crc); write_u32z(ze.csz); write_u32z(ze.usz);
            write_u16z((uint16_t)ze.name.size()); write_u16z(0);
            write_bz(ze.name.data(), ze.name.size());
            write_bz(e.content.data(), e.content.size());
        } else {
            uLongf bound = compressBound((uLong)e.content.size());
            std::vector<uint8_t> cbuf(bound);
            uLongf csize = bound;
            int zr = compress2(cbuf.data(), &csize,
                reinterpret_cast<const Bytef*>(e.content.data()),
                (uLong)e.content.size(), Z_DEFAULT_COMPRESSION);
            bool deflated = (zr == Z_OK && csize > 6 && csize - 6 < e.content.size());
            if (deflated) {
                ze.method = 8;
                ze.csz = (uint32_t)(csize - 6);
                write_u32z(0x04034b50); write_u16z(20); write_u16z(0);
                write_u16z(8); write_u16z(dos_time); write_u16z(dos_date);
                write_u32z(ze.crc); write_u32z(ze.csz); write_u32z(ze.usz);
                write_u16z((uint16_t)ze.name.size()); write_u16z(0);
                write_bz(ze.name.data(), ze.name.size());
                write_bz(cbuf.data() + 2, ze.csz);
            } else {
                ze.method = 0; ze.csz = ze.usz;
                write_u32z(0x04034b50); write_u16z(20); write_u16z(0);
                write_u16z(0); write_u16z(dos_time); write_u16z(dos_date);
                write_u32z(ze.crc); write_u32z(ze.csz); write_u32z(ze.usz);
                write_u16z((uint16_t)ze.name.size()); write_u16z(0);
                write_bz(ze.name.data(), ze.name.size());
                write_bz(e.content.data(), e.content.size());
            }
        }
        zentries.push_back(ze);
    }

    // Central directory
    uint32_t cd_start = (uint32_t)zip.size();
    for (const auto& ze : zentries) {
        write_u32z(0x02014b50); write_u16z(20); write_u16z(20);
        write_u16z(0); write_u16z(ze.method);
        write_u16z(dos_time); write_u16z(dos_date);
        write_u32z(ze.crc); write_u32z(ze.csz); write_u32z(ze.usz);
        write_u16z((uint16_t)ze.name.size()); write_u16z(0); write_u16z(0);
        write_u16z(0); write_u16z(0); write_u32z(0); write_u32z(ze.offset);
        write_bz(ze.name.data(), ze.name.size());
    }
    uint32_t cd_size = (uint32_t)zip.size() - cd_start;
    write_u32z(0x06054b50); write_u16z(0); write_u16z(0);
    write_u16z((uint16_t)zentries.size()); write_u16z((uint16_t)zentries.size());
    write_u32z(cd_size); write_u32z(cd_start); write_u16z(0);

    return zip;
}

// ─────────────────────────────────────────────────────────────────────────────
// compile_docx  — DOCX (Office Open XML) ZIP
// ─────────────────────────────────────────────────────────────────────────────

// Escape XML special characters
static std::string xml_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if      (c == '&')  out += "&amp;";
        else if (c == '<')  out += "&lt;";
        else if (c == '>')  out += "&gt;";
        else if (c == '"')  out += "&quot;";
        else if (c == '\'') out += "&apos;";
        else if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') continue; // strip control chars
        else                out += (char)c;
    }
    return out;
}

// Convert stored HTML for one scene into word/document.xml paragraph runs.
// Returns a string of <w:p>...</w:p> elements.
static std::string html_to_docx_paras(const std::string& html,
                                       const Folio::ExportOptions& opts) {
    if (html.empty()) return "<w:p><w:r><w:t/></w:r></w:p>\n";

    std::string out;
    bool in_tag = false;
    std::string tag_buf;
    std::string text_buf;
    bool bold = false, italic = false, underline = false, strike = false;
    bool in_para = false;

    // Build paragraph properties string
    std::string pPr;
    if (opts.flatten) {
        // Spacing: line = lineSpacing * 240 (twips), after = para_space_pt * 20
        int sl = (int)(opts.flatten_line_spacing * 240.0);
        int sa = opts.flatten_para_space_pt * 20;
        int fi = (opts.first_line_indent && opts.first_line_indent_px > 0)
                  ? opts.first_line_indent_px * 15 : 0;
        pPr = "<w:pPr>"
              "<w:spacing w:line=\"" + std::to_string(sl) + "\" w:lineRule=\"auto\""
              + (sa ? " w:after=\"" + std::to_string(sa) + "\"" : "") + "/>"
              + (fi ? "<w:ind w:firstLine=\"" + std::to_string(fi) + "\"/>" : "")
              + "</w:pPr>";
    } else if (opts.first_line_indent && opts.first_line_indent_px > 0) {
        int fi = opts.first_line_indent_px * 15;
        pPr = "<w:pPr><w:ind w:firstLine=\"" + std::to_string(fi) + "\"/></w:pPr>";
    }

    auto flush_run = [&]() {
        if (text_buf.empty()) return;
        // Decode entities
        std::string plain = Folio::Exporter::html_to_plain("<p>" + text_buf + "</p>");
        text_buf.clear();
        if (plain.empty()) return;

        std::string rPr;
        if (bold)      rPr += "<w:b/>";
        if (italic)    rPr += "<w:i/>";
        if (underline) rPr += "<w:u w:val=\"single\"/>";
        if (strike)    rPr += "<w:strike/>";

        // Apply flatten font/size if set
        if (opts.flatten) {
            rPr = "<w:rFonts w:ascii=\"" + xml_escape(opts.flatten_font) +
                  "\" w:hAnsi=\"" + xml_escape(opts.flatten_font) + "\"/>"
                  "<w:sz w:val=\"" + std::to_string(opts.flatten_size_pt * 2) + "\"/>"
                  "<w:szCs w:val=\"" + std::to_string(opts.flatten_size_pt * 2) + "\"/>"
                  + rPr;
        }

        out += "<w:r>";
        if (!rPr.empty()) out += "<w:rPr>" + rPr + "</w:rPr>";
        // Preserve leading/trailing spaces with xml:space="preserve"
        out += "<w:t xml:space=\"preserve\">" + xml_escape(plain) + "</w:t>";
        out += "</w:r>";
    };

    auto open_para = [&]() {
        out += "<w:p>" + pPr;
        in_para = true;
    };
    auto close_para = [&]() {
        flush_run();
        out += "</w:p>\n";
        in_para = false;
    };

    size_t i = 0;
    while (i < html.size()) {
        char c = html[i];
        if (c == '<') { in_tag = true; tag_buf.clear(); ++i; continue; }
        if (c == '>') {
            in_tag = false;
            std::string t = tag_buf;
            for (auto& ch : t) ch = (char)std::tolower((unsigned char)ch);
            bool closing = !t.empty() && t[0] == '/';
            if (closing) t = t.substr(1);
            std::string tn = t.substr(0, t.find(' '));

            if (tn == "p") {
                if (!closing) { if (in_para) close_para(); open_para(); }
                else          { if (in_para) close_para(); }
            } else if (tn == "b" || tn == "strong") {
                flush_run(); bold = !closing;
            } else if (tn == "i" || tn == "em") {
                flush_run(); italic = !closing;
            } else if (tn == "u") {
                flush_run(); underline = !closing;
            } else if (tn == "s") {
                flush_run(); strike = !closing;
            }
            // Ignore span, font, etc.
            ++i; continue;
        }
        if (in_tag) { tag_buf += c; ++i; continue; }
        if (c == '&') {
            size_t semi = html.find(';', i);
            if (semi != std::string::npos && semi - i <= 8) {
                text_buf += html.substr(i, semi - i + 1);
                i = semi + 1; continue;
            }
        }
        text_buf += c;
        ++i;
    }
    if (in_para) close_para();
    if (out.empty()) out = "<w:p><w:r><w:t/></w:r></w:p>\n";
    return out;
}

std::vector<uint8_t> Exporter::compile_docx(const std::vector<SourceNode>& nodes,
                                               const ExportOptions& opts,
                                               const std::string& title,
                                               const std::string& author) {
    // ── Build document body ───────────────────────────────────────────────────
    std::string body;
    bool first = true;
    int docx_group_counter = 0;

    for (const auto& node : nodes) {
        if (node.is_group) {
            bool has_heading = (opts.group_heading_style != ExportOptions::GroupHeadingStyle::NoHeading);
            if (!has_heading && !opts.include_group_content) continue;

            if (opts.page_break_on_group && !first)
                body += "<w:p><w:r><w:br w:type=\"page\"/></w:r></w:p>\n";

            std::string heading = group_heading_text(node, opts, ++docx_group_counter);
            if (!heading.empty()) {
                // Heading paragraph — map depth to Word heading styles
                std::string style_id = node.depth == 0 ? "Heading1" : "Heading2";
                int hd_sz = node.depth == 0 ? 36 : 28; // half-points: 18pt / 14pt
                body += "<w:p><w:pPr><w:pStyle w:val=\"" + style_id + "\"/>"
                        "<w:jc w:val=\"center\"/></w:pPr>"
                        "<w:r><w:rPr><w:b/><w:sz w:val=\"" + std::to_string(hd_sz) + "\"/>"
                        "<w:szCs w:val=\"" + std::to_string(hd_sz) + "\"/></w:rPr>"
                        "<w:t>" + xml_escape(node.title) + "</w:t></w:r></w:p>\n";
                body += "<w:p/>\n"; // blank line after heading
                first = false;
            }

            if (opts.include_group_content && !node.html_content.empty()) {
                body += html_to_docx_paras(node.html_content, opts);
                first = false;
            }
            continue;
        }

        if (!first && !opts.scene_separator.empty()) {
            body += "<w:p><w:pPr><w:jc w:val=\"center\"/></w:pPr>"
                    "<w:r><w:t>" + xml_escape(opts.scene_separator) + "</w:t></w:r></w:p>\n";
            if (opts.separator_own_line)
                body += "<w:p/>\n";
        }

        body += html_to_docx_paras(node.html_content, opts);
        first = false;
    }

    // Default Normal style font/size
    std::string style_font = opts.flatten ? opts.flatten_font : "Times New Roman";
    int style_sz = (opts.flatten ? opts.flatten_size_pt : 12) * 2; // half-points
    int style_sl = (int)((opts.flatten ? opts.flatten_line_spacing : 2.0) * 240.0);
    int style_sa = opts.flatten ? opts.flatten_para_space_pt * 20 : 0;
    int style_fi = (opts.first_line_indent && opts.first_line_indent_px > 0)
                   ? opts.first_line_indent_px * 15 : 0;
    int style_margin_tw = opts.flatten ? opts.flatten_margin_pt * 20 : 1440;

    // ── DOCX file entries ─────────────────────────────────────────────────────
    std::vector<ExportEntry> entries;

    // [Content_Types].xml
    entries.push_back({"[Content_Types].xml", R"(<?xml version="1.0" encoding="UTF-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
  <Default Extension="xml" ContentType="application/xml"/>
  <Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>
  <Override PartName="/word/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml"/>
  <Override PartName="/word/settings.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.settings+xml"/>
  <Override PartName="/word/fontTable.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.fontTable+xml"/>
  <Override PartName="/docProps/core.xml" ContentType="application/vnd.openxmlformats-package.core-properties+xml"/>
</Types>)"});

    // _rels/.rels
    entries.push_back({"_rels/.rels", R"(<?xml version="1.0" encoding="UTF-8"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
  <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>
  <Relationship Id="rId2" Type="http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties" Target="docProps/core.xml"/>
</Relationships>)"});

    // word/_rels/document.xml.rels
    entries.push_back({"word/_rels/document.xml.rels", R"(<?xml version="1.0" encoding="UTF-8"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
  <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>
  <Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/settings" Target="settings.xml"/>
  <Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/fontTable" Target="fontTable.xml"/>
</Relationships>)"});

    // word/settings.xml
    entries.push_back({"word/settings.xml", R"(<?xml version="1.0" encoding="UTF-8"?>
<w:settings xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
  <w:defaultTabStop w:val="720"/>
  <w:compat><w:compatSetting w:name="compatibilityMode" w:uri="http://schemas.microsoft.com/office/word" w:val="15"/></w:compat>
</w:settings>)"});

    // word/fontTable.xml
    entries.push_back({"word/fontTable.xml",
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<w:fonts xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">\n"
        "  <w:font w:name=\"" + xml_escape(style_font) + "\"><w:charset w:val=\"00\"/></w:font>\n"
        "  <w:font w:name=\"Times New Roman\"><w:charset w:val=\"00\"/></w:font>\n"
        "</w:fonts>"});

    // docProps/core.xml
    entries.push_back({"docProps/core.xml",
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\""
        " xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n"
        "  <dc:title>" + xml_escape(title) + "</dc:title>\n"
        "  <dc:creator>" + xml_escape(author) + "</dc:creator>\n"
        "</cp:coreProperties>"});

    // word/styles.xml — defines Normal style with our formatting
    entries.push_back({"word/styles.xml",
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">\n"
        "  <w:docDefaults>\n"
        "    <w:rPrDefault><w:rPr>\n"
        "      <w:rFonts w:ascii=\"" + xml_escape(style_font) +
        "\" w:hAnsi=\"" + xml_escape(style_font) + "\"/>\n"
        "      <w:sz w:val=\"" + std::to_string(style_sz) + "\"/>\n"
        "      <w:szCs w:val=\"" + std::to_string(style_sz) + "\"/>\n"
        "    </w:rPr></w:rPrDefault>\n"
        "    <w:pPrDefault><w:pPr>\n"
        "      <w:spacing w:line=\"" + std::to_string(style_sl) +
        "\" w:lineRule=\"auto\" w:after=\"" + std::to_string(style_sa) + "\"/>\n"
        + (style_fi ? "      <w:ind w:firstLine=\"" + std::to_string(style_fi) + "\"/>\n" : "")
        + "    </w:pPr></w:pPrDefault>\n"
        "  </w:docDefaults>\n"
        "  <w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\">\n"
        "    <w:name w:val=\"Normal\"/>\n"
        "  </w:style>\n"
        "  <w:style w:type=\"paragraph\" w:styleId=\"Heading1\">\n"
        "    <w:name w:val=\"heading 1\"/>\n"
        "    <w:basedOn w:val=\"Normal\"/>\n"
        "    <w:pPr><w:jc w:val=\"center\"/></w:pPr>\n"
        "    <w:rPr><w:b/><w:sz w:val=\"36\"/><w:szCs w:val=\"36\"/></w:rPr>\n"
        "  </w:style>\n"
        "  <w:style w:type=\"paragraph\" w:styleId=\"Heading2\">\n"
        "    <w:name w:val=\"heading 2\"/>\n"
        "    <w:basedOn w:val=\"Normal\"/>\n"
        "    <w:pPr><w:jc w:val=\"center\"/></w:pPr>\n"
        "    <w:rPr><w:b/><w:sz w:val=\"28\"/><w:szCs w:val=\"28\"/></w:rPr>\n"
        "  </w:style>\n"
        "</w:styles>"});

    // word/document.xml
    entries.push_back({"word/document.xml",
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">\n"
        "<w:body>\n"
        "<w:sectPr>\n"
        "  <w:pgMar w:top=\"" + std::to_string(style_margin_tw) +
        "\" w:right=\"" + std::to_string(style_margin_tw) +
        "\" w:bottom=\"" + std::to_string(style_margin_tw) +
        "\" w:left=\"" + std::to_string(style_margin_tw) + "\"/>\n"
        "</w:sectPr>\n"
        + body +
        "</w:body>\n"
        "</w:document>"});

    // ── Write ZIP using existing infrastructure ───────────────────────────────
    // Re-use the same ZIP writer as write_zip() but return bytes instead of
    // writing to file — build inline since write_zip writes to disk.
    std::vector<uint8_t> zip;

    auto wu16 = [&](uint16_t v) {
        zip.push_back(v & 0xFF); zip.push_back((v >> 8) & 0xFF);
    };
    auto wu32 = [&](uint32_t v) {
        zip.push_back(v & 0xFF); zip.push_back((v >> 8) & 0xFF);
        zip.push_back((v >> 16) & 0xFF); zip.push_back((v >> 24) & 0xFF);
    };
    auto wbytes = [&](const void* src, size_t n) {
        const auto* p = static_cast<const uint8_t*>(src);
        zip.insert(zip.end(), p, p + n);
    };

    uint16_t dos_time = 0, dos_date = (20 << 9) | (1 << 5) | 1;

    struct ZE { uint32_t off, crc, csz, usz; uint16_t method; std::string name; };
    std::vector<ZE> zes;

    for (const auto& e : entries) {
        ZE ze;
        ze.name = e.filename;
        ze.off  = (uint32_t)zip.size();
        ze.usz  = (uint32_t)e.content.size();
        ze.crc  = (uint32_t)crc32(0,
            reinterpret_cast<const Bytef*>(e.content.data()), (uInt)e.content.size());

        uLongf bound = compressBound((uLong)e.content.size());
        std::vector<uint8_t> cb(bound);
        uLongf csz = bound;
        bool deflated = (compress2(cb.data(), &csz,
            reinterpret_cast<const Bytef*>(e.content.data()),
            (uLong)e.content.size(), Z_DEFAULT_COMPRESSION) == Z_OK)
            && csz > 6 && (csz - 6) < e.content.size();

        if (deflated) { ze.method = 8; ze.csz = (uint32_t)(csz - 6); }
        else          { ze.method = 0; ze.csz = ze.usz; }

        wu32(0x04034b50); wu16(20); wu16(0); wu16(ze.method);
        wu16(dos_time); wu16(dos_date);
        wu32(ze.crc); wu32(ze.csz); wu32(ze.usz);
        wu16((uint16_t)ze.name.size()); wu16(0);
        wbytes(ze.name.data(), ze.name.size());
        if (deflated) wbytes(cb.data() + 2, ze.csz);
        else          wbytes(e.content.data(), e.content.size());

        zes.push_back(ze);
    }

    uint32_t cd_start = (uint32_t)zip.size();
    for (const auto& ze : zes) {
        wu32(0x02014b50); wu16(20); wu16(20); wu16(0); wu16(ze.method);
        wu16(dos_time); wu16(dos_date);
        wu32(ze.crc); wu32(ze.csz); wu32(ze.usz);
        wu16((uint16_t)ze.name.size()); wu16(0); wu16(0);
        wu16(0); wu16(0); wu32(0); wu32(ze.off);
        wbytes(ze.name.data(), ze.name.size());
    }
    uint32_t cd_size = (uint32_t)zip.size() - cd_start;
    wu32(0x06054b50); wu16(0); wu16(0);
    wu16((uint16_t)zes.size()); wu16((uint16_t)zes.size());
    wu32(cd_size); wu32(cd_start); wu16(0);

    return zip;
}

// ─────────────────────────────────────────────────────────────────────────────
// compile_odt  — OpenDocument Text (.odt) ZIP
// Produces a minimal but fully conformant ODF 1.2 text document.
// ─────────────────────────────────────────────────────────────────────────────

// Convert stored HTML for one scene into ODF <text:p> elements.
static std::string html_to_odt_paras(const std::string& html,
                                      const Folio::ExportOptions& /*opts*/) {
    if (html.empty())
        return "<text:p text:style-name=\"Text_Body\"/>\n";

    std::string out;
    std::string cur_text;
    bool bold = false, italic = false, underline = false;
    bool in_tag = false;
    std::string tag_buf;

    // Flush accumulated plain text as a run inside a paragraph.
    // We build one <text:p> per HTML <p>/<br> boundary.
    std::vector<std::tuple<std::string,bool,bool,bool>> runs; // text,b,i,u
    auto flush_run = [&]() {
        if (!cur_text.empty()) {
            runs.emplace_back(cur_text, bold, italic, underline);
            cur_text.clear();
        }
    };
    auto flush_para = [&]() {
        flush_run();
        if (runs.empty()) {
            out += "<text:p text:style-name=\"Text_Body\"/>\n";
            return;
        }
        out += "<text:p text:style-name=\"Text_Body\">";
        for (auto& [txt, b, i, u] : runs) {
            bool styled = b || i || u;
            if (styled) {
                std::string sn = std::string(b?"B":"") + (i?"I":"") + (u?"U":"");
                out += "<text:span text:style-name=\"Inline_" + sn + "\">";
            }
            out += xml_escape(txt);
            if (styled) out += "</text:span>";
        }
        out += "</text:p>\n";
        runs.clear();
    };

    size_t i = 0;
    while (i < html.size()) {
        if (html[i] != '<') {
            if (!in_tag) {
                if (html[i] == '\n') { /* skip — </p> handles boundaries */ }
                else cur_text += html[i];
            }
            ++i; continue;
        }
        size_t et = html.find('>', i);
        if (et == std::string::npos) { cur_text += html[i++]; continue; }
        std::string tc = html.substr(i + 1, et - i - 1);
        i = et + 1;
        if (tc.empty()) continue;
        bool closing = (tc[0] == '/');
        if (closing) tc = tc.substr(1);
        std::string tname;
        size_t sp = tc.find_first_of(" \t");
        tname = (sp != std::string::npos) ? tc.substr(0, sp) : tc;
        for (auto& c : tname) c = std::tolower((unsigned char)c);

        if (!closing) {
            if (tname == "p") { /* opening <p>: start accumulating, no flush */ }
            else if (tname == "br") flush_para();
            else if (tname == "b" || tname == "strong") { flush_run(); bold = true; }
            else if (tname == "i" || tname == "em")     { flush_run(); italic = true; }
            else if (tname == "u")                      { flush_run(); underline = true; }
        } else {
            if (tname == "p")                           flush_para();
            else if (tname == "b" || tname == "strong") { flush_run(); bold = false; }
            else if (tname == "i" || tname == "em")     { flush_run(); italic = false; }
            else if (tname == "u")                      { flush_run(); underline = false; }
        }
    }
    flush_para();
    return out;
}

std::vector<uint8_t> Exporter::compile_odt(const std::vector<SourceNode>& nodes,
                                             const ExportOptions& opts,
                                             const std::string& title,
                                             const std::string& author) {
    // ── Build document body ───────────────────────────────────────────────────
    std::string body;
    bool first = true;
    int odt_group_counter = 0;

    std::string style_font = opts.flatten ? opts.flatten_font : "Times New Roman";
    int style_size_pt      = opts.flatten ? opts.flatten_size_pt : 12;
    double style_ls        = opts.flatten ? opts.flatten_line_spacing : 2.0;
    int style_margin_mm    = opts.flatten ? (int)(opts.flatten_margin_pt * 0.353) : 25; // pt→mm
    bool first_line_indent = opts.first_line_indent && opts.first_line_indent_px > 0;
    // Convert px indent to mm (96dpi → 25.4mm/in)
    double indent_mm = first_line_indent ? opts.first_line_indent_px * 25.4 / 96.0 : 0.0;

    char indent_buf[32] = "";
    if (first_line_indent)
        std::snprintf(indent_buf, sizeof(indent_buf), "fo:text-indent=\"%.2fmm\" ", indent_mm);

    for (const auto& node : nodes) {
        if (node.is_group) {
            bool has_heading = (opts.group_heading_style != ExportOptions::GroupHeadingStyle::NoHeading);
            if (!has_heading && !opts.include_group_content) continue;

            if (opts.page_break_on_group && !first)
                body += "<text:p text:style-name=\"Page_Break\"/>\n";

            std::string heading = group_heading_text(node, opts, ++odt_group_counter);
            if (!heading.empty()) {
                std::string style = (node.depth == 0) ? "Heading_1" : "Heading_2";
                body += "<text:h text:style-name=\"" + style + "\" text:outline-level=\""
                      + std::to_string(node.depth + 1) + "\">"
                      + xml_escape(heading) + "</text:h>\n";
                first = false;
            }
            if (opts.include_group_content && !node.html_content.empty()) {
                body += html_to_odt_paras(node.html_content, opts);
                first = false;
            }
            continue;
        }

        if (!first && !opts.scene_separator.empty()) {
            body += "<text:p text:style-name=\"Separator\">"
                  + xml_escape(opts.scene_separator) + "</text:p>\n";
            if (opts.separator_own_line)
                body += "<text:p text:style-name=\"Text_Body\"/>\n";
        }

        body += html_to_odt_paras(node.html_content, opts);
        first = false;
    }

    // ── styles.xml ────────────────────────────────────────────────────────────
    char ls_buf[32];
    std::snprintf(ls_buf, sizeof(ls_buf), "%.0f%%", style_ls * 100.0);
    std::string margin_s = std::to_string(style_margin_mm) + "mm";

    std::string styles =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<office:document-styles"
        " xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\""
        " xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\""
        " xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\""
        " xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\""
        " office:version=\"1.2\">\n"
        "<office:styles>\n"
        "  <style:default-style style:family=\"paragraph\">\n"
        "    <style:paragraph-properties fo:margin-left=\"" + margin_s + "\""
        " fo:margin-right=\"" + margin_s + "\"/>\n"
        "    <style:text-properties fo:font-family=\"" + xml_escape(style_font) + "\""
        " fo:font-size=\"" + std::to_string(style_size_pt) + "pt\"/>\n"
        "  </style:default-style>\n"
        "  <style:style style:name=\"Text_Body\" style:family=\"paragraph\""
        "   style:display-name=\"Text Body\">\n"
        "    <style:paragraph-properties fo:line-height=\"" + std::string(ls_buf) + "\""
        " " + std::string(indent_buf) + "/>\n"
        "  </style:style>\n"
        "  <style:style style:name=\"Heading_1\" style:family=\"paragraph\""
        "   style:display-name=\"Heading 1\">\n"
        "    <style:paragraph-properties fo:text-align=\"center\""
        " fo:margin-top=\"12pt\" fo:margin-bottom=\"6pt\"/>\n"
        "    <style:text-properties fo:font-size=\"18pt\" fo:font-weight=\"bold\"/>\n"
        "  </style:style>\n"
        "  <style:style style:name=\"Heading_2\" style:family=\"paragraph\""
        "   style:display-name=\"Heading 2\">\n"
        "    <style:paragraph-properties fo:text-align=\"center\""
        " fo:margin-top=\"10pt\" fo:margin-bottom=\"4pt\"/>\n"
        "    <style:text-properties fo:font-size=\"14pt\" fo:font-weight=\"bold\"/>\n"
        "  </style:style>\n"
        "  <style:style style:name=\"Separator\" style:family=\"paragraph\""
        "   style:display-name=\"Separator\">\n"
        "    <style:paragraph-properties fo:text-align=\"center\""
        " fo:margin-top=\"6pt\" fo:margin-bottom=\"6pt\"/>\n"
        "  </style:style>\n"
        "  <style:style style:name=\"Page_Break\" style:family=\"paragraph\">\n"
        "    <style:paragraph-properties fo:break-before=\"page\"/>\n"
        "  </style:style>\n"
        // Inline span styles for bold/italic/underline combinations
        "  <style:style style:name=\"Inline_B\" style:family=\"text\">"
        "<style:text-properties fo:font-weight=\"bold\"/></style:style>\n"
        "  <style:style style:name=\"Inline_I\" style:family=\"text\">"
        "<style:text-properties fo:font-style=\"italic\"/></style:style>\n"
        "  <style:style style:name=\"Inline_U\" style:family=\"text\">"
        "<style:text-properties style:text-underline-style=\"solid\""
        " style:text-underline-width=\"auto\""
        " style:text-underline-color=\"font-color\"/></style:style>\n"
        "  <style:style style:name=\"Inline_BI\" style:family=\"text\">"
        "<style:text-properties fo:font-weight=\"bold\""
        " fo:font-style=\"italic\"/></style:style>\n"
        "  <style:style style:name=\"Inline_BU\" style:family=\"text\">"
        "<style:text-properties fo:font-weight=\"bold\""
        " style:text-underline-style=\"solid\""
        " style:text-underline-width=\"auto\""
        " style:text-underline-color=\"font-color\"/></style:style>\n"
        "  <style:style style:name=\"Inline_IU\" style:family=\"text\">"
        "<style:text-properties fo:font-style=\"italic\""
        " style:text-underline-style=\"solid\""
        " style:text-underline-width=\"auto\""
        " style:text-underline-color=\"font-color\"/></style:style>\n"
        "  <style:style style:name=\"Inline_BIU\" style:family=\"text\">"
        "<style:text-properties fo:font-weight=\"bold\""
        " fo:font-style=\"italic\""
        " style:text-underline-style=\"solid\""
        " style:text-underline-width=\"auto\""
        " style:text-underline-color=\"font-color\"/></style:style>\n"
        "</office:styles>\n"
        "</office:document-styles>\n";

    // ── content.xml ───────────────────────────────────────────────────────────
    std::string content =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<office:document-content"
        " xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\""
        " xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\""
        " xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\""
        " xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\""
        " office:version=\"1.2\">\n"
        "<office:body><office:text>\n"
        + body +
        "</office:text></office:body>\n"
        "</office:document-content>\n";

    // ── meta.xml ──────────────────────────────────────────────────────────────
    std::string meta =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<office:document-meta"
        " xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\""
        " xmlns:dc=\"http://purl.org/dc/elements/1.1/\""
        " xmlns:meta=\"urn:oasis:names:tc:opendocument:xmlns:meta:1.0\""
        " office:version=\"1.2\">\n"
        "<office:meta>\n"
        "  <dc:title>" + xml_escape(title) + "</dc:title>\n"
        "  <dc:creator>" + xml_escape(author) + "</dc:creator>\n"
        "  <meta:generator>Folio</meta:generator>\n"
        "</office:meta>\n"
        "</office:document-meta>\n";

    // ── ODF file entries ──────────────────────────────────────────────────────
    // NOTE: mimetype MUST be first entry, stored uncompressed (method=0),
    // with no extra field — this is an ODF spec requirement.
    std::vector<ExportEntry> entries;
    entries.push_back({"mimetype",
        "application/vnd.oasis.opendocument.text"});
    entries.push_back({"META-INF/manifest.xml",
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<manifest:manifest"
        " xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\""
        " manifest:version=\"1.2\">\n"
        "  <manifest:file-entry manifest:full-path=\"/\""
        " manifest:media-type=\"application/vnd.oasis.opendocument.text\"/>\n"
        "  <manifest:file-entry manifest:full-path=\"content.xml\""
        " manifest:media-type=\"text/xml\"/>\n"
        "  <manifest:file-entry manifest:full-path=\"styles.xml\""
        " manifest:media-type=\"text/xml\"/>\n"
        "  <manifest:file-entry manifest:full-path=\"meta.xml\""
        " manifest:media-type=\"text/xml\"/>\n"
        "</manifest:manifest>\n"});
    entries.push_back({"content.xml", content});
    entries.push_back({"styles.xml",  styles});
    entries.push_back({"meta.xml",    meta});

    // ── Write ZIP ─────────────────────────────────────────────────────────────
    std::vector<uint8_t> zip;

    auto wu16 = [&](uint16_t v) {
        zip.push_back(v & 0xFF); zip.push_back((v >> 8) & 0xFF);
    };
    auto wu32 = [&](uint32_t v) {
        zip.push_back(v & 0xFF); zip.push_back((v >> 8) & 0xFF);
        zip.push_back((v >> 16) & 0xFF); zip.push_back((v >> 24) & 0xFF);
    };
    auto wbytes = [&](const void* src, size_t n) {
        const auto* p = static_cast<const uint8_t*>(src);
        zip.insert(zip.end(), p, p + n);
    };

    uint16_t dos_time = 0, dos_date = (20 << 9) | (1 << 5) | 1;

    struct ZE { uint32_t off, crc, csz, usz; uint16_t method; std::string name; };
    std::vector<ZE> zes;

    for (size_t ei = 0; ei < entries.size(); ++ei) {
        const auto& e = entries[ei];
        ZE ze;
        ze.name = e.filename;
        ze.off  = (uint32_t)zip.size();
        ze.usz  = (uint32_t)e.content.size();
        ze.crc  = (uint32_t)crc32(0,
            reinterpret_cast<const Bytef*>(e.content.data()), (uInt)e.content.size());

        // mimetype (entry 0) must be stored uncompressed per ODF spec
        bool force_store = (ei == 0);
        bool deflated = false;
        std::vector<uint8_t> cb;

        if (!force_store) {
            uLongf bound = compressBound((uLong)e.content.size());
            cb.resize(bound);
            uLongf csz = bound;
            deflated = (compress2(cb.data(), &csz,
                reinterpret_cast<const Bytef*>(e.content.data()),
                (uLong)e.content.size(), Z_DEFAULT_COMPRESSION) == Z_OK)
                && csz > 6 && (csz - 6) < e.content.size();
            if (deflated) ze.csz = (uint32_t)(csz - 6);
        }
        if (!deflated) { ze.method = 0; ze.csz = ze.usz; }
        else             ze.method = 8;

        wu32(0x04034b50); wu16(20); wu16(0); wu16(ze.method);
        wu16(dos_time); wu16(dos_date);
        wu32(ze.crc); wu32(ze.csz); wu32(ze.usz);
        wu16((uint16_t)ze.name.size()); wu16(0);
        wbytes(ze.name.data(), ze.name.size());
        if (deflated) wbytes(cb.data() + 2, ze.csz);
        else          wbytes(e.content.data(), e.content.size());

        zes.push_back(ze);
    }

    uint32_t cd_start = (uint32_t)zip.size();
    for (const auto& ze : zes) {
        wu32(0x02014b50); wu16(20); wu16(20); wu16(0); wu16(ze.method);
        wu16(dos_time); wu16(dos_date);
        wu32(ze.crc); wu32(ze.csz); wu32(ze.usz);
        wu16((uint16_t)ze.name.size()); wu16(0); wu16(0);
        wu16(0); wu16(0); wu32(0); wu32(ze.off);
        wbytes(ze.name.data(), ze.name.size());
    }
    uint32_t cd_size = (uint32_t)zip.size() - cd_start;
    wu32(0x06054b50); wu16(0); wu16(0);
    wu16((uint16_t)zes.size()); wu16((uint16_t)zes.size());
    wu32(cd_size); wu32(cd_start); wu16(0);

    return zip;
}

} // namespace Folio
