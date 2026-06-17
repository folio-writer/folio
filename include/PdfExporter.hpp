#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// PdfExporter.hpp — F-PDF: the compile/export-to-PDF module (s16)
//
// This is the first real consumer of the two s15 foundation headers
// (CompileFormat.hpp, ExportScan.hpp). It wires them into the Folio build.
//
// s16 phasing (see HANDOFF.md "Suggested s16 build order"):
//   1. [THIS STEP] Wire the foundations in; build the thin
//      Exporter::SourceNode → ExportScan adapter (pure logic), plus a
//      pre-render coverage preflight that the export flow can gate on.
//   2. Run the scan against a real project; use the report to pick the first
//      tags to render.
//   3. Build the paginator (RenderMode::Adaptable first), Pango layout → Cairo
//      PDF surface.  [declared below, implemented after the design check-in]
//   4. Then RenderMode::Formal (screenplay / manuscript).
//
// Deliberately GTK-free at this layer: the scan + preflight operate purely on
// Exporter::SourceNode (itself GTK-free) and the CompileFormat model, so this
// TU compiles and is unit-checkable without gtkmm. The paginator (step 3) will
// need Cairo/Pango and is split into its own section below so the pure-logic
// preflight stays independently testable — the exact "thin test seam around
// pure-logic units" the testing-gap note in HANDOFF.md calls for.
// ─────────────────────────────────────────────────────────────────────────────

#include "CompileFormat.hpp"
#include "ExportScan.hpp"
#include "Exporter.hpp"   // Exporter::SourceNode (no GTK dependency)

#include <string>
#include <vector>

namespace Folio {

// ─── Scan adapter ────────────────────────────────────────────────────────────
// The "thin adapter over Exporter::SourceNode.content" the design calls for:
// pulls each node's serialized HTML and runs the pure-logic coverage scan.
ScanReport scan_source_nodes(const std::vector<Exporter::SourceNode>& nodes);

// ─── Coverage preflight ──────────────────────────────────────────────────────
// Result of gating an export against the renderer's known tag inventory.
// `unsupported` lists element names the renderer can't yet handle (the scan's
// unknown-element set); when it's empty the document is fully within coverage.
struct PreflightResult {
    ScanReport               report;
    std::vector<std::string> unsupported;   // == report.unknown_elements
    bool ok() const { return unsupported.empty(); }
};

// Run the scan over the included nodes and decide whether the document is fully
// covered by the renderer's inventory. The caller (export flow / UI) can surface
// `report` (via format_report) and refuse / warn when !ok().
PreflightResult pdf_export_preflight(const std::vector<Exporter::SourceNode>& nodes);

// Convenience: which builtin format a chosen menu index maps to. Order matches
// builtin_compile_formats() (novel, manuscript, screenplay). Out-of-range falls
// back to the first builtin. Pure helper so the UI layer and tests agree on the
// mapping without duplicating the order.
const CompileFormat& builtin_format_at(const std::vector<CompileFormat>& fmts,
                                       std::size_t index);

// ─── Resolver (pure logic; the validated s16 mode/tier truth table) ──────────
// Turns "what the document carries" + the format + the mode into a concrete
// effective format per paragraph and per run. GTK-free so the layout layer
// (Cairo/Pango, separate TU) is a thin consumer and this stays unit-testable.
//
// Reading B (confirmed s16): Formal drops ALL document expression — inline runs
// AND paragraph-level alignment/line-height/indent — keeping only the structural
// slot selectors (data-ol / data-sp). Adaptable honors document values over the
// slot's seeded defaults. See the truth table in the s16 design check-in.

// Which ElementMap slot a paragraph resolves to. Selection is mode-independent:
// structural tags always drive it and are never dropped.
struct SlotKind {
    enum Kind { Body, Heading, Screenplay } kind = Body;
    int index = 0;   // heading level, or screenplay element index; 0 for Body
};

// Document tags parsed off a paragraph's <p> wrapper. The has_* flags gate
// whether the doc value participates (Adaptable only — ignored in Formal).
// outline/screenplay are structural (drive slot selection, both modes).
struct DocParaTags {
    bool   has_align        = false;  Align  align         = Align::Left;
    bool   has_line_spacing = false;  double line_spacing  = 0.0;
    bool   has_indent_left  = false;  double indent_left_pt  = 0.0;
    bool   has_indent_right = false;  double indent_right_pt = 0.0;
    bool   has_outline      = false;  int    outline_level   = 0;  // data-ol
    bool   has_screenplay   = false;  int    screenplay_index = 0; // data-sp
};

// Document tags on one inline run. A named style (data-folio-style) is NOT
// resolved here — the layout layer (which has prefs / the TextStyle table)
// dereferences it and fills the concrete fields below before calling
// resolve_run, keeping the resolver pure.
struct DocRunTags {
    bool        bold      = false;
    bool        italic    = false;
    bool        underline = false;
    bool        strike    = false;
    bool        has_fg    = false;  std::string fg_hex;
    bool        has_bg    = false;  std::string bg_hex;
    bool        has_font  = false;  std::string font_family;  double font_size_pt = 0.0;
};

// One run's effective inline attributes, ready to hand to Pango. Empty hex / 0
// size / empty family mean "inherit" — the renderer fills from the editor base.
struct ResolvedRun {
    bool        bold         = false;
    bool        italic       = false;
    bool        underline    = false;
    bool        strike       = false;
    std::string font_family  = "";
    double      font_size_pt = 0.0;
    std::string fg_hex       = "";   // "" = none/inherit
    std::string bg_hex       = "";   // "" = none
};

// Slot selection: data-sp wins over data-ol; neither → body.
SlotKind select_slot(const DocParaTags& p);

// Resolve a paragraph's geometry/typography TextFormat. Picks the slot, falls
// back to body when the chosen slot was left all-inherit (e.g. data-sp in a
// novel, or data-ol past the levels the format fills), inherit-fills font/size/
// spacing from body, then applies the mode rule (Adaptable: doc paragraph tags
// override; Formal: ignored).
TextFormat resolve_paragraph(const CompileFormat& fmt, const DocParaTags& p);

// Resolve one run. Base typography comes from the already-resolved paragraph
// format; in Adaptable the run's own doc tags layer on top, in Formal they're
// dropped (the run inherits the slot typography unchanged).
ResolvedRun resolve_run(const CompileFormat& fmt,
                        const TextFormat&    para_fmt,
                        const DocRunTags&    run);

// ─── HTML parser (pure logic) ────────────────────────────────────────────────
// Parses the serialized inline HTML that EditorHtmlSerializer::to_html emits
// (the closed vocabulary in that file) into paragraphs + inline runs, GTK-free
// and verifiable. The layout layer consumes this directly — the "parse the HTML
// into runs" alternative from the F-PDF design, chosen here over rehydrating a
// hidden TextBuffer so the whole pre-Cairo pipeline stays in the testable seam.
//
// Vocabulary handled (must track the serializer):
//   wrapper  <p> | <p data-sp="TYPE"> | <p data-ol="N"> (+ optional data-anchor)
//   inline   <b> <i> <u> <s>
//            <span style="color:#hex"> / "background-color:#hex"
//            <span style="font-family:'FAM';font-size:Npt">
//            <span data-folio-style="NAME">          (deref'd later, by paginator)
//            <a data-folio-link="TARGET">            (flattened to text for now)
//   para-as-span (lifted to paragraph level — the serializer emits these inline):
//            text-align:{center|right|justify}, line-height:V,
//            margin-left:Npx, margin-right:Npx     (px → pt at parse time)
//
// data-sp TYPE → ScreenplayElement index via SP_ELEMENTS order. Indents convert
// CSS px → typographic pt (× 0.75). Entities &amp; &lt; &gt; &quot; are decoded.

// One inline run as parsed (pre-resolution). named_style / link_target are raw;
// the paginator dereferences the style against prefs and decides link handling.
struct ParsedRun {
    std::string text;
    bool        bold      = false;
    bool        italic    = false;
    bool        underline = false;
    bool        strike    = false;
    bool        has_fg    = false;  std::string fg_hex;
    bool        has_bg    = false;  std::string bg_hex;
    bool        has_font  = false;  std::string font_family;  double font_size_pt = 0.0;
    std::string named_style;        // "" = none
    std::string link_target;        // "" = none
};

struct ParsedParagraph {
    DocParaTags            tags;     // structural (data-sp/ol) + expressive (align/lh/indent)
    std::string            anchor;   // data-anchor, "" = none
    std::vector<ParsedRun> runs;

    // True when the paragraph has no visible text (whitespace only).
    bool is_blank() const;
};

// Parse one node's serialized HTML into paragraphs.
std::vector<ParsedParagraph> parse_html(const std::string& html);

// Map a screenplay type string ("scene", "action", …) to its index, or -1.
int screenplay_index_of(const std::string& type);

}  // namespace Folio
