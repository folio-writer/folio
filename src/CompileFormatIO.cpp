// ─────────────────────────────────────────────────────────────────────────────
// CompileFormatIO.cpp — pure (string ⇄ CompileFormat) serialization (s18)
// GTK-free / GLib-free. See CompileFormatIO.hpp for the scheme.
// ─────────────────────────────────────────────────────────────────────────────
#include "CompileFormatIO.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>

namespace Folio {

// ── number formatting ────────────────────────────────────────────────────────
// Locale-independent. We avoid std::to_string for doubles (locale + trailing
// zeros); "%g" gives a compact round-trippable form. The C locale is assumed at
// the file layer (GKeyFile stores raw text); parse with strtod which honours
// the C locale for the '.' decimal point.
namespace {

std::string d2s(double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.10g", v);
    return std::string(buf);
}
double s2d(const std::string& s, double fallback = 0.0) {
    if (s.empty()) return fallback;
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str()) return fallback;
    return v;
}
std::string b2s(bool v) { return v ? "true" : "false"; }
bool s2b(const std::string& s, bool fallback = false) {
    if (s == "true" || s == "1")  return true;
    if (s == "false" || s == "0") return false;
    return fallback;
}

// map lookup → "" when absent
std::string mget(const std::map<std::string, std::string>& m, const std::string& k) {
    auto it = m.find(k);
    return it == m.end() ? std::string() : it->second;
}
bool mhas(const std::map<std::string, std::string>& m, const std::string& k) {
    return m.find(k) != m.end();
}

}  // namespace

// ── enum ⇄ token ─────────────────────────────────────────────────────────────
std::string render_mode_token(RenderMode m) {
    return m == RenderMode::Formal ? "formal" : "adaptable";
}
RenderMode render_mode_from_token(const std::string& s, RenderMode fb) {
    if (s == "formal")    return RenderMode::Formal;
    if (s == "adaptable") return RenderMode::Adaptable;
    return fb;
}

std::string paper_size_token(PaperSize p) {
    switch (p) {
        case PaperSize::Letter: return "letter";
        case PaperSize::A4:     return "a4";
        case PaperSize::Legal:  return "legal";
        case PaperSize::Custom: return "custom";
    }
    return "letter";
}
PaperSize paper_size_from_token(const std::string& s, PaperSize fb) {
    if (s == "letter") return PaperSize::Letter;
    if (s == "a4")     return PaperSize::A4;
    if (s == "legal")  return PaperSize::Legal;
    if (s == "custom") return PaperSize::Custom;
    return fb;
}

std::string orientation_token(Orientation o) {
    return o == Orientation::Landscape ? "landscape" : "portrait";
}
Orientation orientation_from_token(const std::string& s, Orientation fb) {
    if (s == "portrait")  return Orientation::Portrait;
    if (s == "landscape") return Orientation::Landscape;
    return fb;
}

std::string align_token(Align a) {
    switch (a) {
        case Align::Left:    return "left";
        case Align::Center:  return "center";
        case Align::Right:   return "right";
        case Align::Justify: return "justify";
    }
    return "left";
}
Align align_from_token(const std::string& s, Align fb) {
    if (s == "left")    return Align::Left;
    if (s == "center")  return Align::Center;
    if (s == "right")   return Align::Right;
    if (s == "justify") return Align::Justify;
    return fb;
}

std::string text_case_token(TextCase c) {
    switch (c) {
        case TextCase::AsIs:  return "asis";
        case TextCase::Upper: return "upper";
        case TextCase::Lower: return "lower";
        case TextCase::Title: return "title";
    }
    return "asis";
}
TextCase text_case_from_token(const std::string& s, TextCase fb) {
    if (s == "asis")  return TextCase::AsIs;
    if (s == "upper") return TextCase::Upper;
    if (s == "lower") return TextCase::Lower;
    if (s == "title") return TextCase::Title;
    return fb;
}

// ── TextFormat ⇄ kv (prefixed) ───────────────────────────────────────────────
// Field keys under a prefix like "body." / "heading.0." / "sp.2.":
//   font size bold italic underline align ls above below il ir fl case color
namespace {

void tf_to_kv(const std::string& pfx, const TextFormat& tf,
              std::vector<FormatKV>& out) {
    const TextFormat def{};
    if (tf.font_family    != def.font_family)    out.push_back({pfx + "font",   tf.font_family});
    if (tf.font_size_pt   != def.font_size_pt)   out.push_back({pfx + "size",   d2s(tf.font_size_pt)});
    if (tf.bold           != def.bold)           out.push_back({pfx + "bold",   b2s(tf.bold)});
    if (tf.italic         != def.italic)         out.push_back({pfx + "italic", b2s(tf.italic)});
    if (tf.underline      != def.underline)      out.push_back({pfx + "underline", b2s(tf.underline)});
    if (tf.align          != def.align)          out.push_back({pfx + "align",  align_token(tf.align)});
    if (tf.line_spacing   != def.line_spacing)   out.push_back({pfx + "ls",     d2s(tf.line_spacing)});
    if (tf.space_above_pt != def.space_above_pt) out.push_back({pfx + "above",  d2s(tf.space_above_pt)});
    if (tf.space_below_pt != def.space_below_pt) out.push_back({pfx + "below",  d2s(tf.space_below_pt)});
    if (tf.indent_left_pt != def.indent_left_pt) out.push_back({pfx + "il",     d2s(tf.indent_left_pt)});
    if (tf.indent_right_pt!= def.indent_right_pt)out.push_back({pfx + "ir",     d2s(tf.indent_right_pt)});
    if (tf.first_line_pt  != def.first_line_pt)  out.push_back({pfx + "fl",     d2s(tf.first_line_pt)});
    if (tf.text_case      != def.text_case)      out.push_back({pfx + "case",   text_case_token(tf.text_case)});
    if (tf.color_hex      != def.color_hex)      out.push_back({pfx + "color",  tf.color_hex});
}

TextFormat tf_from_kv(const std::string& pfx,
                      const std::map<std::string, std::string>& m) {
    TextFormat tf;  // defaults
    if (mhas(m, pfx + "font"))      tf.font_family     = mget(m, pfx + "font");
    if (mhas(m, pfx + "size"))      tf.font_size_pt    = s2d(mget(m, pfx + "size"));
    if (mhas(m, pfx + "bold"))      tf.bold            = s2b(mget(m, pfx + "bold"));
    if (mhas(m, pfx + "italic"))    tf.italic          = s2b(mget(m, pfx + "italic"));
    if (mhas(m, pfx + "underline")) tf.underline       = s2b(mget(m, pfx + "underline"));
    if (mhas(m, pfx + "align"))     tf.align           = align_from_token(mget(m, pfx + "align"));
    if (mhas(m, pfx + "ls"))        tf.line_spacing    = s2d(mget(m, pfx + "ls"));
    if (mhas(m, pfx + "above"))     tf.space_above_pt  = s2d(mget(m, pfx + "above"));
    if (mhas(m, pfx + "below"))     tf.space_below_pt  = s2d(mget(m, pfx + "below"));
    if (mhas(m, pfx + "il"))        tf.indent_left_pt  = s2d(mget(m, pfx + "il"));
    if (mhas(m, pfx + "ir"))        tf.indent_right_pt = s2d(mget(m, pfx + "ir"));
    if (mhas(m, pfx + "fl"))        tf.first_line_pt   = s2d(mget(m, pfx + "fl"));
    if (mhas(m, pfx + "case"))      tf.text_case       = text_case_from_token(mget(m, pfx + "case"));
    if (mhas(m, pfx + "color"))     tf.color_hex       = mget(m, pfx + "color");
    return tf;
}

}  // namespace

// ── CompileFormat ⇄ kv ───────────────────────────────────────────────────────
std::vector<FormatKV> compile_format_to_kv(const CompileFormat& fmt) {
    std::vector<FormatKV> out;
    const CompileFormat def{};

    // Identity / top-level. `name` always written (the loader keys off it).
    out.push_back({"name", fmt.name});
    out.push_back({"builtin", b2s(fmt.builtin)});
    if (fmt.mode != def.mode) out.push_back({"mode", render_mode_token(fmt.mode)});
    if (fmt.hyphenate != def.hyphenate) out.push_back({"hyphenate", b2s(fmt.hyphenate)});
    if (fmt.page_break_before_top_heading != def.page_break_before_top_heading)
        out.push_back({"pb-top", b2s(fmt.page_break_before_top_heading)});

    // Page
    const PageSpec& p = fmt.page; const PageSpec& dp = def.page;
    if (p.size        != dp.size)        out.push_back({"page.size",   paper_size_token(p.size)});
    if (p.orientation != dp.orientation) out.push_back({"page.orient", orientation_token(p.orientation)});
    if (p.custom_w_pt != dp.custom_w_pt) out.push_back({"page.cw", d2s(p.custom_w_pt)});
    if (p.custom_h_pt != dp.custom_h_pt) out.push_back({"page.ch", d2s(p.custom_h_pt)});
    if (p.margin_inner_pt  != dp.margin_inner_pt)  out.push_back({"page.mi", d2s(p.margin_inner_pt)});
    if (p.margin_outer_pt  != dp.margin_outer_pt)  out.push_back({"page.mo", d2s(p.margin_outer_pt)});
    if (p.margin_top_pt    != dp.margin_top_pt)    out.push_back({"page.mt", d2s(p.margin_top_pt)});
    if (p.margin_bottom_pt != dp.margin_bottom_pt) out.push_back({"page.mb", d2s(p.margin_bottom_pt)});
    if (p.mirror_margins   != dp.mirror_margins)   out.push_back({"page.mirror", b2s(p.mirror_margins)});

    // Furniture
    const Furniture& fu = fmt.furniture; const Furniture& dfu = def.furniture;
    if (fu.header_enabled != dfu.header_enabled) out.push_back({"fur.hdr", b2s(fu.header_enabled)});
    if (fu.header.left   != dfu.header.left)   out.push_back({"fur.hdr.l", fu.header.left});
    if (fu.header.center != dfu.header.center) out.push_back({"fur.hdr.c", fu.header.center});
    if (fu.header.right  != dfu.header.right)  out.push_back({"fur.hdr.r", fu.header.right});
    if (fu.footer_enabled != dfu.footer_enabled) out.push_back({"fur.ftr", b2s(fu.footer_enabled)});
    if (fu.footer.left   != dfu.footer.left)   out.push_back({"fur.ftr.l", fu.footer.left});
    if (fu.footer.center != dfu.footer.center) out.push_back({"fur.ftr.c", fu.footer.center});
    if (fu.footer.right  != dfu.footer.right)  out.push_back({"fur.ftr.r", fu.footer.right});
    if (fu.restart_numbers_per_section != dfu.restart_numbers_per_section)
        out.push_back({"fur.restart", b2s(fu.restart_numbers_per_section)});
    if (fu.title_page != dfu.title_page) out.push_back({"fur.title", b2s(fu.title_page)});

    // Element map
    tf_to_kv("body.", fmt.elements.body, out);
    for (int i = 0; i < OUTLINE_LEVELS; ++i)
        tf_to_kv("heading." + std::to_string(i) + ".", fmt.elements.heading[i], out);
    for (int i = 0; i < static_cast<int>(ScreenplayElement::COUNT); ++i)
        tf_to_kv("sp." + std::to_string(i) + ".", fmt.elements.screenplay[i], out);

    return out;
}

CompileFormat compile_format_from_kv(const std::map<std::string, std::string>& m) {
    CompileFormat fmt;  // defaults

    if (mhas(m, "name"))      fmt.name = mget(m, "name");
    if (mhas(m, "builtin"))   fmt.builtin = s2b(mget(m, "builtin"));
    if (mhas(m, "mode"))      fmt.mode = render_mode_from_token(mget(m, "mode"));
    if (mhas(m, "hyphenate")) fmt.hyphenate = s2b(mget(m, "hyphenate"));
    if (mhas(m, "pb-top"))    fmt.page_break_before_top_heading = s2b(mget(m, "pb-top"));

    PageSpec& p = fmt.page;
    if (mhas(m, "page.size"))   p.size        = paper_size_from_token(mget(m, "page.size"));
    if (mhas(m, "page.orient")) p.orientation = orientation_from_token(mget(m, "page.orient"));
    if (mhas(m, "page.cw"))     p.custom_w_pt = s2d(mget(m, "page.cw"));
    if (mhas(m, "page.ch"))     p.custom_h_pt = s2d(mget(m, "page.ch"));
    if (mhas(m, "page.mi"))     p.margin_inner_pt  = s2d(mget(m, "page.mi"));
    if (mhas(m, "page.mo"))     p.margin_outer_pt  = s2d(mget(m, "page.mo"));
    if (mhas(m, "page.mt"))     p.margin_top_pt    = s2d(mget(m, "page.mt"));
    if (mhas(m, "page.mb"))     p.margin_bottom_pt = s2d(mget(m, "page.mb"));
    if (mhas(m, "page.mirror")) p.mirror_margins   = s2b(mget(m, "page.mirror"));

    Furniture& fu = fmt.furniture;
    if (mhas(m, "fur.hdr"))     fu.header_enabled = s2b(mget(m, "fur.hdr"));
    if (mhas(m, "fur.hdr.l"))   fu.header.left   = mget(m, "fur.hdr.l");
    if (mhas(m, "fur.hdr.c"))   fu.header.center = mget(m, "fur.hdr.c");
    if (mhas(m, "fur.hdr.r"))   fu.header.right  = mget(m, "fur.hdr.r");
    if (mhas(m, "fur.ftr"))     fu.footer_enabled = s2b(mget(m, "fur.ftr"));
    if (mhas(m, "fur.ftr.l"))   fu.footer.left   = mget(m, "fur.ftr.l");
    if (mhas(m, "fur.ftr.c"))   fu.footer.center = mget(m, "fur.ftr.c");
    if (mhas(m, "fur.ftr.r"))   fu.footer.right  = mget(m, "fur.ftr.r");
    if (mhas(m, "fur.restart")) fu.restart_numbers_per_section = s2b(mget(m, "fur.restart"));
    if (mhas(m, "fur.title"))   fu.title_page = s2b(mget(m, "fur.title"));

    fmt.elements.body = tf_from_kv("body.", m);
    for (int i = 0; i < OUTLINE_LEVELS; ++i)
        fmt.elements.heading[i] = tf_from_kv("heading." + std::to_string(i) + ".", m);
    for (int i = 0; i < static_cast<int>(ScreenplayElement::COUNT); ++i)
        fmt.elements.screenplay[i] = tf_from_kv("sp." + std::to_string(i) + ".", m);

    return fmt;
}

}  // namespace Folio
