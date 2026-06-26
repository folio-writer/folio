#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ImageNormalize.hpp — the import normalization POLICY (Gallery asset layer).
//
// DESIGN_gallery.md §4a / §5 / §6, build-arc step 1 ("asset layer — pure: the
// normalization policy (limits, format-choice rule, fragment record schema)").
//
// Every image, regardless of which of the four import doors it arrives through
// (DnD-file / paste-bytes / paste-or-drop-URL / file-picker), runs ONE gauntlet.
// That gauntlet has two halves:
//
//   • a PURE DECISION half — guard (reject decode-bombs), downscale math,
//     format-by-content, thumbnail sizing. No pixels touched; just arithmetic
//     and rules over (probed dimensions, byte size, alpha, prefs). THIS FILE.
//   • a SIDE-EFFECT half — the actual gdk-pixbuf decode → scale → re-encode →
//     write assets/<iid>.<ext> + thumbs/<iid>.<ext>. That is the GTK seam
//     (compile-and-confirm on Scott's box), and it merely EXECUTES the plan this
//     file produces. The seam owns no policy.
//
// Splitting it this way means the only place a judgement is made ("is this too
// big?", "JPEG or PNG?", "scale to what?") is GTK-free and proven in a sandbox
// before any pixbuf is decoded — same discipline as JournalLog before
// JournalSurface, CompileFormatIO before the dialog.
//
// Pure / dependency-free (<string>, <cstdint>) so it is g++-compilable and
// unit-checkable. No GTK, no GLib, no gdk-pixbuf.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>
#include <string>

namespace Folio {

// ── Hard safety constant (NOT a pref) ────────────────────────────────────────
// A decode-bomb guard independent of the byte ceiling: a 200 KB file can claim
// 30000×30000 px and OOM the app on full decode. ~50 MP is the design's stated
// cap (DESIGN_gallery.md §4a step 2). This is a structural safety limit, so it
// is a constant, not one of the six writer-owned prefs.
inline constexpr long long kMaxImportMegapixels = 50;          // 50 * 1e6 px
inline constexpr long long kMaxImportPixels = kMaxImportMegapixels * 1000000LL;

// ── The pref subset the policy reads ─────────────────────────────────────────
// A plain mirror of the six §6 FolioPrefs gallery fields, passed by value so the
// policy stays free of the FolioPrefs (GLib) type. The surface fills this from
// FolioPrefs at import time.
struct NormalizePolicy {
  int  image_max_dim   = 2048;   // gallery_image_max_dim  — long-edge CEILING (tier cap)
  int  base_long_edge  = 512;    // gallery_base_long_edge — the 1× "fills the box" long edge (§13)
  int  image_quality   = 85;     // gallery_image_quality  — JPEG re-encode q
  int  thumb_max_dim   = 512;    // gallery_thumb_max_dim  — thumbnail long edge
  int  import_max_mb   = 40;     // gallery_import_max_mb  — reject-before-decode
  bool prefer_lossless = false;  // gallery_prefer_lossless— true => PNG always
  // gallery_allow_url_fetch lives at the URL door, not in the pixel policy.
};

// ── What a probe of the source yields (filled by the GTK seam, fed to policy) ─
// All of this is cheaply obtainable before a full decode: byte length from the
// import door, and width/height/alpha/animation from gdk-pixbuf's size-prepared
// path (gdk_pixbuf_get_file_info / a loader's "size-prepared" signal).
struct SourceProbe {
  long long bytes  = 0;      // declared/known byte size of the source
  int  width       = 0;      // probed pixel width
  int  height      = 0;      // probed pixel height
  bool has_alpha   = false;  // an alpha channel is present
  bool is_animated = false;  // multi-frame (animated GIF/WEBP) → first frame
};

// ── Guard verdict ─────────────────────────────────────────────────────────────
struct GuardResult {
  bool ok = true;            // false => reject before decode
  std::string reason;        // human-readable ("" when ok) for the clear message
};

// Reject-before-decode. Order: byte ceiling first (cheapest, and the URL door
// also enforces it mid-stream), then the megapixel bomb guard. A clear reason is
// always set on rejection so the door can surface it (never silent — §5).
GuardResult guard_import(const SourceProbe& src, const NormalizePolicy& pol);

// ── Output format choice (by content, §4a step 4) ─────────────────────────────
// Alpha present (cutouts/diagrams) OR the writer asked for lossless => PNG.
// Otherwise JPEG (opaque photos, small). One rule, applied to asset AND thumb so
// they never disagree.
enum class OutFormat { Jpeg, Png };
OutFormat choose_format(bool has_alpha, const NormalizePolicy& pol);

// ext token ⇄ format. "jpg"/"png" are the stored asset extensions.
const char* out_format_ext(OutFormat f);   // Jpeg -> "jpg", Png -> "png"
const char* out_format_name(OutFormat f);  // Jpeg -> "jpeg", Png -> "png" (recorded)

// ── Downscale math (long-edge cap, DOWNSCALE-ONLY) ────────────────────────────
struct Dim { int w = 0; int h = 0; };

// Fit (w,h) under a long-edge cap, preserving aspect, never upscaling, never
// below 1px. cap <= 0 means "no cap" (returns the input). Used for both the
// asset (image_max_dim) and the thumbnail (thumb_max_dim).
Dim fit_long_edge(int w, int h, int cap);

// ── Detail tier — the writer's viewing depth (§13, amends §9) ────────────────
// 1 = Observe (1× base_long_edge: fills the lightbox at fit, no zoom headroom).
// 4 = View into (4× base, capped at image_max_dim): pixels to spare for zoom.
// The stored asset long edge = min(tier × base_long_edge, image_max_dim,
// source_long_edge) — clamped to the source, NEVER upscaled. The display scale
// (HiDPI) is the system's job and is never baked into the asset.
inline constexpr int kMinDetailTier = 1;
inline constexpr int kMaxDetailTier = 4;

struct TierResolve {
  int  target_long_edge = 0;   // the asset's long-edge cap for the chosen tier
  bool underfilled = false;    // source can't fill the chosen depth → low-res cue
};

// Pure: resolve the asset long-edge target for a chosen tier. `base` is the 1×
// size; `hard_cap` is image_max_dim (<= 0 = no ceiling). `tier` is clamped to
// [kMinDetailTier, kMaxDetailTier]. `underfilled` is true when the source's long
// edge is below the (ceiling-capped) chosen depth — reaching it would upscale,
// which is the import-time low-quality cue (computed, never stored — §13b). A
// non-positive source_long (unknown) never flags underfill.
TierResolve resolve_detail_tier(int source_long, int base, int tier, int hard_cap);

// ── The full plan the GTK seam executes ───────────────────────────────────────
// Produced by plan_normalize() once the guard passes. The seam decodes the
// source, scales to asset_dim, re-encodes as `format` (quality for JPEG; the
// re-encode strips EXIF/GPS as a side effect), then scales to thumb_dim and
// writes thumbs/. flatten_animation => take frame 0 (galleries are stills, §4a).
struct NormalizePlan {
  OutFormat format = OutFormat::Jpeg;
  Dim  asset_dim;            // target pixels for assets/<iid>.<ext>
  Dim  thumb_dim;            // target pixels for thumbs/<iid>.<ext>
  int  quality = 85;         // JPEG quality (ignored for PNG)
  bool flatten_animation = false; // multi-frame source → first frame only
  bool underfilled = false;  // §13b — source below the chosen tier depth (low-res cue)
};

// Build the plan from a passed-guard probe + policy and the chosen detail tier.
// Pure; no pixels touched. (Call guard_import first; this assumes the source is
// within limits.) `tier` defaults to kMaxDetailTier so existing callers and
// existing imports are bit-for-bit unchanged (4 × default base 512 = 2048 = the
// default image_max_dim ceiling) — the tier is additive (§13c).
NormalizePlan plan_normalize(const SourceProbe& src, const NormalizePolicy& pol,
                             int tier = kMaxDetailTier);

// ── Derived initial caption (from the file name) ─────────────────────────────
// A best-effort human caption from a path's file name: strip the directory and
// extension, turn '_' / '-' into spaces, collapse + trim, capitalise the first
// letter. Returns "" when the name looks auto-generated (camera/phone/screenshot
// prefixes like IMG_, DSC…, PXL_, "Screenshot…", or a name with too few letters
// such as a bare timestamp) — a junk caption is worse than none. The importer
// prefers any embedded title/description and falls back to this. Pure/testable.
std::string caption_from_filename(const std::string& path);

}  // namespace Folio
