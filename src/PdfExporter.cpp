// ─────────────────────────────────────────────────────────────────────────────
// PdfExporter.cpp — F-PDF compile/export module (s16, step 1)
//
// Step-1 scope: the pure-logic scan adapter + coverage preflight. This TU pulls
// CompileFormat.hpp and ExportScan.hpp into the Folio build via their first real
// consumer. GTK-free — compiles standalone (verified g++ -std=gnu++20 -Wall
// -Wextra) and inside Folio.
//
// The paginator (Cairo/Pango, RenderMode::Adaptable then Formal) is the next
// step and lands in this same module once the entry-point / surface-setup forks
// are settled (see the s16 design check-in).
// ─────────────────────────────────────────────────────────────────────────────

#include "PdfExporter.hpp"

#include <cctype>    // std::isspace, std::isdigit
#include <cstdlib>   // std::strtod, std::atoi

namespace Folio {

ScanReport scan_source_nodes(const std::vector<Exporter::SourceNode>& nodes) {
    std::vector<std::string> htmls;
    htmls.reserve(nodes.size());
    for (const auto& n : nodes)
        htmls.push_back(n.html_content);
    return scan_export(htmls);
}

PreflightResult pdf_export_preflight(const std::vector<Exporter::SourceNode>& nodes) {
    PreflightResult pr;
    pr.report      = scan_source_nodes(nodes);
    pr.unsupported = pr.report.unknown_elements;
    return pr;
}

const CompileFormat& builtin_format_at(const std::vector<CompileFormat>& fmts,
                                       std::size_t index) {
    if (fmts.empty()) {
        // Should never happen — builtin_compile_formats() always returns three.
        // A function-local static keeps a valid reference to return.
        static const CompileFormat fallback{};
        return fallback;
    }
    if (index >= fmts.size())
        index = 0;
    return fmts[index];
}

// ─── Resolver ────────────────────────────────────────────────────────────────
namespace {

// A slot counts as "unfilled" when it carries no styling of its own — i.e. it
// equals a default-constructed TextFormat. Such a slot means "the format didn't
// style this kind," so the paragraph falls back to body. (CompileFormat.hpp's
// foundation is left untouched; we compare fields locally rather than adding an
// operator== to the model header.)
bool tf_is_inherit_all(const TextFormat& t) {
    return t.font_family.empty()
        && t.font_size_pt   == 0.0
        && !t.bold && !t.italic && !t.underline
        && t.align          == Align::Left
        && t.line_spacing   == 0.0
        && t.space_above_pt == 0.0
        && t.space_below_pt == 0.0
        && t.indent_left_pt == 0.0
        && t.indent_right_pt== 0.0
        && t.first_line_pt  == 0.0
        && t.text_case      == TextCase::AsIs
        && t.color_hex.empty();
}

// Fill zero/empty typographic fields from the body default (inherit chain:
// slot → body → [doc base, filled later by the renderer]).
void inherit_fill_from_body(TextFormat& out, const TextFormat& body) {
    if (out.font_family.empty()) out.font_family = body.font_family;
    if (out.font_size_pt == 0.0) out.font_size_pt = body.font_size_pt;
    if (out.line_spacing == 0.0) out.line_spacing = body.line_spacing;
    if (out.color_hex.empty())   out.color_hex   = body.color_hex;
}

}  // namespace

SlotKind select_slot(const DocParaTags& p) {
    // data-sp wins over data-ol; neither → body. (Structural, mode-independent.)
    if (p.has_screenplay) {
        int n = p.screenplay_index;
        if (n >= 0 && n < static_cast<int>(ScreenplayElement::COUNT))
            return SlotKind{SlotKind::Screenplay, n};
        return SlotKind{SlotKind::Body, 0};
    }
    if (p.has_outline) {
        int l = p.outline_level;
        if (l >= 0 && l < OUTLINE_LEVELS)
            return SlotKind{SlotKind::Heading, l};
        return SlotKind{SlotKind::Body, 0};
    }
    return SlotKind{SlotKind::Body, 0};
}

TextFormat resolve_paragraph(const CompileFormat& fmt, const DocParaTags& p) {
    const TextFormat& body = fmt.elements.body;
    SlotKind s = select_slot(p);

    TextFormat slot = body;
    if (s.kind == SlotKind::Heading)
        slot = fmt.elements.heading[s.index];
    else if (s.kind == SlotKind::Screenplay)
        slot = fmt.elements.screenplay[s.index];

    // Unfilled non-body slot → fall back to the body format.
    if (s.kind != SlotKind::Body && tf_is_inherit_all(slot))
        slot = body;

    TextFormat out = slot;
    inherit_fill_from_body(out, body);

    // Mode rule (Reading B): Adaptable lets the document's paragraph-level
    // expressive tags override; Formal ignores them entirely (slot wins).
    if (fmt.mode == RenderMode::Adaptable) {
        if (p.has_align)        out.align          = p.align;
        if (p.has_line_spacing) out.line_spacing   = p.line_spacing;
        if (p.has_indent_left)  out.indent_left_pt = p.indent_left_pt;
        if (p.has_indent_right) out.indent_right_pt= p.indent_right_pt;
    }
    return out;
}

ResolvedRun resolve_run(const CompileFormat& fmt,
                        const TextFormat&    para_fmt,
                        const DocRunTags&    run) {
    ResolvedRun r;
    // Base typography from the resolved paragraph/slot format.
    r.bold         = para_fmt.bold;
    r.italic       = para_fmt.italic;
    r.underline    = para_fmt.underline;
    r.strike       = false;                 // TextFormat has no strike; slot can't request it
    r.font_family  = para_fmt.font_family;
    r.font_size_pt = para_fmt.font_size_pt;
    r.fg_hex       = para_fmt.color_hex;
    r.bg_hex       = "";

    // Reading B: Formal drops the run's document tags; the run inherits the slot
    // typography unchanged. Adaptable layers the document's run tags on top.
    if (fmt.mode == RenderMode::Adaptable) {
        if (run.bold)      r.bold      = true;
        if (run.italic)    r.italic    = true;
        if (run.underline) r.underline = true;
        if (run.strike)    r.strike    = true;
        if (run.has_fg)    r.fg_hex    = run.fg_hex;
        if (run.has_bg)    r.bg_hex    = run.bg_hex;
        if (run.has_font) {
            r.font_family  = run.font_family;
            r.font_size_pt = run.font_size_pt;
        }
    }
    return r;
}

// ─── HTML parser ─────────────────────────────────────────────────────────────
namespace {

constexpr double PX_TO_PT = 72.0 / 96.0;   // CSS px (1/96") → typographic pt (1/72")

std::string decode_entities(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    std::size_t j = 0;
    while (j < s.size()) {
        if (s[j] == '&') {
            if      (s.compare(j, 5, "&amp;")  == 0) { out += '&'; j += 5; }
            else if (s.compare(j, 4, "&lt;")   == 0) { out += '<'; j += 4; }
            else if (s.compare(j, 4, "&gt;")   == 0) { out += '>'; j += 4; }
            else if (s.compare(j, 6, "&quot;") == 0) { out += '"'; j += 6; }
            else                                     { out += s[j++]; }
        } else {
            out += s[j++];
        }
    }
    return out;
}

// Pull the value of attr="..." from a tag's inner text. Returns "" if absent.
std::string attr_value(const std::string& tag, const std::string& attr) {
    std::string key = attr + "=\"";
    auto p = tag.find(key);
    if (p == std::string::npos) return "";
    p += key.size();
    auto q = tag.find('"', p);
    if (q == std::string::npos) return "";
    return tag.substr(p, q - p);
}

// Parse leading numeric (handles a trailing unit or '%'). '%' divides by 100.
double parse_leading_number(const std::string& s) {
    std::size_t i = 0;
    while (i < s.size() && (std::isspace((unsigned char)s[i]))) ++i;
    std::size_t start = i;
    while (i < s.size() && (std::isdigit((unsigned char)s[i]) || s[i] == '.' ||
                            s[i] == '-' || s[i] == '+'))
        ++i;
    if (i == start) return 0.0;
    double v = std::strtod(s.substr(start, i - start).c_str(), nullptr);
    if (s.find('%') != std::string::npos) v /= 100.0;
    return v;
}

// A frame on the inline open-tag stack. PARA carries no run styling (it was a
// paragraph-level span lifted onto the paragraph); the rest are inline.
struct Frame {
    enum Type { B, I, U, S, FG, BG, FONT, NAMED, LINK, PARA } type;
    std::string s1;            // hex / family / style name / link target
    double      d1 = 0.0;      // font size pt
};

}  // namespace

int screenplay_index_of(const std::string& type) {
    static const char* names[] = {"scene", "action", "character",
                                  "parenthetical", "dialogue", "transition"};
    for (int i = 0; i < static_cast<int>(ScreenplayElement::COUNT); ++i)
        if (type == names[i]) return i;
    return -1;
}

bool ParsedParagraph::is_blank() const {
    for (const auto& r : runs)
        for (unsigned char c : r.text)
            if (!std::isspace(c)) return false;
    return true;
}

std::vector<ParsedParagraph> parse_html(const std::string& html) {
    std::vector<ParsedParagraph> paras;

    // Plain text with no markup at all → a single body paragraph.
    if (html.find('<') == std::string::npos) {
        if (!html.empty()) {
            ParsedParagraph pp;
            ParsedRun r; r.text = decode_entities(html);
            pp.runs.push_back(r);
            paras.push_back(std::move(pp));
        }
        return paras;
    }

    ParsedParagraph*   cur_para = nullptr;
    std::vector<Frame> stack;
    std::string        text_buf;

    auto ensure_para = [&]() {
        if (!cur_para) {
            paras.emplace_back();
            cur_para = &paras.back();
        }
    };

    // Flush accumulated text as a run carrying the current inline state.
    auto flush_text = [&]() {
        if (text_buf.empty()) return;
        ensure_para();
        ParsedRun r;
        r.text = decode_entities(text_buf);
        for (const auto& f : stack) {
            switch (f.type) {
                case Frame::B: r.bold = true; break;
                case Frame::I: r.italic = true; break;
                case Frame::U: r.underline = true; break;
                case Frame::S: r.strike = true; break;
                case Frame::FG: r.has_fg = true; r.fg_hex = f.s1; break;
                case Frame::BG: r.has_bg = true; r.bg_hex = f.s1; break;
                case Frame::FONT: r.has_font = true; r.font_family = f.s1; r.font_size_pt = f.d1; break;
                case Frame::NAMED: r.named_style = f.s1; break;
                case Frame::LINK:  r.link_target = f.s1; break;
                case Frame::PARA:  break;  // paragraph-level, no run effect
            }
        }
        cur_para->runs.push_back(std::move(r));
        text_buf.clear();
    };

    std::size_t i = 0;
    while (i < html.size()) {
        if (html[i] != '<') { text_buf += html[i++]; continue; }
        std::size_t gt = html.find('>', i);
        if (gt == std::string::npos) { text_buf += html[i++]; continue; }
        std::string tag = html.substr(i + 1, gt - i - 1);   // inner text of <...>
        i = gt + 1;

        bool closing = (!tag.empty() && tag[0] == '/');
        // Element name (lowercased) up to first space or end.
        std::string name;
        {
            std::size_t k = closing ? 1 : 0;
            while (k < tag.size() && tag[k] != ' ' && tag[k] != '/')
                { name += static_cast<char>(tag[k] | 0x20); ++k; }
        }

        if (name == "p") {
            flush_text();
            if (closing) {
                cur_para = nullptr;
                stack.clear();              // paragraphs don't carry runs across
            } else {
                paras.emplace_back();
                cur_para = &paras.back();
                stack.clear();
                // Structural attributes.
                std::string sp = attr_value(tag, "data-sp");
                std::string ol = attr_value(tag, "data-ol");
                std::string an = attr_value(tag, "data-anchor");
                if (!sp.empty()) {
                    int idx = screenplay_index_of(sp);
                    if (idx >= 0) { cur_para->tags.has_screenplay = true;
                                    cur_para->tags.screenplay_index = idx; }
                } else if (!ol.empty()) {
                    cur_para->tags.has_outline = true;
                    cur_para->tags.outline_level = std::atoi(ol.c_str());
                }
                if (!an.empty()) cur_para->anchor = an;
            }
            continue;
        }

        // Inline elements.
        if (closing) {
            flush_text();
            // Pop the innermost frame whose element matches this close.
            // (Serializer always closes in reverse open order, so the top frame
            //  of the matching kind is the right one.)
            for (int s = static_cast<int>(stack.size()) - 1; s >= 0; --s) {
                Frame::Type ft = stack[s].type;
                bool match =
                    (name == "b" && ft == Frame::B) ||
                    (name == "i" && ft == Frame::I) ||
                    (name == "u" && ft == Frame::U) ||
                    (name == "s" && ft == Frame::S) ||
                    (name == "a" && ft == Frame::LINK) ||
                    (name == "span" && (ft == Frame::FG || ft == Frame::BG ||
                                        ft == Frame::FONT || ft == Frame::NAMED ||
                                        ft == Frame::PARA));
                if (match) { stack.erase(stack.begin() + s); break; }
            }
            continue;
        }

        // Opening inline tag.
        flush_text();
        ensure_para();
        if      (name == "b") stack.push_back({Frame::B, "", 0});
        else if (name == "i") stack.push_back({Frame::I, "", 0});
        else if (name == "u") stack.push_back({Frame::U, "", 0});
        else if (name == "s") stack.push_back({Frame::S, "", 0});
        else if (name == "a") {
            stack.push_back({Frame::LINK, attr_value(tag, "data-folio-link"), 0});
        } else if (name == "span") {
            std::string named = attr_value(tag, "data-folio-style");
            if (!named.empty()) {
                stack.push_back({Frame::NAMED, named, 0});
            } else {
                std::string style = attr_value(tag, "style");
                // Split into prop:value pairs on ';'.
                bool is_para_span = false;
                std::size_t pos = 0;
                std::string fam; double fsz = 0.0; bool have_font = false;
                while (pos < style.size()) {
                    std::size_t semi = style.find(';', pos);
                    std::string piece = style.substr(pos, semi == std::string::npos
                                                          ? std::string::npos : semi - pos);
                    pos = (semi == std::string::npos) ? style.size() : semi + 1;
                    std::size_t colon = piece.find(':');
                    if (colon == std::string::npos) continue;
                    std::string prop = piece.substr(0, colon);
                    std::string val  = piece.substr(colon + 1);
                    // trim
                    auto trim = [](std::string& x){
                        while (!x.empty() && std::isspace((unsigned char)x.front())) x.erase(x.begin());
                        while (!x.empty() && std::isspace((unsigned char)x.back()))  x.pop_back();
                    };
                    trim(prop); trim(val);

                    if (prop == "text-align") {
                        cur_para->tags.has_align = true;
                        if      (val == "center")  cur_para->tags.align = Align::Center;
                        else if (val == "right")   cur_para->tags.align = Align::Right;
                        else if (val == "justify") cur_para->tags.align = Align::Justify;
                        else                       cur_para->tags.align = Align::Left;
                        is_para_span = true;
                    } else if (prop == "line-height") {
                        cur_para->tags.has_line_spacing = true;
                        cur_para->tags.line_spacing = parse_leading_number(val);
                        is_para_span = true;
                    } else if (prop == "margin-left") {
                        cur_para->tags.has_indent_left = true;
                        cur_para->tags.indent_left_pt = parse_leading_number(val) * PX_TO_PT;
                        is_para_span = true;
                    } else if (prop == "margin-right") {
                        cur_para->tags.has_indent_right = true;
                        cur_para->tags.indent_right_pt = parse_leading_number(val) * PX_TO_PT;
                        is_para_span = true;
                    } else if (prop == "color") {
                        stack.push_back({Frame::FG, val, 0});
                    } else if (prop == "background-color") {
                        stack.push_back({Frame::BG, val, 0});
                    } else if (prop == "font-family") {
                        // strip surrounding quotes
                        if (val.size() >= 2 && val.front() == '\'' && val.back() == '\'')
                            val = val.substr(1, val.size() - 2);
                        fam = val; have_font = true;
                    } else if (prop == "font-size") {
                        fsz = parse_leading_number(val); have_font = true;
                    }
                }
                if (have_font)
                    stack.push_back({Frame::FONT, fam, fsz});
                else if (is_para_span)
                    stack.push_back({Frame::PARA, "", 0});   // balance the close
                // a span with neither (shouldn't happen) is simply ignored,
                // but still needs a frame to balance its closing tag:
                else
                    stack.push_back({Frame::PARA, "", 0});
            }
        }
        // Unknown elements: ignored here (the preflight scan flags them upstream).
    }
    flush_text();
    return paras;
}

}  // namespace Folio
