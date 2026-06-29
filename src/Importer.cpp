// ─────────────────────────────────────────────────────────────────────────────
// Folio — Importer.cpp
// ─────────────────────────────────────────────────────────────────────────────
#include "Importer.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <zlib.h>

namespace Folio {

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// Small utilities
// ─────────────────────────────────────────────────────────────────────────────

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string to_lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// HTML-escape a plain-text string.
static std::string html_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if      (c == '&')  out += "&amp;";
        else if (c == '<')  out += "&lt;";
        else if (c == '>')  out += "&gt;";
        else if (c == '"')  out += "&quot;";
        else                out += static_cast<char>(c);
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Path helpers
// ─────────────────────────────────────────────────────────────────────────────

std::string Importer::extension(const std::string& path) {
    fs::path p(path);
    std::string ext = p.extension().string();
    return to_lower(ext);
}

std::string Importer::stem(const std::string& path) {
    return fs::path(path).stem().string();
}

std::string Importer::dir_basename(const std::string& path) {
    return fs::path(path).filename().string();
}

std::string Importer::title_from_stem(const std::string& s) {
    // Replace underscores and hyphens with spaces, then capitalise first letter.
    std::string t = s;
    for (char& c : t) if (c == '_' || c == '-') c = ' ';
    if (!t.empty()) t[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(t[0])));
    return t;
}

bool Importer::is_supported_extension(const std::string& ext) {
    return ext == ".txt" || ext == ".md"   || ext == ".markdown" ||
           ext == ".rtf" || ext == ".docx" || ext == ".odt";
}

// ─────────────────────────────────────────────────────────────────────────────
// File I/O
// ─────────────────────────────────────────────────────────────────────────────

std::vector<uint8_t> Importer::read_binary(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return { std::istreambuf_iterator<char>(f),
             std::istreambuf_iterator<char>() };
}

std::string Importer::read_text(const std::string& path) {
    std::ifstream f(path);
    if (!f) return {};
    return { std::istreambuf_iterator<char>(f),
             std::istreambuf_iterator<char>() };
}

// ─────────────────────────────────────────────────────────────────────────────
// Minimal ZIP reader  (PKZIP local-file-header traversal)
// Supports stored (method 0) and deflated (method 8) entries.
// ─────────────────────────────────────────────────────────────────────────────

static uint16_t read_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}
static uint32_t read_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0])        |
           (static_cast<uint32_t>(p[1]) <<  8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

// s87 — Read entries via the ZIP CENTRAL DIRECTORY instead of walking local
// file headers. LibreOffice ODT writes some entries (e.g. styles.xml) with a
// DATA DESCRIPTOR: the local header's comp/uncomp sizes are 0 and the real
// sizes live AFTER the compressed data. Walking local headers then can't tell
// where an entry ends, so the scan derails (it would read mid-stream bytes as
// the next header) and content.xml — which sits after styles.xml — is never
// found. The central directory, at the end of the archive, always carries
// accurate sizes plus each entry's local-header offset, so it is the correct
// place to read from.
std::vector<uint8_t> Importer::zip_extract_entry(const std::vector<uint8_t>& zip,
                                                  const std::string& entry_name) {
    const size_t sz = zip.size();
    if (sz < 22) return {};  // smaller than an EOCD record

    // 1. Locate the End Of Central Directory record (sig 0x06054b50). A trailing
    //    comment of up to 65535 bytes may follow it, so scan backward.
    const size_t scan_floor = (sz > (22u + 65535u)) ? (sz - (22u + 65535u)) : 0u;
    size_t eocd = sz;  // sentinel = not found
    for (size_t i = sz - 22;; --i) {
        if (read_u32(zip.data() + i) == 0x06054b50) { eocd = i; break; }
        if (i == scan_floor) break;
    }
    if (eocd == sz) {  return {}; }

    const uint16_t total_entries = read_u16(zip.data() + eocd + 10);
    const uint32_t cd_offset     = read_u32(zip.data() + eocd + 16);

    // 2. Walk the central directory (each header sig 0x02014b50).
    size_t pos = cd_offset;
    for (unsigned n = 0; n < total_entries; ++n) {
        if (pos + 46 > sz) break;
        if (read_u32(zip.data() + pos) != 0x02014b50) break;

        const uint16_t method    = read_u16(zip.data() + pos + 10);
        const uint32_t comp_size = read_u32(zip.data() + pos + 20);
        const uint32_t uncomp_sz = read_u32(zip.data() + pos + 24);
        const uint16_t name_len  = read_u16(zip.data() + pos + 28);
        const uint16_t extra_len = read_u16(zip.data() + pos + 30);
        const uint16_t cmt_len   = read_u16(zip.data() + pos + 32);
        const uint32_t lho       = read_u32(zip.data() + pos + 42);  // local hdr offset

        if (pos + 46 + name_len > sz) break;
        std::string name(reinterpret_cast<const char*>(zip.data() + pos + 46), name_len);

        if (name == entry_name) {
            // The local header at lho carries its own name/extra lengths (which
            // can differ from the central dir), so recompute the data offset.
            if (static_cast<size_t>(lho) + 30 > sz) return {};
            if (read_u32(zip.data() + lho) != 0x04034b50) return {};
            const uint16_t l_name_len  = read_u16(zip.data() + lho + 26);
            const uint16_t l_extra_len = read_u16(zip.data() + lho + 28);
            const size_t data_off =
                static_cast<size_t>(lho) + 30 + l_name_len + l_extra_len;
            if (data_off + comp_size > sz) return {};

            if (method == 0) {  // Stored
                return { zip.begin() + static_cast<std::ptrdiff_t>(data_off),
                         zip.begin() + static_cast<std::ptrdiff_t>(data_off + comp_size) };
            }
            if (method == 8) {  // Deflate — comp_size from the central dir is accurate
                z_stream zs{};
                zs.next_in  = const_cast<uint8_t*>(zip.data() + data_off);
                zs.avail_in = static_cast<uInt>(comp_size);
                if (inflateInit2(&zs, -15) != Z_OK) return {};
                size_t cap = (uncomp_sz > 0 && uncomp_sz < (256u << 20))
                                 ? static_cast<size_t>(uncomp_sz)
                                 : (static_cast<size_t>(1) << 16);
                std::vector<uint8_t> out(cap);
                int r = Z_OK;
                for (;;) {
                    if (zs.total_out >= out.size()) out.resize(out.size() * 2);
                    zs.next_out  = out.data() + zs.total_out;
                    zs.avail_out = static_cast<uInt>(out.size() - zs.total_out);
                    r = inflate(&zs, Z_NO_FLUSH);
                    if (r == Z_STREAM_END) break;
                    if (r != Z_OK) break;
                    if (zs.avail_in == 0 && zs.avail_out > 0) break;
                }
                const uLong produced = zs.total_out;
                inflateEnd(&zs);
                if (r != Z_STREAM_END) return {};
                out.resize(produced);
                return out;
            }
            return {};  // unsupported method
        }

        pos += static_cast<size_t>(46) + name_len + extra_len + cmt_len;
    }
    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
// plain_to_html  — wrap each non-empty line in <p>…</p>
// ─────────────────────────────────────────────────────────────────────────────

std::string Importer::plain_to_html(const std::string& plain) {
    std::string out;
    std::istringstream ss(plain);
    std::string line;
    while (std::getline(ss, line)) {
        std::string t = trim(line);
        if (t.empty()) continue;
        out += "<p>" + html_escape(t) + "</p>";
    }
    if (out.empty()) out = "<p></p>";
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// split_plain  — divide a block of plain text into ImportNodes
// ─────────────────────────────────────────────────────────────────────────────

std::vector<ImportNode> Importer::split_plain(const std::string& plain,
                                              const std::string& file_stem,
                                              const ImportOptions& opts) {
    std::vector<ImportNode> nodes;

    // Collect lines
    std::vector<std::string> lines;
    {
        std::istringstream ss(plain);
        std::string l;
        while (std::getline(ss, l)) lines.push_back(l);
    }

    // Split on separator
    std::vector<std::vector<std::string>> chunks;
    chunks.push_back({});
    for (auto& l : lines) {
        std::string t = trim(l);
        if (!opts.separator.empty() && t == opts.separator) {
            chunks.push_back({});
        } else {
            chunks.back().push_back(l);
        }
    }

    int seq = 1;
    for (auto& chunk : chunks) {
        // Drop leading/trailing blank lines in chunk
        size_t a = 0, b = chunk.size();
        while (a < b && trim(chunk[a]).empty()) ++a;
        while (b > a && trim(chunk[b-1]).empty()) --b;
        if (a == b) continue;

        std::string title;
        size_t content_start = a;

        switch (opts.title_source) {
        case ImportOptions::TitleSource::FirstLine: {
            // Use first non-empty line as title, strip it from body
            std::string first = trim(chunk[a]);
            if (!first.empty()) {
                title = first;
                content_start = a + 1;
                // Skip blank lines immediately after title
                while (content_start < b && trim(chunk[content_start]).empty())
                    ++content_start;
            }
            break;
        }
        case ImportOptions::TitleSource::Filename:
            title = title_from_stem(file_stem);
            if (chunks.size() > 1)
                title += " " + std::to_string(seq);
            break;
        case ImportOptions::TitleSource::Sequential:
            title = "Scene " + std::to_string(seq);
            break;
        }

        if (title.empty())
            title = "Scene " + std::to_string(seq);

        // Build body
        std::string body;
        for (size_t i = content_start; i < b; ++i) {
            body += chunk[i];
            body += '\n';
        }

        ImportNode nd;
        nd.title    = title;
        nd.html     = plain_to_html(body);
        nd.is_group = false;
        nd.depth    = 0;
        nodes.push_back(std::move(nd));
        ++seq;
    }

    return nodes;
}

// ─────────────────────────────────────────────────────────────────────────────
// parse_txt
// ─────────────────────────────────────────────────────────────────────────────

ImportResult Importer::parse_txt(const std::string& text,
                                 const std::string& s,
                                 const ImportOptions& opts) {
    ImportResult r;
    r.nodes = split_plain(text, s, opts);
    if (r.nodes.empty()) {
        ImportNode nd;
        nd.title = title_from_stem(s);
        nd.html  = "<p></p>";
        r.nodes.push_back(nd);
    }
    return r;
}

// ─────────────────────────────────────────────────────────────────────────────
// parse_md  — Markdown with optional H1/H2 hierarchy
// ─────────────────────────────────────────────────────────────────────────────

ImportResult Importer::parse_md(const std::string& text,
                                const std::string& file_stem,
                                const ImportOptions& opts) {
    ImportResult r;

    if (!opts.md_headings_as_hierarchy) {
        // Fall back to plain splitting (separator or whole-file)
        return parse_txt(text, file_stem, opts);
    }

    // Heading-aware parse:
    //   # Title  → Group node (depth 0)
    //   ## Title → Scene node inside last group (depth 1)
    //   No heading found at all → fall back to separator splitting
    std::vector<std::string> lines;
    {
        std::istringstream ss(text);
        std::string l;
        while (std::getline(ss, l)) lines.push_back(l);
    }

    // Check whether the document contains any ATX headings
    bool has_headings = false;
    for (auto& l : lines) {
        std::string t = trim(l);
        if (!t.empty() && t[0] == '#') { has_headings = true; break; }
    }
    if (!has_headings) {
        return parse_txt(text, file_stem, opts);
    }

    // Heading-structured parse
    struct Chunk {
        std::string heading;
        int         level = 0;   // 1 or 2+
        std::vector<std::string> body_lines;
    };
    std::vector<Chunk> chunks;
    chunks.push_back({ "", 0, {} }); // preamble before first heading

    for (auto& l : lines) {
        std::string t = trim(l);
        if (!t.empty() && t[0] == '#') {
            // Count hashes
            int level = 0;
            while (level < (int)t.size() && t[level] == '#') ++level;
            std::string htext = trim(t.substr(level));
            chunks.push_back({ htext, level, {} });
        } else {
            chunks.back().body_lines.push_back(l);
        }
    }

    // Convert chunks to ImportNodes
    // H1 → Group; H2+ → Scene child of last group (depth 1)
    // preamble (level 0) → Scene at depth 0
    int seq = 1;
    for (auto& ch : chunks) {
        // Skip empty preamble
        bool body_empty = true;
        for (auto& bl : ch.body_lines)
            if (!trim(bl).empty()) { body_empty = false; break; }

        if (ch.level == 0) {
            // Preamble chunk
            if (body_empty) continue;
            ImportNode nd;
            nd.title    = title_from_stem(file_stem);
            nd.is_group = false;
            nd.depth    = 0;
            // Build plain body
            std::string body;
            for (auto& bl : ch.body_lines) body += bl + "\n";
            nd.html = plain_to_html(body);
            r.nodes.push_back(std::move(nd));
        } else if (ch.level == 1) {
            // Group
            ImportNode nd;
            nd.title    = ch.heading.empty() ? ("Part " + std::to_string(seq++)) : ch.heading;
            nd.is_group = true;
            nd.depth    = 0;
            nd.html     = "<p></p>";
            r.nodes.push_back(std::move(nd));
        } else {
            // Scene
            ImportNode nd;
            nd.title    = ch.heading.empty() ? ("Scene " + std::to_string(seq++)) : ch.heading;
            nd.is_group = false;
            // depth = 1 if a group preceded us, else 0
            nd.depth = 0;
            for (int i = (int)r.nodes.size() - 1; i >= 0; --i) {
                if (r.nodes[i].is_group && r.nodes[i].depth == 0) {
                    nd.depth = 1;
                    break;
                }
            }
            std::string body;
            for (auto& bl : ch.body_lines) body += bl + "\n";
            nd.html = body_empty ? "<p></p>" : plain_to_html(body);
            r.nodes.push_back(std::move(nd));
        }
    }

    if (r.nodes.empty()) {
        ImportNode nd;
        nd.title = title_from_stem(file_stem);
        nd.html  = "<p></p>";
        r.nodes.push_back(nd);
    }
    return r;
}

// ─────────────────────────────────────────────────────────────────────────────
// rtf_to_plain  — strip RTF control words/groups → plain UTF-8 text
// Handles \par, \pard, Unicode escapes (\uN?), and basic \'xx hex escapes.
// Not a full RTF parser — covers typical word-processor output.
// ─────────────────────────────────────────────────────────────────────────────

std::string Importer::rtf_to_plain(const std::string& rtf) {
    std::string out;
    size_t i = 0;
    const size_t n = rtf.size();
    int skip_chars = 0;       // after \uN, skip the fallback char
    [[maybe_unused]] int group_depth = 0;
    // Track "destination" groups we should skip (\fonttbl, \colortbl, \stylesheet, etc.)
    // We use a simple depth counter for skip-groups.
    int skip_depth = 0;

    while (i < n) {
        char c = rtf[i];

        if (c == '{') {
            ++group_depth;
            ++i;
            // Check if next token is a skip-destination control word
            if (skip_depth > 0) { ++skip_depth; }
            else {
                // Look for \* or known skip destinations
                size_t j = i;
                while (j < n && rtf[j] == ' ') ++j;
                if (j + 1 < n && rtf[j] == '\\' && rtf[j+1] == '*') {
                    // \* means the following destination is ignorable
                    skip_depth = 1;
                    i = j + 2; // skip \*
                } else if (j + 9 < n && rtf.substr(j, 9) == "\\fonttbl") {
                    skip_depth = 1;
                } else if (j + 9 < n && rtf.substr(j, 9) == "\\colortbl") {
                    skip_depth = 1;
                } else if (j + 11 < n && rtf.substr(j, 11) == "\\stylesheet") {
                    skip_depth = 1;
                } else if (j + 5 < n && rtf.substr(j, 5) == "\\info") {
                    skip_depth = 1;
                } else if (j + 8 < n && rtf.substr(j, 8) == "\\pict") {
                    skip_depth = 1;
                }
            }
            continue;
        }

        if (c == '}') {
            --group_depth;
            if (skip_depth > 0) --skip_depth;
            ++i;
            continue;
        }

        if (skip_depth > 0) { ++i; continue; }

        if (c == '\\') {
            ++i;
            if (i >= n) break;
            char nc = rtf[i];

            // Escaped special chars
            if (nc == '\\' || nc == '{' || nc == '}') {
                if (skip_chars > 0) { --skip_chars; }
                else out += nc;
                ++i; continue;
            }
            if (nc == '\n' || nc == '\r') {
                // \<newline> = paragraph break
                out += '\n';
                ++i; continue;
            }
            // Hex escape \'xx
            if (nc == '\'') {
                ++i;
                if (i + 1 < n) {
                    char hi = rtf[i], lo = rtf[i+1];
                    auto hex = [](char h) -> int {
                        if (h >= '0' && h <= '9') return h - '0';
                        if (h >= 'a' && h <= 'f') return h - 'a' + 10;
                        if (h >= 'A' && h <= 'F') return h - 'A' + 10;
                        return 0;
                    };
                    unsigned char byte = static_cast<unsigned char>((hex(hi) << 4) | hex(lo));
                    if (skip_chars > 0) { --skip_chars; }
                    else {
                        // Latin-1 passthrough (good enough for most RTF)
                        if (byte < 0x80) out += static_cast<char>(byte);
                        else {
                            // Encode as UTF-8 (Latin-1 supplement U+0080..U+00FF)
                            out += static_cast<char>(0xC0 | (byte >> 6));
                            out += static_cast<char>(0x80 | (byte & 0x3F));
                        }
                    }
                    i += 2;
                }
                continue;
            }

            // Control word or control symbol
            if (!std::isalpha(static_cast<unsigned char>(nc))) {
                ++i; continue; // control symbol we don't care about
            }

            // Read control word
            std::string word;
            while (i < n && std::isalpha(static_cast<unsigned char>(rtf[i])))
                word += rtf[i++];
            // Optional numeric parameter
            bool neg = false;
            if (i < n && rtf[i] == '-') { neg = true; ++i; }
            std::string numstr;
            while (i < n && std::isdigit(static_cast<unsigned char>(rtf[i])))
                numstr += rtf[i++];
            // Optional trailing space (consumed but not emitted)
            if (i < n && rtf[i] == ' ') ++i;

            // s87 — parse the numeric parameter WITHOUT std::stoi. Real RTF
            // routinely carries values beyond INT_MAX (\rsidN are random 32-bit,
            // plus large \fsN / colour values), and stoi throws std::out_of_range
            // on those. That exception unwinds through GTK's C signal dispatch
            // (undefined behaviour — observed as a "free(): invalid pointer"
            // abort), so parse with saturation instead. param is only consumed
            // for \uN, which never approaches the cap.
            long long val = 0;
            for (char d : numstr) {
                val = val * 10 + (d - '0');
                if (val > 1000000000LL) val = 1000000000LL;  // saturate
            }
            int param = neg ? -static_cast<int>(val) : static_cast<int>(val);

            if (word == "par" || word == "pard" || word == "line") {
                out += '\n';
            } else if (word == "tab") {
                out += '\t';
            } else if (word == "u") {
                // Unicode character \uN — emit as UTF-8
                int32_t cp = param; // can be negative (signed 16-bit)
                if (cp < 0) cp += 65536;
                if (cp < 0x80) {
                    out += static_cast<char>(cp);
                } else if (cp < 0x800) {
                    out += static_cast<char>(0xC0 | (cp >> 6));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                } else {
                    out += static_cast<char>(0xE0 | (cp >> 12));
                    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
                skip_chars = 1; // skip RTF fallback char
            }
            // All other control words (bold, italic, etc.) — ignored for plain text
            continue;
        }

        // Plain character
        if (skip_chars > 0) { --skip_chars; ++i; continue; }

        if (c == '\n' || c == '\r') {
            // Bare newlines in RTF are ignored (not paragraph breaks)
            ++i; continue;
        }
        out += c;
        ++i;
    }

    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// parse_rtf
// ─────────────────────────────────────────────────────────────────────────────

ImportResult Importer::parse_rtf(const std::string& rtf_bytes,
                                 const std::string& s,
                                 const ImportOptions& opts) {
    std::string plain = rtf_to_plain(rtf_bytes);
    return parse_txt(plain, s, opts);
}

// ─────────────────────────────────────────────────────────────────────────────
// docx_xml_to_plain  — word/document.xml → plain text
// Converts <w:p> to paragraph breaks, <w:t> contents to text.
// ─────────────────────────────────────────────────────────────────────────────

std::string Importer::docx_xml_to_plain(const std::string& xml) {
    std::string out;
    size_t i = 0;
    const size_t n = xml.size();

    // Track whether we are inside a <w:t> run
    // We emit a newline at every </w:p>
    std::string tag_buf;
    bool in_tag = false;
    bool in_wt  = false; // inside <w:t>
    bool in_del = false; // inside <w:del> (tracked-deleted text — skip)

    auto decode = [](const std::string& e) -> std::string {
        if (e == "&amp;")  return "&";
        if (e == "&lt;")   return "<";
        if (e == "&gt;")   return ">";
        if (e == "&quot;") return "\"";
        if (e == "&apos;") return "'";
        if (e == "&#160;") return "\xc2\xa0"; // NBSP → UTF-8
        return e;
    };

    while (i < n) {
        char c = xml[i];
        if (c == '<') {
            in_tag = true;
            tag_buf.clear();
            ++i; continue;
        }
        if (c == '>') {
            in_tag = false;
            std::string t = trim(tag_buf);

            if (t == "w:t" || t.substr(0,4) == "w:t ") {
                in_wt = true;
            } else if (t == "/w:t") {
                in_wt = false;
            } else if (t == "/w:p") {
                out += '\n';
                in_wt = false;
            } else if (t == "w:del" || t.substr(0,6) == "w:del ") {
                in_del = true;
            } else if (t == "/w:del") {
                in_del = false;
            }
            // w:tab
            else if (t == "w:tab") {
                if (!in_del) out += '\t';
            }
            ++i; continue;
        }
        if (in_tag) {
            tag_buf += c;
            ++i; continue;
        }

        // Text content
        if (in_wt && !in_del) {
            // Handle XML entities
            if (c == '&') {
                size_t semi = xml.find(';', i);
                if (semi != std::string::npos && semi - i <= 8) {
                    out += decode(xml.substr(i, semi - i + 1));
                    i = semi + 1;
                    continue;
                }
            }
            out += c;
        }
        ++i;
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// odt_xml_to_plain  — content.xml → plain text
// Converts <text:p> to paragraph breaks, character data to text.
// ─────────────────────────────────────────────────────────────────────────────

std::string Importer::odt_xml_to_plain(const std::string& xml) {
    std::string out;
    size_t i = 0;
    const size_t n = xml.size();

    bool in_tag  = false;
    bool in_body = false; // inside <office:text>
    bool in_para = false; // inside <text:p> or <text:h>
    std::string tag_buf;

    auto decode = [](const std::string& e) -> std::string {
        if (e == "&amp;")  return "&";
        if (e == "&lt;")   return "<";
        if (e == "&gt;")   return ">";
        if (e == "&quot;") return "\"";
        if (e == "&apos;") return "'";
        if (e == "&#160;") return "\xc2\xa0";
        return e;
    };

    while (i < n) {
        char c = xml[i];
        if (c == '<') {
            in_tag = true;
            tag_buf.clear();
            ++i; continue;
        }
        if (c == '>') {
            in_tag = false;
            std::string t = trim(tag_buf);

            if (t == "office:text" || t.substr(0,12) == "office:text ") {
                in_body = true;
            } else if (t == "/office:text") {
                in_body = false;
            } else if (in_body && (t == "text:p" || t.substr(0,7) == "text:p " ||
                                   t == "text:h" || t.substr(0,7) == "text:h ")) {
                in_para = true;
            } else if (in_body && (t == "/text:p" || t == "/text:h")) {
                out += '\n';
                in_para = false;
            } else if (in_body && in_para && (t == "text:tab")) {
                out += '\t';
            } else if (in_body && in_para &&
                       (t == "text:line-break" || t == "text:s")) {
                out += ' ';
            }
            ++i; continue;
        }
        if (in_tag) { tag_buf += c; ++i; continue; }

        if (in_body && in_para) {
            if (c == '&') {
                size_t semi = xml.find(';', i);
                if (semi != std::string::npos && semi - i <= 8) {
                    out += decode(xml.substr(i, semi - i + 1));
                    i = semi + 1;
                    continue;
                }
            }
            out += c;
        }
        ++i;
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// parse_docx
// ─────────────────────────────────────────────────────────────────────────────

ImportResult Importer::parse_docx(const std::vector<uint8_t>& zip_bytes,
                                  const std::string& s,
                                  const ImportOptions& opts) {
    auto xml_bytes = zip_extract_entry(zip_bytes, "word/document.xml");
    if (xml_bytes.empty()) {
        ImportResult r;
        r.error = "Could not read word/document.xml from DOCX archive.";
        return r;
    }
    std::string xml(reinterpret_cast<const char*>(xml_bytes.data()), xml_bytes.size());
    std::string plain = docx_xml_to_plain(xml);
    return parse_txt(plain, s, opts);
}

// ─────────────────────────────────────────────────────────────────────────────
// ODT structured import (s88)
//
// Map the ODT outline to Folio's binder, preserving inline emphasis:
//   <text:h outline-level="1">  → Group  (chapter)        depth 0
//   <text:h outline-level="2+"> → Scene  (child)          depth 1
//   <text:p>                    → a <p> paragraph in the current scene
//   <text:span style=italic>    → <i>…</i>   (style flagged italic)
//   <text:span style=bold>      → <b>…</b>   (style flagged bold)
// Outline level is keyed off `text:outline-level` (robust, style-independent —
// what LibreOffice always sets); style names are only a fallback. A document
// with no headings returns found_heading=false so parse_odt can fall back to the
// flat separator-split path. Pure; sandbox-tested against a real content.xml.
// ─────────────────────────────────────────────────────────────────────────────

// Collect automatic/text style names that carry italic or bold, by scanning the
// <style:style> blocks' <style:text-properties> for fo:font-style / fo:font-weight.
static void odt_collect_emphasis_styles(const std::string& xml,
                                        std::set<std::string>& italic,
                                        std::set<std::string>& bold) {
    size_t pos = 0;
    while (true) {
        size_t s = xml.find("<style:style", pos);
        if (s == std::string::npos) break;
        size_t hdr_end = xml.find('>', s);
        if (hdr_end == std::string::npos) break;
        size_t e = xml.find("</style:style>", s);

        std::string open = xml.substr(s, hdr_end - s);
        std::string name;
        size_t np = open.find("style:name=\"");
        if (np != std::string::npos) {
            np += 12;
            size_t nq = open.find('"', np);
            if (nq != std::string::npos) name = open.substr(np, nq - np);
        }
        std::string block = (e == std::string::npos) ? open : xml.substr(s, e - s);
        if (!name.empty()) {
            if (block.find("fo:font-style=\"italic\"")  != std::string::npos)
                italic.insert(name);
            if (block.find("fo:font-weight=\"bold\"")   != std::string::npos)
                bold.insert(name);
        }
        pos = (e == std::string::npos) ? hdr_end + 1 : e + 14;
    }
}

static std::string odt_decode_entity(const std::string& e) {
    if (e == "&amp;")  return "&";
    if (e == "&lt;")   return "<";
    if (e == "&gt;")   return ">";
    if (e == "&quot;") return "\"";
    if (e == "&apos;") return "'";
    if (e == "&#160;") return "\xc2\xa0";
    return e;
}

static std::vector<ImportNode> odt_to_nodes(const std::string& xml,
                                            bool& found_heading) {
    found_heading = false;
    std::set<std::string> italic_styles, bold_styles;
    odt_collect_emphasis_styles(xml, italic_styles, bold_styles);

    std::vector<ImportNode> nodes;
    int  cur_scene  = -1;     // index into nodes of the scene we're filling
    bool have_group = false;

    bool in_body = false, in_para = false, in_head = false;
    int  head_level = 1;
    std::string head_text;    // plain (decoded) heading text → node title
    std::string para_html;    // inner HTML of the current paragraph
    bool para_italic = false, para_bold = false;
    std::vector<std::string> span_close;  // per-open-span closing tags

    auto append_char_data = [&](const std::string& decoded) {
        if (in_head)       head_text += decoded;            // titles stay plain
        else if (in_para)  para_html += html_escape(decoded); // body → HTML-safe
    };

    size_t i = 0, n = xml.size();
    bool in_tag = false;
    std::string tag;

    while (i < n) {
        char c = xml[i];
        if (c == '<') { in_tag = true; tag.clear(); ++i; continue; }
        if (c == '>') {
            in_tag = false;
            std::string t = trim(tag);
            if (!t.empty() && t.back() == '/') { t.pop_back(); t = trim(t); }

            std::string name = t, attrs;
            size_t sp = t.find_first_of(" \t");
            if (sp != std::string::npos) { name = t.substr(0, sp); attrs = t.substr(sp + 1); }

            auto attr = [&](const char* key) -> std::string {
                std::string k = std::string(key) + "=\"";
                size_t p = attrs.find(k);
                if (p == std::string::npos) return "";
                p += k.size();
                size_t q = attrs.find('"', p);
                if (q == std::string::npos) return "";
                return attrs.substr(p, q - p);
            };

            if      (name == "office:text")  in_body = true;
            else if (name == "/office:text") in_body = false;

            else if (in_body && name == "text:h") {
                in_head = true;
                head_text.clear();
                std::string lv = attr("text:outline-level");
                if (!lv.empty()) {
                    head_level = std::atoi(lv.c_str());
                } else {
                    std::string st = attr("text:style-name");   // fallback
                    head_level = (st.find("Heading_20_2") != std::string::npos ||
                                  st.find("Heading 2")    != std::string::npos) ? 2 : 1;
                }
                if (head_level < 1) head_level = 1;
            }
            else if (in_body && name == "/text:h") {
                in_head = false;
                found_heading = true;
                std::string title = trim(head_text);
                if (head_level <= 1) {
                    ImportNode g;
                    g.is_group = true; g.depth = 0;
                    g.title = title.empty() ? "Chapter" : title;
                    g.html  = "<p></p>";
                    nodes.push_back(std::move(g));
                    have_group = true;
                    cur_scene  = -1;
                } else {
                    ImportNode s;
                    s.is_group = false;
                    s.depth    = have_group ? 1 : 0;
                    s.title    = title.empty() ? "Scene" : title;
                    s.html     = "";
                    nodes.push_back(std::move(s));
                    cur_scene  = (int)nodes.size() - 1;
                }
            }
            else if (in_body && name == "text:p") {
                in_para = true;
                para_html.clear();
                span_close.clear();
                std::string st = attr("text:style-name");
                para_italic = italic_styles.count(st) > 0;
                para_bold   = bold_styles.count(st)   > 0;
            }
            else if (in_body && name == "/text:p") {
                in_para = false;
                if (cur_scene < 0) {           // prose before any scene heading
                    ImportNode s;
                    s.is_group = false;
                    s.depth    = have_group ? 1 : 0;
                    s.title    = "Scene";
                    s.html     = "";
                    nodes.push_back(std::move(s));
                    cur_scene  = (int)nodes.size() - 1;
                }
                std::string open, close;
                if (para_bold)   { open += "<b>"; close = "</b>" + close; }
                if (para_italic) { open += "<i>"; close = "</i>" + close; }
                nodes[(size_t)cur_scene].html += "<p>" + open + para_html + close + "</p>";
            }
            else if (in_body && in_para && name == "text:span") {
                std::string st = attr("text:style-name");
                std::string opened;
                if (bold_styles.count(st))   { para_html += "<b>"; opened = "</b>" + opened; }
                if (italic_styles.count(st)) { para_html += "<i>"; opened = "</i>" + opened; }
                span_close.push_back(opened);
            }
            else if (in_body && in_para && name == "/text:span") {
                if (!span_close.empty()) { para_html += span_close.back(); span_close.pop_back(); }
            }
            else if (in_body && in_para && name == "text:tab") {
                para_html += " ";
            }
            else if (in_body && in_para && name == "text:line-break") {
                para_html += " ";
            }
            else if (in_body && in_para && name == "text:s") {
                int cnt = 1;
                std::string cc = attr("text:c");
                if (!cc.empty()) cnt = std::max(1, std::atoi(cc.c_str()));
                for (int k = 0; k < cnt; ++k) para_html += " ";
            }
            ++i; continue;
        }
        if (in_tag) { tag += c; ++i; continue; }

        if (in_body && (in_para || in_head)) {
            if (c == '&') {
                size_t semi = xml.find(';', i);
                if (semi != std::string::npos && semi - i <= 8) {
                    append_char_data(odt_decode_entity(xml.substr(i, semi - i + 1)));
                    i = semi + 1;
                    continue;
                }
            }
            append_char_data(std::string(1, c));
        }
        ++i;
    }

    for (auto& nd : nodes)
        if (!nd.is_group && nd.html.empty()) nd.html = "<p></p>";

    return nodes;
}

// ─────────────────────────────────────────────────────────────────────────────
// parse_odt
// ─────────────────────────────────────────────────────────────────────────────

ImportResult Importer::parse_odt(const std::vector<uint8_t>& zip_bytes,
                                 const std::string& s,
                                 const ImportOptions& opts) {
    auto xml_bytes = zip_extract_entry(zip_bytes, "content.xml");
    if (xml_bytes.empty()) {
        ImportResult r;
        r.error = "Could not read content.xml from ODT archive.";
        return r;
    }
    std::string xml(reinterpret_cast<const char*>(xml_bytes.data()), xml_bytes.size());

    // Structured path: chapters (outline 1) + scenes (outline 2) + italic/bold.
    bool found_heading = false;
    auto nodes = odt_to_nodes(xml, found_heading);
    if (found_heading && !nodes.empty()) {
        ImportResult r;
        r.nodes = std::move(nodes);
        return r;
    }

    // No headings → fall back to flat separator-split (same model as Markdown).
    std::string plain = odt_xml_to_plain(xml);
    return parse_txt(plain, s, opts);
}

// ─────────────────────────────────────────────────────────────────────────────
// import_file  — public entry point
// ─────────────────────────────────────────────────────────────────────────────

ImportResult Importer::import_file(const std::string& path,
                                   const ImportOptions& opts) {
    std::string ext  = extension(path);
    std::string st   = stem(path);

    if (ext == ".txt") {
        std::string text = read_text(path);
        if (text.empty() && !fs::exists(path)) {
            ImportResult r; r.error = "File not found: " + path; return r;
        }
        return parse_txt(text, st, opts);
    }

    if (ext == ".md" || ext == ".markdown") {
        std::string text = read_text(path);
        return parse_md(text, st, opts);
    }

    if (ext == ".rtf") {
        std::string text = read_text(path);
        return parse_rtf(text, st, opts);
    }

    if (ext == ".docx") {
        auto bytes = read_binary(path);
        if (bytes.empty()) {
            ImportResult r; r.error = "Cannot read file: " + path; return r;
        }
        return parse_docx(bytes, st, opts);
    }

    if (ext == ".odt") {
        auto bytes = read_binary(path);
        if (bytes.empty()) {
            ImportResult r; r.error = "Cannot read file: " + path; return r;
        }
        return parse_odt(bytes, st, opts);
    }
    ImportResult r;
    r.error = "Unsupported file type: " + ext;
    return r;
}

// ─────────────────────────────────────────────────────────────────────────────
// import_folder  — public entry point
// ─────────────────────────────────────────────────────────────────────────────

ImportResult Importer::import_folder(const std::string& folder_path,
                                     const ImportOptions& opts) {
    ImportResult combined;

    // Collect supported files, sorted by name
    std::vector<std::string> files;
    try {
        for (auto& entry : fs::directory_iterator(folder_path)) {
            if (!entry.is_regular_file()) continue;
            std::string ext = to_lower(entry.path().extension().string());
            if (is_supported_extension(ext))
                files.push_back(entry.path().string());
        }
    } catch (const std::exception& ex) {
        combined.error = ex.what();
        return combined;
    }
    std::sort(files.begin(), files.end());

    if (files.empty()) {
        combined.error = "No supported files found in folder.";
        return combined;
    }

    // Optional wrapper group
    if (opts.folder_as_group) {
        ImportNode grp;
        grp.title    = title_from_stem(dir_basename(folder_path));
        grp.is_group = true;
        grp.depth    = 0;
        grp.html     = "<p></p>";
        combined.nodes.push_back(std::move(grp));
    }

    int depth_offset = opts.folder_as_group ? 1 : 0;

    for (auto& f : files) {
        ImportResult fr = import_file(f, opts);
        if (!fr.ok()) {
            // Non-fatal: skip the file but record a note in the title
            ImportNode err_node;
            err_node.title    = "[Import error: " + stem(f) + "]";
            err_node.html     = "<p>" + html_escape(fr.error) + "</p>";
            err_node.is_group = false;
            err_node.depth    = depth_offset;
            combined.nodes.push_back(std::move(err_node));
            continue;
        }
        for (auto& nd : fr.nodes) {
            nd.depth += depth_offset;
            combined.nodes.push_back(std::move(nd));
        }
    }

    return combined;
}

} // namespace Folio
