#pragma once
//
// folioedit :: Anchor -- the per-scene re-anchor ladder (DESIGN_editorialization
// §7 / §16.6).
//
// A returned annotation carries BOTH numeric offsets (range_start/range_end,
// character offsets within its scene) AND the exact `quote` it spanned. Between
// export and return the manuscript may have DRIFTED (the author kept writing), so
// the offsets can be stale. Import re-anchors each annotation against the CURRENT
// scene text by a three-rung ladder, and NEVER silently lands on the wrong words:
//
//   1. Offset  -- the offsets still point exactly at the quote -> trust them.
//   2. Quote   -- else find the quote as text in the current scene:
//                   found once     -> re-anchor there.
//                   found several  -> re-anchor to the occurrence NEAREST the
//                                     original offset, flagged `ambiguous` so the
//                                     report can surface it.
//   3. Floating -- else (quote absent or not found) -> a scene-level floating note
//                  (range 0..0) for the author to place. Never a wrong guess.
//
// Offsets are UNICODE CODEPOINT offsets -- the same unit GtkTextBuffer counts, and
// what Folio's Annotation.range_* already mean ("UTF-8 character offsets in the
// node's HTML buffer"; §14 offset-unit fork resolved to codepoints). The engine
// text is that same HTML string, so anchoring is consistent with in-app
// annotations.
//
// PURE STL -- no crypto, no json, no DocumentModel. Operates on a plain string +
// primitives, so it's sandbox-testable end to end and the import glue (Folio-side)
// maps a folioedit::Annotation onto it. (s102 step-3.)
//
#include <string>

namespace folioedit {

enum class AnchorMethod {
    Offset,    // rung 1 -- offsets verified against the quote
    Quote,     // rung 2 -- re-anchored by matching the quote text
    Floating,  // rung 3 -- unplaceable; a scene-level note
};

struct AnchorResult {
    AnchorMethod method      = AnchorMethod::Floating;
    int          range_start = 0;   // resolved codepoint offsets (0..0 when Floating)
    int          range_end   = 0;
    bool         ambiguous   = false;  // Quote matched >1 place; nearest chosen

    bool floating() const { return method == AnchorMethod::Floating; }
};

// Number of Unicode codepoints ("characters") in a UTF-8 string. Exposed because
// the import glue needs the same count to sanity-check offsets. Continuation
// bytes (0b10xxxxxx) are not counted; malformed bytes degrade gracefully.
int utf8_length(const std::string& s);

// Re-anchor one annotation against the current scene text.
//   current_text   -- the scene's live text now (HTML; same buffer offsets)
//   range_start/end -- the annotation's offsets as sent (codepoints)
//   quote          -- the exact spanned text as sent ("" if the tool captured none)
//   version_matches -- true if the body is known un-drifted (issued body_hash still
//                      matches). Only used to trust offsets when NO quote is
//                      carried; when a quote exists the offsets are verified
//                      against it regardless, which is strictly stronger.
// Returns the rung reached and the resolved offsets. Deterministic, pure.
AnchorResult reanchor(const std::string& current_text,
                      int range_start, int range_end,
                      const std::string& quote,
                      bool version_matches = false);

}  // namespace folioedit
