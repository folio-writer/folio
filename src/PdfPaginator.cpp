// ─────────────────────────────────────────────────────────────────────────────
// PdfPaginator.cpp — F-PDF render/layout layer (s16, step 3)
//
// Cairo/Pango layout over the verified parse_html + resolver core. See the
// header for scope. Heavily logged (FolioLog) so a first render that "runs but
// looks wrong" leaves a trail to read alongside the visual output.
//
// NOT compiled in the Claude sandbox (gtkmm/Cairo/Pango). API choices match
// idioms already in the Folio tree (PrintDialog draw path; Editor_build's
// pango_cairo_* + cobj()/gobj() bridge; Inspector's raw-cairo surfaces).
// ─────────────────────────────────────────────────────────────────────────────

#include "PdfPaginator.hpp"
#include "PdfExporter.hpp"          // parse_html, resolve_paragraph, resolve_run
#include "Hyphenator.hpp"          // hyphenate() — libhyphen pump (no-op w/o lib)
#include <FolioLog.hpp>

#include <cairomm/context.h>
#include <cairomm/surface.h>
#include <pangomm.h>             // Pango:: C++ wrappers (Layout, FontDescription, Alignment)
#include <pango/pangocairo.h>    // C: pango_cairo_create_layout / set_resolution
#include <glibmm.h>              // Glib::wrap(PangoLayout*) overload, Glib::RefPtr

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace Folio {
namespace {

// ── Geometry ─────────────────────────────────────────────────────────────────
void page_dimensions(const PageSpec& p, double& w, double& h) {
    switch (p.size) {
        case PaperSize::Letter: w = 612.0;     h = 792.0;    break;  // 8.5×11"
        case PaperSize::A4:     w = 595.276;   h = 841.890;  break;  // 210×297mm
        case PaperSize::Legal:  w = 612.0;     h = 1008.0;   break;  // 8.5×14"
        case PaperSize::Custom: w = p.custom_w_pt; h = p.custom_h_pt; break;
    }
    if (p.orientation == Orientation::Landscape) std::swap(w, h);
    if (w <= 0 || h <= 0) { w = 612.0; h = 792.0; }   // defensive fallback
}

// Left/right margins for a 1-based page, honoring mirrored (facing-page) layout.
// Page 1 is a recto (right-hand) page: on recto the inner margin is the left.
void page_margins(const CompileFormat& fmt, int page_number_1based,
                  double& left, double& right, double& top, double& bottom) {
    const PageSpec& p = fmt.page;
    top    = p.margin_top_pt;
    bottom = p.margin_bottom_pt;
    if (p.mirror_margins) {
        bool recto = (page_number_1based % 2 == 1);
        left  = recto ? p.margin_inner_pt : p.margin_outer_pt;
        right = recto ? p.margin_outer_pt : p.margin_inner_pt;
    } else {
        left  = p.margin_inner_pt;   // single-sided: inner==left, outer==right
        right = p.margin_outer_pt;
    }
}

// ── Text helpers ─────────────────────────────────────────────────────────────
std::string markup_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;";  break;
            case '<': out += "&lt;";   break;
            case '>': out += "&gt;";   break;
            default:  out += c;        break;
        }
    }
    return out;
}

std::string apply_case(std::string s, TextCase tc) {
    // ASCII casing for v1 (Unicode-aware casing is a TODO).
    if (tc == TextCase::Upper)
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c){ return std::toupper(c); });
    else if (tc == TextCase::Lower)
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c){ return std::tolower(c); });
    // Title/AsIs: leave as-is for now.
    return s;
}

std::string fmt_pt(double v) {
    std::ostringstream o;
    if (std::fabs(v - std::round(v)) < 1e-6) o << static_cast<long>(std::round(v));
    else                                     o << v;
    return o.str();
}

// Inject U+00AD soft hyphens into text at libhyphen's legal break points, word
// by word. Pango then breaks at them (drawing a hyphen glyph) only when a line
// would otherwise overflow; mid-line they stay invisible. A no-op when libhyphen
// is absent or no dictionary loads (hyphenate() returns no points) → text
// unchanged, plain wrapping. The soft hyphens live only in this transient render
// string; the document content is untouched.
std::string inject_soft_hyphens(const std::string& text) {
    static const std::string SHY = "\xC2\xAD";   // U+00AD, UTF-8
    std::string out;
    out.reserve(text.size() + 8);
    std::size_t i = 0, n = text.size();
    while (i < n) {
        if (std::isspace(static_cast<unsigned char>(text[i]))) { out += text[i++]; continue; }
        std::size_t start = i;
        while (i < n && !std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        std::string word = text.substr(start, i - start);
        std::vector<std::size_t> br = hyphenate(word, "en_US");
        if (br.empty()) { out += word; continue; }
        std::size_t prev = 0;
        for (std::size_t b : br) {
            if (b <= prev || b >= word.size()) continue;
            out += word.substr(prev, b - prev);
            out += SHY;
            prev = b;
        }
        out += word.substr(prev);
    }
    return out;
}

// Build Pango markup for one paragraph's runs under the resolved format.
std::string build_markup(const CompileFormat& fmt, const TextFormat& pf,
                         const std::vector<ParsedRun>& runs) {
    // Hyphenate only where loose justification actually hurts: Adaptable mode,
    // the format opts in, and the paragraph is justified. (Formal/Courier and
    // centered headings are left untouched.) "en_US" is the v1 language; a
    // document/locale-driven tag is the follow-up.
    const bool hy = fmt.hyphenate
                 && fmt.mode == RenderMode::Adaptable
                 && pf.align == Align::Justify;
    std::string out;
    for (const auto& pr : runs) {
        // Parsed run → DocRunTags (named-style deref TODO: would fill these from
        // the TextStyle table; skipped here, named runs fall back to slot type).
        DocRunTags rt;
        rt.bold = pr.bold; rt.italic = pr.italic;
        rt.underline = pr.underline; rt.strike = pr.strike;
        rt.has_fg = pr.has_fg; rt.fg_hex = pr.fg_hex;
        rt.has_bg = pr.has_bg; rt.bg_hex = pr.bg_hex;
        rt.has_font = pr.has_font; rt.font_family = pr.font_family;
        rt.font_size_pt = pr.font_size_pt;

        ResolvedRun r = resolve_run(fmt, pf, rt);

        std::string opens, closes;
        auto wrap = [&](const std::string& o, const std::string& c) {
            opens += o; closes = c + closes;
        };
        if (r.bold)      wrap("<b>", "</b>");
        if (r.italic)    wrap("<i>", "</i>");
        if (r.underline) wrap("<u>", "</u>");
        if (r.strike)    wrap("<s>", "</s>");

        std::string span;
        if (!r.fg_hex.empty()) span += " foreground='" + markup_escape(r.fg_hex) + "'";
        if (!r.bg_hex.empty()) span += " background='" + markup_escape(r.bg_hex) + "'";
        // Font span only when the RUN overrides the font (Adaptable mode). The
        // paragraph's base family/size is already on the layout via
        // set_font_description, so inherited size must NOT be re-emitted per run
        // (doing so stamps a malformed empty-family "font=' 11'" on every run).
        if (fmt.mode == RenderMode::Adaptable && rt.has_font) {
            std::string fd = rt.font_family;
            if (rt.font_size_pt > 0.0) fd += " " + fmt_pt(rt.font_size_pt);
            span += " font='" + markup_escape(fd) + "'";
        }
        if (!span.empty()) wrap("<span" + span + ">", "</span>");

        std::string text = hy ? inject_soft_hyphens(pr.text) : pr.text;
        out += opens + markup_escape(apply_case(text, pf.text_case)) + closes;
    }
    return out;
}

Pango::Alignment to_pango_align(Align a) {
    switch (a) {
        case Align::Center: return Pango::Alignment::CENTER;
        case Align::Right:  return Pango::Alignment::RIGHT;
        case Align::Left:
        case Align::Justify:
        default:            return Pango::Alignment::LEFT;
    }
}

// ── Layout factory ───────────────────────────────────────────────────────────
// Create a Pango layout bound to the cairo context with resolution pinned to 72
// DPI so Pango points map 1:1 to PDF points (default pango_cairo is 96 DPI,
// which would render text 1.333× too large on a points-based surface).
Glib::RefPtr<Pango::Layout> make_layout(const Cairo::RefPtr<Cairo::Context>& cr) {
    PangoLayout* raw = pango_cairo_create_layout(cr->cobj());
    pango_cairo_context_set_resolution(pango_layout_get_context(raw), 72.0);
    return Glib::wrap(raw);   // owns the new ref
}

double layout_height_pt(const Glib::RefPtr<Pango::Layout>& layout) {
    int w_pu = 0, h_pu = 0;
    layout->get_size(w_pu, h_pu);
    return static_cast<double>(h_pu) / PANGO_SCALE;
}

// Base font family fallback when a format leaves body family = "inherit"
// (e.g. the novel preset, which expects the editor's base font). TODO: source
// this from FolioPrefs instead of a hardcoded serif.
const char* BASE_FONT_FALLBACK = "Serif";

std::string font_desc_string(const TextFormat& pf) {
    std::string fam = pf.font_family.empty() ? BASE_FONT_FALLBACK : pf.font_family;
    double sz = pf.font_size_pt > 0.0 ? pf.font_size_pt : 11.0;
    return fam + " " + fmt_pt(sz);
}

// ── Furniture ────────────────────────────────────────────────────────────────
std::string substitute_tokens(const std::string& tmpl, const PdfDocInfo& info,
                              int page_number, const std::string& chapter) {
    std::string out;
    for (std::size_t i = 0; i < tmpl.size();) {
        if (tmpl[i] == '{') {
            auto close = tmpl.find('}', i);
            if (close != std::string::npos) {
                std::string tok = tmpl.substr(i + 1, close - i - 1);
                if      (tok == "title")   out += info.title;
                else if (tok == "author")  out += info.author;
                else if (tok == "page")    out += std::to_string(page_number);
                else if (tok == "chapter") out += chapter;
                else                       out += tmpl.substr(i, close - i + 1);
                i = close + 1;
                continue;
            }
        }
        out += tmpl[i++];
    }
    return out;
}

void draw_running_slot(const Cairo::RefPtr<Cairo::Context>& cr,
                       const std::string& text, double x, double y,
                       double box_w, Pango::Alignment align) {
    if (text.empty()) return;
    auto layout = make_layout(cr);
    layout->set_font_description(Pango::FontDescription("Serif 9"));
    layout->set_width(static_cast<int>(PANGO_SCALE * box_w));
    layout->set_alignment(align);
    layout->set_text(text);
    cr->move_to(x, y);
    cr->set_source_rgb(0, 0, 0);
    layout->show_in_cairo_context(cr);
}

void draw_furniture(const Cairo::RefPtr<Cairo::Context>& cr,
                    const CompileFormat& fmt, const PdfDocInfo& info,
                    int page_number, const std::string& chapter,
                    double page_w, double left, double right,
                    double top, double bottom, double page_h) {
    double box_w = page_w - left - right;
    if (fmt.furniture.header_enabled) {
        double hy = top * 0.5;   // baseline within the top margin band
        const auto& h = fmt.furniture.header;
        draw_running_slot(cr, substitute_tokens(h.left,   info, page_number, chapter), left, hy, box_w, Pango::Alignment::LEFT);
        draw_running_slot(cr, substitute_tokens(h.center, info, page_number, chapter), left, hy, box_w, Pango::Alignment::CENTER);
        draw_running_slot(cr, substitute_tokens(h.right,  info, page_number, chapter), left, hy, box_w, Pango::Alignment::RIGHT);
    }
    if (fmt.furniture.footer_enabled) {
        double fy = page_h - bottom * 0.6;
        const auto& f = fmt.furniture.footer;
        draw_running_slot(cr, substitute_tokens(f.left,   info, page_number, chapter), left, fy, box_w, Pango::Alignment::LEFT);
        draw_running_slot(cr, substitute_tokens(f.center, info, page_number, chapter), left, fy, box_w, Pango::Alignment::CENTER);
        draw_running_slot(cr, substitute_tokens(f.right,  info, page_number, chapter), left, fy, box_w, Pango::Alignment::RIGHT);
    }
}

// ── A flat render item: one paragraph to lay out ─────────────────────────────
struct RenderPara {
    DocParaTags            tags;
    std::vector<ParsedRun> runs;
    bool                   page_break_before = false;
    bool                   is_chapter_head   = false;
    std::string            chapter_title;   // set on chapter-head paras
};

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
std::string export_pdf(const std::vector<Exporter::SourceNode>& nodes,
                       const CompileFormat&                      fmt,
                       const PdfDocInfo&                         info,
                       const std::string&                        out_path) {
    LOG_INFO("F-PDF export start: format=\"{}\" mode={} out=\"{}\" nodes={}",
             fmt.name,
             fmt.mode == RenderMode::Adaptable ? "Adaptable" : "Formal",
             out_path, nodes.size());

    // ── Flatten nodes into an ordered paragraph list ─────────────────────────
    std::vector<RenderPara> paras;
    bool first_group = true;
    std::string current_chapter = info.title;

    for (const auto& node : nodes) {
        if (node.is_group) {
            RenderPara head;
            head.is_chapter_head = true;
            head.chapter_title   = node.title;
            head.tags.has_outline = true;
            head.tags.outline_level = 0;
            head.page_break_before = fmt.page_break_before_top_heading && !first_group;
            ParsedRun r; r.text = node.title;
            head.runs.push_back(r);
            paras.push_back(std::move(head));
            first_group = false;

            // Group preface body, if any.
            if (!node.html_content.empty()) {
                for (auto& p : parse_html(node.html_content)) {
                    if (p.is_blank()) continue;
                    RenderPara rp; rp.tags = p.tags; rp.runs = std::move(p.runs);
                    paras.push_back(std::move(rp));
                }
            }
        } else {
            for (auto& p : parse_html(node.html_content)) {
                if (p.is_blank()) continue;
                RenderPara rp; rp.tags = p.tags; rp.runs = std::move(p.runs);
                paras.push_back(std::move(rp));
            }
        }
    }
    LOG_INFO("F-PDF flattened to {} paragraph(s)", paras.size());

    // ── Page setup ───────────────────────────────────────────────────────────
    double page_w = 0, page_h = 0;
    page_dimensions(fmt.page, page_w, page_h);
    LOG_DEBUG("F-PDF page size {}×{} pt", page_w, page_h);

    Cairo::RefPtr<Cairo::PdfSurface> surface;
    try {
        surface = Cairo::PdfSurface::create(out_path, page_w, page_h);
    } catch (const std::exception& e) {
        LOG_ERROR("F-PDF surface create failed: {}", e.what());
        return std::string("Could not create PDF: ") + e.what();
    }
    auto cr = Cairo::Context::create(surface);

    int    page_number = 1;
    double left, right, top, bottom;
    page_margins(fmt, page_number, left, right, top, bottom);

    auto begin_page_furniture = [&]() {
        page_margins(fmt, page_number, left, right, top, bottom);
        draw_furniture(cr, fmt, info, page_number, current_chapter,
                       page_w, left, right, top, bottom, page_h);
    };

    // ── Optional title page ──────────────────────────────────────────────────
    if (fmt.furniture.title_page) {
        auto tl = make_layout(cr);
        tl->set_font_description(Pango::FontDescription("Serif 24"));
        tl->set_width(static_cast<int>(PANGO_SCALE * (page_w - left - right)));
        tl->set_alignment(Pango::Alignment::CENTER);
        tl->set_text(info.title.empty() ? "Untitled" : info.title);
        cr->set_source_rgb(0, 0, 0);
        cr->move_to(left, page_h * 0.38);
        tl->show_in_cairo_context(cr);

        if (!info.author.empty()) {
            auto al = make_layout(cr);
            al->set_font_description(Pango::FontDescription("Serif 13"));
            al->set_width(static_cast<int>(PANGO_SCALE * (page_w - left - right)));
            al->set_alignment(Pango::Alignment::CENTER);
            al->set_text(info.author);
            cr->move_to(left, page_h * 0.38 + 48.0);
            al->show_in_cairo_context(cr);
        }
        cr->show_page();
        ++page_number;
    }

    begin_page_furniture();
    double y = top;
    const double bottom_limit = page_h - bottom;

    auto new_page = [&]() {
        cr->show_page();
        ++page_number;
        begin_page_furniture();
        y = top;
    };

    // ── Lay out paragraphs ───────────────────────────────────────────────────
    for (auto& rp : paras) {
        if (rp.is_chapter_head) current_chapter = rp.chapter_title;

        TextFormat pf = resolve_paragraph(fmt, rp.tags);

        // Forced break before a chapter head (except at the very top of a page).
        if (rp.page_break_before && y > top + 0.5)
            new_page();

        double indent_l = pf.indent_left_pt;
        double indent_r = pf.indent_right_pt;
        double text_w   = page_w - left - right - indent_l - indent_r;
        if (text_w < 36.0) text_w = 36.0;   // defensive floor

        auto layout = make_layout(cr);
        layout->set_font_description(Pango::FontDescription(font_desc_string(pf)));
        layout->set_width(static_cast<int>(PANGO_SCALE * text_w));
        layout->set_wrap(Pango::WrapMode::WORD_CHAR);
        layout->set_alignment(to_pango_align(pf.align));
        if (pf.align == Align::Justify) layout->set_justify(true);
        if (pf.line_spacing > 0.0)
            layout->set_line_spacing(static_cast<float>(pf.line_spacing));
        if (pf.first_line_pt != 0.0)
            layout->set_indent(static_cast<int>(PANGO_SCALE * pf.first_line_pt));
        layout->set_markup(build_markup(fmt, pf, rp.runs));

        double space_above = pf.space_above_pt;
        double space_below = pf.space_below_pt;
        double h = layout_height_pt(layout);

        // Paragraph-level pagination: move the whole paragraph down if it doesn't
        // fit (unless it's already at the top of a fresh page).
        if (y + space_above + h > bottom_limit && y > top + 0.5)
            new_page();

        y += space_above;
        cr->set_source_rgb(0, 0, 0);
        cr->move_to(left + indent_l, y);
        layout->show_in_cairo_context(cr);
        y += h + space_below;
    }

    cr->show_page();
    surface->finish();

    LOG_INFO("F-PDF export done: {} page(s) written to \"{}\"", page_number, out_path);
    return "";
}

}  // namespace Folio
