#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// CompileFormat.hpp — output/compile format model (F-PDF, s15 block-in)
//
// DESIGN BLOCK-IN — not yet wired to the renderer, the Exporter, prefs
// persistence, or any UI. This file defines the *shape* of an output format so
// we can react to the concrete fields before building the paginator.
//
// The model in one line: a format is a filled-in core (page + furniture +
// element map) plus one mode bit. The renderer walks the document's region
// model (plain-text buffer + tag ranges) and resolves each paragraph/run
// against this core.
//
//   RenderMode::Formal     — impose the format; the document's *inline* tags
//                            (bold/italic/colour/font) are dropped. Structural
//                            paragraph tags (screenplay element, outline level)
//                            still drive which element-map entry is used.
//                            → screenplay, manuscript.
//   RenderMode::Adaptable  — honor the document's inline tags on top of the
//                            element-map entry (which supplies defaults).
//                            → novel / print.
//
// EDITABLE BUT SEEDED: there are deliberately NO per-field "locked" flags. The
// formal presets are seeded with the standard, but every field stays editable
// (someone can make a 13pt screenplay). Rigidity = the seed values + the mode,
// not immutability.
//
// WIRING NOTES (to reconcile when this leaves block-in):
//   • ScreenplayElement order MUST match SP_ELEMENTS in Editor_format.cpp
//     ({scene, action, character, parenthetical, dialogue, transition}).
//   • OUTLINE_LEVELS mirrors MAX_OUTLINE_LEVELS in FolioPrefs.hpp.
//   • TextFormat overlaps TextStyle/HeadingStyle (FolioPrefs.hpp). DECISION
//     (s15): keep TextFormat as its own output-layout type and convert at the
//     boundary. TextStyle/HeadingStyle are editor-applied (screen) concepts;
//     TextFormat adds output-only fields (point indents, first-line indent,
//     space-below, casing) that don't belong on the editor structs. The overlap
//     is only the typographic primitives; conversion is a trivial field copy at
//     the point Adaptable mode seeds defaults from the doc, or a folio-style:
//     run resolves to its TextStyle.
//   • Colours are "#rrggbb" strings here to stay GTK-free (matches TextStyle);
//     the renderer converts via Folio::color (color_utils.hpp).
//   • Sizes/margins are typographic points (72 pt = 1 inch).
//   • Persistence: a CompileFormat would serialise into prefs alongside
//     text_styles (GROUP_STYLES) — not designed here.
// ─────────────────────────────────────────────────────────────────────────────

#include <array>
#include <string>
#include <vector>

namespace Folio {

// Mirrors MAX_OUTLINE_LEVELS in FolioPrefs.hpp — reconcile when wiring.
inline constexpr int OUTLINE_LEVELS = 9;

// Order MUST match SP_ELEMENTS in Editor_format.cpp.
enum class ScreenplayElement {
    Scene = 0,
    Action,
    Character,
    Parenthetical,
    Dialogue,
    Transition,
    COUNT
};

enum class RenderMode {
    Formal,     // impose format; drop document inline tags
    Adaptable   // honor document inline tags over the seeded defaults
};

enum class PaperSize { Letter, A4, Legal, Custom };
enum class Orientation { Portrait, Landscape };
enum class Align { Left, Center, Right, Justify };
enum class TextCase { AsIs, Upper, Lower, Title };

// ─── TextFormat ──────────────────────────────────────────────────────────────
// One paragraph kind's text format. A default-constructed TextFormat means
// "inherit" everywhere (empty font, 0 size, 0 spacing) — used for element-map
// slots a given format doesn't style.
struct TextFormat {
    std::string font_family       = "";          // "" = inherit base body font
    double      font_size_pt      = 0.0;          // 0  = inherit
    bool        bold              = false;
    bool        italic            = false;
    bool        underline         = false;
    Align       align             = Align::Left;
    double      line_spacing      = 0.0;          // multiple of single; 0 = inherit
    double      space_above_pt    = 0.0;
    double      space_below_pt    = 0.0;
    double      indent_left_pt    = 0.0;          // from the text block's left margin
    double      indent_right_pt   = 0.0;
    double      first_line_pt     = 0.0;          // first-line (paragraph) indent
    TextCase    text_case         = TextCase::AsIs;
    std::string color_hex         = "";           // "" = inherit
};

// ─── PageSpec ────────────────────────────────────────────────────────────────
// Margins are inner/outer/top/bottom. When mirror == false, inner = left and
// outer = right on every page (single-sided: screenplay, manuscript). When
// mirror == true, inner/outer swap on facing pages for the binding gutter
// (novel / print).
struct PageSpec {
    PaperSize   size          = PaperSize::Letter;
    Orientation orientation   = Orientation::Portrait;
    double      custom_w_pt    = 0.0;   // used only when size == Custom
    double      custom_h_pt    = 0.0;
    double      margin_inner_pt  = 72.0;
    double      margin_outer_pt  = 72.0;
    double      margin_top_pt    = 72.0;
    double      margin_bottom_pt = 72.0;
    bool        mirror_margins   = false;
};

// ─── Furniture ───────────────────────────────────────────────────────────────
// Running header/footer slots are token templates. Recognised tokens (block-in
// set): {title} {author} {page} {chapter}. Empty slot = nothing in that corner.
struct RunningHead {
    std::string left   = "";
    std::string center = "";
    std::string right  = "";
};

struct Furniture {
    bool        header_enabled  = false;
    RunningHead header;
    bool        footer_enabled  = false;
    RunningHead footer;
    bool        restart_numbers_per_section = false;
    bool        title_page       = false;
};

// ─── ElementMap ──────────────────────────────────────────────────────────────
// Text format per paragraph kind. A format fills the entries relevant to its
// document type; the rest stay default (inherit).
//   • body            — default prose paragraph.
//   • heading[level]  — prose outline headings (level 0 = top).
//   • screenplay[el]  — the six screenplay elements, indexed by ScreenplayElement.
struct ElementMap {
    TextFormat body;
    std::array<TextFormat, OUTLINE_LEVELS> heading{};
    std::array<TextFormat, static_cast<int>(ScreenplayElement::COUNT)> screenplay{};
};

// ─── CompileFormat ───────────────────────────────────────────────────────────
struct CompileFormat {
    std::string name     = "Untitled";
    bool        builtin  = false;
    RenderMode  mode     = RenderMode::Adaptable;
    PageSpec    page;
    Furniture   furniture;
    ElementMap  elements;
    bool        page_break_before_top_heading = false;
    bool        hyphenate = false;   // s16 — auto-hyphenation (Adaptable only);
                                     // seeded true for the novel preset
};

// ─── Seed presets ────────────────────────────────────────────────────────────
// Convenience indexer.
inline TextFormat& sp_entry(ElementMap& m, ScreenplayElement e) {
    return m.screenplay[static_cast<int>(e)];
}

// US screenplay. Formal. Asymmetric left margin (1.5") for binding; the six
// element formats are seeded to the common industry geometry.
// NOTE: indent figures are the widely-cited US standard, expressed in points
// relative to the left text margin — verify against a definitive reference
// before this drives real output.
inline CompileFormat preset_screenplay() {
    CompileFormat f;
    f.name    = "Screenplay (US)";
    f.builtin = true;
    f.mode    = RenderMode::Formal;

    f.page = PageSpec{};
    f.page.size            = PaperSize::Letter;
    f.page.margin_inner_pt = 108.0;  // 1.5" left (binding)
    f.page.margin_outer_pt = 72.0;   // 1.0" right
    f.page.margin_top_pt   = 72.0;
    f.page.margin_bottom_pt= 72.0;
    f.page.mirror_margins  = false;

    f.furniture.header_enabled = true;
    f.furniture.header.right   = "{page}.";   // page number top-right
    f.furniture.title_page     = true;

    // Body / action: Courier 12, full width, left.
    f.elements.body.font_family = "Courier New";
    f.elements.body.font_size_pt = 12.0;
    f.elements.body.line_spacing = 1.0;

    auto base = f.elements.body;
    sp_entry(f.elements, ScreenplayElement::Action) = base;

    TextFormat scene = base;
    scene.text_case = TextCase::Upper;
    scene.space_above_pt = 24.0;   // ~2 blank lines
    scene.space_below_pt = 12.0;
    sp_entry(f.elements, ScreenplayElement::Scene) = scene;

    TextFormat character = base;
    character.text_case = TextCase::Upper;
    character.indent_left_pt = 158.0;  // ~2.2" from margin
    character.space_above_pt = 12.0;
    sp_entry(f.elements, ScreenplayElement::Character) = character;

    TextFormat paren = base;
    paren.indent_left_pt = 108.0;      // ~1.5" from margin
    sp_entry(f.elements, ScreenplayElement::Parenthetical) = paren;

    TextFormat dialogue = base;
    dialogue.indent_left_pt  = 72.0;   // 1.0"
    dialogue.indent_right_pt = 108.0;  // 1.5"
    sp_entry(f.elements, ScreenplayElement::Dialogue) = dialogue;

    TextFormat trans = base;
    trans.text_case = TextCase::Upper;
    trans.align = Align::Right;
    trans.space_above_pt = 12.0;
    sp_entry(f.elements, ScreenplayElement::Transition) = trans;

    return f;
}

// Standard novel/prose manuscript submission. Formal. Courier 12, double-spaced,
// 1" margins, half-inch first-line indent, chapter headings centered on a new
// page. This is what the current print "normalise" path produces.
inline CompileFormat preset_manuscript() {
    CompileFormat f;
    f.name    = "Manuscript";
    f.builtin = true;
    f.mode    = RenderMode::Formal;

    f.page = PageSpec{};   // 1" all sides, Letter, single-sided (defaults)

    f.furniture.header_enabled = true;
    f.furniture.header.right   = "{author} / {title} / {page}";  // running head
    f.furniture.title_page     = true;

    f.elements.body.font_family   = "Courier New";
    f.elements.body.font_size_pt  = 12.0;
    f.elements.body.line_spacing  = 2.0;     // double
    f.elements.body.first_line_pt = 36.0;    // 0.5"

    TextFormat chapter;
    chapter.font_family   = "Courier New";
    chapter.font_size_pt  = 12.0;
    chapter.align         = Align::Center;
    chapter.text_case     = TextCase::Upper;
    chapter.space_above_pt = 144.0;          // start ~1/3 down the page
    chapter.space_below_pt = 24.0;
    f.elements.heading[0] = chapter;

    f.page_break_before_top_heading = true;
    return f;
}

// Novel for print. Adaptable — honors the document's own styling; the entries
// below are the *defaults* it lays over. 6x9 trade size, mirrored margins,
// justified serif body, running heads + folio.
inline CompileFormat preset_novel() {
    CompileFormat f;
    f.name    = "Novel (print, 6x9)";
    f.builtin = true;
    f.mode    = RenderMode::Adaptable;

    f.page = PageSpec{};
    f.page.size            = PaperSize::Custom;
    f.page.custom_w_pt     = 432.0;  // 6"
    f.page.custom_h_pt     = 648.0;  // 9"
    f.page.margin_inner_pt = 63.0;   // 0.875" gutter side
    f.page.margin_outer_pt = 45.0;   // 0.625" outer
    f.page.margin_top_pt   = 54.0;   // 0.75"
    f.page.margin_bottom_pt= 54.0;
    f.page.mirror_margins  = true;

    f.furniture.header_enabled = true;
    f.furniture.header.center  = "{title}";
    f.furniture.footer_enabled = true;
    f.furniture.footer.center  = "{page}";
    f.furniture.title_page     = true;

    // Body default (document styles override per run in Adaptable mode).
    f.elements.body.font_family   = "";       // inherit doc base serif
    f.elements.body.font_size_pt  = 11.0;
    f.elements.body.align         = Align::Justify;
    f.elements.body.line_spacing  = 1.15;
    f.elements.body.first_line_pt = 18.0;     // 0.25"

    TextFormat h0;
    h0.font_size_pt  = 18.0;
    h0.bold          = true;
    h0.align         = Align::Center;
    h0.space_above_pt = 96.0;
    h0.space_below_pt = 24.0;
    f.elements.heading[0] = h0;

    TextFormat h1;
    h1.font_size_pt  = 14.0;
    h1.bold          = true;
    h1.space_above_pt = 18.0;
    h1.space_below_pt = 6.0;
    f.elements.heading[1] = h1;

    f.page_break_before_top_heading = true;
    f.hyphenate = true;   // narrow justified measure benefits from hyphenation
    return f;
}

// Built-in set, in menu order.
inline std::vector<CompileFormat> builtin_compile_formats() {
    return { preset_novel(), preset_manuscript(), preset_screenplay() };
}

}  // namespace Folio
