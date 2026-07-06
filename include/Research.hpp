#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Research.hpp — front-door sentinel for the Research area (s113).
//
// A Research capture is a Reference leaf whose reserved form id routes it to the
// owned ResearchCard surface (Title / editable Summary / source / open-in-
// browser) instead of the ObjectForm — exactly as kGalleryTemplateId /
// kJournalTemplateId / kMindMapTemplateId do for their surfaces.
//
// The captured page lives at assets/<iid>.html (NOT the content blob — content
// is eager-loaded and a monolith page is multi-MB; see DESIGN_research.md). The
// node carries: title = page title, synopsis = summary (editable), url = source.
// ─────────────────────────────────────────────────────────────────────────────

namespace Folio {

inline constexpr const char* kResearchTemplateId = "research";

// The asset extension the capture is stored under (assets/<iid>.html).
inline constexpr const char* kResearchAssetExt = "html";

} // namespace Folio
