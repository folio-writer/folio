// ─────────────────────────────────────────────────────────────────────────────
// ImageNormalize.cpp — the pure import policy (see ImageNormalize.hpp).
// GTK-free, std-lib only. The gdk-pixbuf seam executes the NormalizePlan; it
// makes no decisions of its own.
// ─────────────────────────────────────────────────────────────────────────────

#include "ImageNormalize.hpp"

namespace Folio {

GuardResult guard_import(const SourceProbe& src, const NormalizePolicy& pol) {
  GuardResult r;

  // Byte ceiling first — cheapest, and the URL door enforces the same number
  // mid-stream (don't trust Content-Length, §5).
  const long long cap_bytes =
      static_cast<long long>(pol.import_max_mb) * 1024LL * 1024LL;
  if (pol.import_max_mb > 0 && src.bytes > cap_bytes) {
    r.ok = false;
    r.reason = "Image exceeds the " + std::to_string(pol.import_max_mb) +
               " MB import limit.";
    return r;
  }

  // Decode-bomb guard: pixel count, independent of byte size. width*height can
  // overflow int, so compute in 64-bit.
  const long long px =
      static_cast<long long>(src.width) * static_cast<long long>(src.height);
  if (px > kMaxImportPixels) {
    r.ok = false;
    r.reason = "Image is too large to decode safely (over " +
               std::to_string(kMaxImportMegapixels) + " megapixels).";
    return r;
  }

  return r;  // ok
}

OutFormat choose_format(bool has_alpha, const NormalizePolicy& pol) {
  // Alpha must be preserved; lossless is the writer's fidelity-over-size verdict.
  if (has_alpha || pol.prefer_lossless)
    return OutFormat::Png;
  return OutFormat::Jpeg;
}

const char* out_format_ext(OutFormat f) {
  return f == OutFormat::Png ? "png" : "jpg";
}
const char* out_format_name(OutFormat f) {
  return f == OutFormat::Png ? "png" : "jpeg";
}

Dim fit_long_edge(int w, int h, int cap) {
  Dim d{w, h};
  if (w <= 0 || h <= 0)            // degenerate; nothing to do
    return d;
  if (cap <= 0)                    // no cap requested
    return d;
  const int longest = (w >= h) ? w : h;
  if (longest <= cap)              // already within the cap — never upscale
    return d;

  // Scale by the long edge, preserve aspect, clamp to >= 1px on both axes.
  const double f = static_cast<double>(cap) / static_cast<double>(longest);
  int nw = static_cast<int>(static_cast<double>(w) * f + 0.5);
  int nh = static_cast<int>(static_cast<double>(h) * f + 0.5);
  if (nw < 1) nw = 1;
  if (nh < 1) nh = 1;
  d.w = nw;
  d.h = nh;
  return d;
}

TierResolve resolve_detail_tier(int source_long, int base, int tier, int hard_cap) {
  TierResolve r;
  if (tier < kMinDetailTier) tier = kMinDetailTier;
  if (tier > kMaxDetailTier) tier = kMaxDetailTier;
  if (base < 1) base = 1;

  const long long desired = static_cast<long long>(tier) * base;   // chosen depth
  const long long ceil    = hard_cap > 0 ? hard_cap : desired;     // ceiling pref
  const long long capped  = desired < ceil ? desired : ceil;       // depth after ceiling

  // Underfill: a known source below the capped depth would have to upscale.
  r.underfilled = source_long > 0 && static_cast<long long>(source_long) < capped;

  long long target = capped;
  if (source_long > 0 && static_cast<long long>(source_long) < target)
    target = source_long;                                          // never upscale
  if (target < 1) target = 1;
  r.target_long_edge = static_cast<int>(target);
  return r;
}

NormalizePlan plan_normalize(const SourceProbe& src, const NormalizePolicy& pol,
                             int tier) {
  NormalizePlan plan;
  plan.format = choose_format(src.has_alpha, pol);
  plan.quality = pol.image_quality;
  plan.flatten_animation = src.is_animated;

  // Asset: the long-edge target is the chosen tier's depth, clamped to source +
  // ceiling (§13). Then fit the source under it (downscale-only).
  const int src_long = (src.width >= src.height) ? src.width : src.height;
  const TierResolve tr =
      resolve_detail_tier(src_long, pol.base_long_edge, tier, pol.image_max_dim);
  plan.asset_dim = fit_long_edge(src.width, src.height, tr.target_long_edge);
  plan.underfilled = tr.underfilled;

  // Thumbnail: derived from the NORMALIZED asset dims, capped to thumb_max_dim.
  plan.thumb_dim =
      fit_long_edge(plan.asset_dim.w, plan.asset_dim.h, pol.thumb_max_dim);
  return plan;
}

namespace {
char lower_ascii(char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}
bool starts_with(const std::string& s, const std::string& p) {
  return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}
}  // namespace

std::string caption_from_filename(const std::string& path) {
  // 1. file name without directory or final extension
  std::size_t slash = path.find_last_of("/\\");
  std::string base = (slash == std::string::npos) ? path : path.substr(slash + 1);
  std::size_t dot = base.find_last_of('.');
  std::string stem = (dot == std::string::npos || dot == 0) ? base : base.substr(0, dot);

  // 2. junk guards (operate on a lowercased copy)
  std::string low;
  low.reserve(stem.size());
  int letters = 0;
  for (char c : stem) {
    low += lower_ascii(c);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) ++letters;
  }
  if (letters < 3)                       // bare timestamps, "P1010101", etc.
    return "";
  static const char* kJunkPrefix[] = {
      "img_", "img-", "img ", "dsc", "dscn", "dscf", "pxl_", "gopr", "mvimg",
      "screenshot", "screen shot", "screen_shot", "photo_", "photo-"};
  for (const char* p : kJunkPrefix)
    if (starts_with(low, p))
      return "";

  // 3. prettify: separators → space, collapse runs, trim, capitalise first letter
  std::string out;
  out.reserve(stem.size());
  bool prev_space = true;  // leading-space suppression
  for (char c : stem) {
    char ch = (c == '_' || c == '-') ? ' ' : c;
    if (ch == ' ') {
      if (!prev_space) out += ' ';
      prev_space = true;
    } else {
      out += ch;
      prev_space = false;
    }
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  if (!out.empty() && out[0] >= 'a' && out[0] <= 'z')
    out[0] = static_cast<char>(out[0] - 'a' + 'A');
  return out;
}

}  // namespace Folio
