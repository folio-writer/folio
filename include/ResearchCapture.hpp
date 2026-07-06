#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ResearchCapture.hpp — the pure (GTK-free) engine behind the Research area.
//
// Two responsibilities, both dependency-free (STL only), so the extractor half
// is real-compiled and unit-tested in the Claude sandbox:
//
//   • research_extract_meta(html)  — pull a card's Title + Summary out of a
//     captured HTML document. Title from <title>; Summary from
//     og:description / name="description", else the first non-trivial <p>.
//     Entity-decoded, whitespace-collapsed, trimmed, summary length-capped.
//     PURE — sandbox-tested against fixtures.
//
//   • research_capture(url, out_path) — shell out to `monolith` to save `url`
//     as a single self-contained HTML at `out_path`, then extract meta from it.
//     Touches the filesystem + spawns a process; NOT sandbox-runnable end to
//     end (no monolith binary here). The extractor it calls IS tested.
//
//   • monolith_available() — is the `monolith` binary on PATH? The GTK door
//     (slice 2) uses this to message "monolith not installed" instead of failing
//     opaquely.
//
// Storage note (see DESIGN_research.md): the caller writes the returned HTML to
// assets/<iid>.html (NOT content/<iid>.md) — content is eager-loaded and a
// monolith page is multi-MB. This engine only produces the bytes + meta; it does
// not know about the bundle layout.
// ─────────────────────────────────────────────────────────────────────────────

#include <string>

namespace Folio {

// Card metadata extracted from a captured HTML document. Empty strings mean
// "nothing found" (a legitimate, non-error outcome — the card field stays blank
// and the user types their own).
struct ResearchMeta {
    std::string title;    // from <title>
    std::string summary;  // og:description / description meta, else first <p>
};

// Longest summary we keep on a card before eliding, in bytes of decoded text.
// Cards are compact; a summary is a glance, not the page.
inline constexpr std::size_t kResearchSummaryCap = 280;

// PURE. Extract Title + Summary from an HTML document. Tolerant string-scan (not
// a full HTML parse — a full parser would be a dependency the pure layer avoids);
// covers the common real-world shapes and degrades to empty on anything exotic.
ResearchMeta research_extract_meta(const std::string& html);

// Is the `monolith` binary resolvable on PATH? (Probes PATH dirs directly — no
// shell.) Slice 2 gates the capture door on this.
bool monolith_available();

// Outcome of a capture attempt.
struct CaptureResult {
    bool         ok = false;
    std::string  error;     // human-readable reason on failure; "" on ok
    ResearchMeta meta;      // extracted metadata on ok
    // The caller reads out_path off disk itself (it chose the path); we don't
    // echo the bytes back through here to avoid a needless multi-MB copy.
};

// Shell out to monolith to capture `url` into `out_path` (a single
// self-contained .html), then extract meta from the written file. Returns a
// clear error if monolith is absent, the spawn fails, or the output is empty.
// `url` is shell-escaped before use. NOT sandbox-runnable (no monolith here).
CaptureResult research_capture(const std::string& url, const std::string& out_path);

} // namespace Folio
