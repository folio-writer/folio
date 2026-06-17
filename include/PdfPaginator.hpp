#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// PdfPaginator.hpp — F-PDF render/layout layer (s16, step 3)
//
// The Cairo/Pango layout layer over the verified GTK-free core:
//   parse_html  (PdfExporter)  → paragraphs + runs
//   resolve_*   (PdfExporter)  → effective format per paragraph/run  (truth table B)
//   [this]                     → Pango layout → paginate → Cairo PDF surface
//
// This is the one part of F-PDF that needs gtkmm/Cairo/Pango, so it lives in its
// own TU and is NOT unit-testable in the sandbox — verification is Scott's build
// + eyes on the output PDF (convergent evidence). Instrumented with FolioLog.
//
// Step-3 scope (this drop): Adaptable (novel) + Formal (manuscript/screenplay)
// laid out to a PDF at a caller-supplied path. Pagination is paragraph-level for
// this first cut (a paragraph taller than the text box would overflow — fine for
// real prose; line-level splitting is the next refinement). Named-style deref
// (data-folio-style → TextStyle) and a prefs-sourced base font are TODOs flagged
// inline; neither affects the current novel test content.
// ─────────────────────────────────────────────────────────────────────────────

#include "CompileFormat.hpp"
#include "Exporter.hpp"   // Exporter::SourceNode

#include <string>
#include <vector>

namespace Folio {

// Document-level info for furniture token substitution ({title}, {author}).
struct PdfDocInfo {
    std::string title;
    std::string author;
};

// Render the selected nodes to a PDF at out_path using fmt. Returns an empty
// string on success, or a human-readable error message on failure.
std::string export_pdf(const std::vector<Exporter::SourceNode>& nodes,
                       const CompileFormat&                      fmt,
                       const PdfDocInfo&                         info,
                       const std::string&                        out_path);

}  // namespace Folio
