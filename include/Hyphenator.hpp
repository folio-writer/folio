#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Hyphenator.hpp — libhyphen pump (F-PDF auto-hyphenation, s16)
//
// Ported from Curvz's hyphenation pump. Finds legal in-word break points via
// libhyphen (Liang/TeX patterns, the hyph_<lang>.dic dictionaries shared with
// LibreOffice/Pango). The F-PDF paginator injects U+00AD soft hyphens at the
// returned offsets and lets Pango break/draw them.
//
// Gated by FOLIO_HAVE_HYPHEN (set by CMake when libhyphen is found). Without the
// library every entry point still compiles and hyphenate() simply returns no
// break points, so callers degrade to plain wrapping — never a crash.
// ─────────────────────────────────────────────────────────────────────────────

#include <string>
#include <vector>

namespace Folio {

// Optional override directory for hyph_<lang>.dic; consulted before the system
// and bundled locations. No-op when libhyphen is absent.
void set_hyphen_dict_dir(const std::string& dir);

// Return byte offsets into `word` where a soft hyphen may legally be inserted
// (ascending). Empty when libhyphen is unavailable, no dictionary exists for
// `lang`, or the word is too short to break. `lang` is a dictionary tag such
// as "en_US" (→ hyph_en_US.dic).
std::vector<size_t> hyphenate(const std::string& word, const std::string& lang);

}  // namespace Folio
