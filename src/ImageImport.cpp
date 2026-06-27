// ─────────────────────────────────────────────────────────────────────────────
// ImageImport.cpp — the gdk-pixbuf import pipeline (see ImageImport.hpp).
// Executes the pure NormalizePlan; the only place pixels are touched.
// ─────────────────────────────────────────────────────────────────────────────

#include "ImageImport.hpp"

#include <fstream>
#include <system_error>

#include <gdkmm/pixbuf.h>
#include <gdkmm/pixbufanimation.h>
#include <gdkmm/pixbufloader.h>
#include <glibmm/error.h>

#include "FolioPrefs.hpp"
#include "Iid.hpp"
#include "ImagePool.hpp"
#include "ProjectBundle.hpp"   // asset_path / thumb_path / content_hash / bundle_dir

namespace Folio {

NormalizePolicy normalize_policy_from_prefs(const FolioPrefs& prefs) {
  NormalizePolicy p;
  p.image_max_dim   = prefs.gallery_image_max_dim;
  p.base_long_edge  = prefs.gallery_base_long_edge;
  p.image_quality   = prefs.gallery_image_quality;
  p.thumb_max_dim   = prefs.gallery_thumb_max_dim;
  p.import_max_mb   = prefs.gallery_import_max_mb;
  p.prefer_lossless = prefs.gallery_prefer_lossless;
  return p;
}

namespace {

// Encode a pixbuf to JPEG (with quality) or PNG, into a byte string.
bool encode_pixbuf(const Glib::RefPtr<Gdk::Pixbuf>& pb, OutFormat fmt,
                   int quality, std::string& out) {
  if (!pb) return false;
  gchar*  buf = nullptr;
  gsize   len = 0;
  GError* err = nullptr;
  gboolean ok;
  if (fmt == OutFormat::Jpeg) {
    std::string q = std::to_string(quality);
    ok = gdk_pixbuf_save_to_buffer(pb->gobj(), &buf, &len, "jpeg", &err,
                                   "quality", q.c_str(), nullptr);
  } else {
    ok = gdk_pixbuf_save_to_buffer(pb->gobj(), &buf, &len, "png", &err, nullptr);
  }
  if (!ok || err) {
    if (err) g_error_free(err);
    if (buf) g_free(buf);
    return false;
  }
  out.assign(buf, len);
  g_free(buf);
  return true;
}

// Scale a pixbuf to (w,h) unless it is already that size.
Glib::RefPtr<Gdk::Pixbuf> scale_to(const Glib::RefPtr<Gdk::Pixbuf>& pb,
                                   const Dim& d) {
  if (!pb) return pb;
  if (pb->get_width() == d.w && pb->get_height() == d.h) return pb;
  return pb->scale_simple(d.w, d.h, Gdk::InterpType::BILINEAR);
}

// Binary write via tmp + rename, so a half-written asset never lands in place.
bool write_bytes_atomic(const fs::path& target, const std::string& data) {
  std::error_code ec;
  fs::create_directories(target.parent_path(), ec);
  fs::path tmp = target;
  tmp += ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!f) return false;
  }
  fs::rename(tmp, target, ec);
  if (ec) { fs::remove(tmp, ec); return false; }
  return true;
}

std::string base_name(const std::string& path) {
  return fs::path(path).filename().string();
}

// The shared post-decode pipeline both import doors run: plan from the DECODED
// image (real dims + alpha), scale asset + thumb, encode by content-format, write
// the pair atomically into the bundle, record the fragment in the pool. Assumes
// the source already passed guard_import (same contract as plan_normalize). The
// caption prefers an embedded pixbuf text field, else `caption_fallback` (a
// filename-derived name for the file door, the caller's caption for the bytes
// door). `src_bytes` rides into the plan only as provenance; sizing is by pixels.
ImportResult finish_from_pixbuf(const fs::path& root, const NormalizePolicy& policy,
                                const Glib::RefPtr<Gdk::Pixbuf>& full, bool animated,
                                long long src_bytes,
                                const std::string& caption_fallback,
                                ImagePool& pool, int tier) {
  ImportResult r;
  if (!full) { r.error = "Could not decode the image."; return r; }

  SourceProbe dprobe{src_bytes, full->get_width(), full->get_height(),
                     full->get_has_alpha(), animated};
  NormalizePlan plan = plan_normalize(dprobe, policy, tier);
  r.low_res = plan.underfilled;   // §13b — non-blocking low-res cue

  Glib::RefPtr<Gdk::Pixbuf> asset_pb = scale_to(full, plan.asset_dim);
  if (!asset_pb) { r.error = "Could not scale the image."; return r; }
  Glib::RefPtr<Gdk::Pixbuf> thumb_pb = scale_to(asset_pb, plan.thumb_dim);
  if (!thumb_pb) { r.error = "Could not build a thumbnail."; return r; }

  std::string asset_bytes, thumb_bytes;
  if (!encode_pixbuf(asset_pb, plan.format, plan.quality, asset_bytes) ||
      !encode_pixbuf(thumb_pb, plan.format, plan.quality, thumb_bytes)) {
    r.error = "Could not encode the image.";
    return r;
  }

  // Write into the live bundle; mint the iid first (it names the files).
  const std::string iid = make_iid(IidKind::Asset);
  const std::string ext = out_format_ext(plan.format);
  if (!write_bytes_atomic(asset_path(root, iid, ext), asset_bytes) ||
      !write_bytes_atomic(thumb_path(root, iid, ext), thumb_bytes)) {
    std::error_code rc;
    fs::remove(asset_path(root, iid, ext), rc);
    fs::remove(thumb_path(root, iid, ext), rc);
    r.error = "Could not write image into the project.";
    return r;
  }

  ImageFragment frag;
  frag.iid    = iid;
  frag.ext    = ext;
  frag.format = out_format_name(plan.format);
  frag.width  = asset_pb->get_width();
  frag.height = asset_pb->get_height();
  frag.bytes  = static_cast<long long>(asset_bytes.size());
  frag.hash   = content_hash(asset_bytes);
  frag.flattened_animation = plan.flatten_animation;
  // Prefer an embedded human field (best-effort — gdk-pixbuf exposes PNG text
  // chunks + a JPEG comment), harvested BEFORE the re-encode strips metadata.
  std::string embedded;
  for (const char* key : {"tEXt::Title", "tEXt::Description", "tEXt::Comment", "comment"}) {
    if (const char* v = gdk_pixbuf_get_option(full->gobj(), key)) {
      if (*v) { embedded = v; break; }
    }
  }
  frag.caption = !embedded.empty() ? embedded : caption_fallback;
  pool.add(std::move(frag));

  r.ok  = true;
  r.iid = iid;
  return r;
}

}  // namespace

ImageImporter::ImageImporter(fs::path bundle_root, NormalizePolicy policy)
    : m_root(std::move(bundle_root)), m_policy(std::move(policy)) {}

ImportResult ImageImporter::import_file(const std::string& path, ImagePool& pool,
                                        int tier) {
  ImportResult r;
  const std::string name = base_name(path);

  // The bundle must exist on disk (assets/ live inside it). A never-saved project
  // has no root to write into — ask the caller to save first.
  if (m_root.empty()) {
    r.error = "Save the project before importing images.";
    return r;
  }

  std::error_code ec;
  if (!fs::exists(path, ec)) {
    r.error = "File not found: " + name;
    return r;
  }

  // ── Guard BEFORE full decode: file size + header dimensions only ────────────
  const long long bytes = static_cast<long long>(fs::file_size(path, ec));
  int probe_w = 0, probe_h = 0;
  GdkPixbufFormat* gf = gdk_pixbuf_get_file_info(path.c_str(), &probe_w, &probe_h);
  if (!gf) {
    // Unknown / unsupported format → graceful-fail (HEIC/AVIF need a loader).
    r.error = "Could not read \"" + name +
              "\": unrecognised or unsupported image format "
              "(HEIC/AVIF may need an extra image loader installed).";
    return r;
  }
  SourceProbe pre{bytes, probe_w, probe_h, false, false};
  GuardResult g = guard_import(pre, m_policy);
  if (!g.ok) {
    r.error = g.reason;
    return r;
  }

  // ── Decode (animation-aware: first frame for GIF/WEBP, flag it) ─────────────
  Glib::RefPtr<Gdk::Pixbuf> full;
  bool animated = false;
  try {
    auto anim = Gdk::PixbufAnimation::create_from_file(path);
    animated = !anim->is_static_image();
    full = anim->get_static_image();
  } catch (const Glib::Error& e) {
    r.error = "Could not decode \"" + name + "\": " + std::string(e.what());
    return r;
  }
  if (!full) {
    r.error = "Could not decode \"" + name + "\".";
    return r;
  }

  // ── Plan + write + record (shared with the bytes door) ──────────────────────
  return finish_from_pixbuf(m_root, m_policy, full, animated, bytes,
                            caption_from_filename(path), pool, tier);
}

ImportResult ImageImporter::import_bytes(const std::string& data, ImagePool& pool,
                                         const std::string& caption, int tier) {
  ImportResult r;

  // The bundle must exist on disk (assets/ live inside it).
  if (m_root.empty()) {
    r.error = "Save the project before importing images.";
    return r;
  }
  if (data.empty()) {
    r.error = "No image data to import.";
    return r;
  }

  // ── Decode the encoded bytes (PNG / JPEG / etc.) via a pixbuf loader ────────
  // The clipboard/drag payload arrives already encoded; there is no file on disk,
  // so the cheap pre-decode header guard import_file uses does not apply. We
  // decode, then guard on the real dimensions + an estimated in-memory size.
  Glib::RefPtr<Gdk::Pixbuf> full;
  try {
    auto loader = Gdk::PixbufLoader::create();
    loader->write(reinterpret_cast<const guint8*>(data.data()), data.size());
    loader->close();
    full = loader->get_pixbuf();
  } catch (const Glib::Error& e) {
    r.error = "Could not decode the pasted image: " + std::string(e.what());
    return r;
  }
  if (!full) {
    r.error = "Could not decode the pasted image.";
    return r;
  }

  SourceProbe probe{
      estimated_decoded_bytes(full->get_width(), full->get_height(),
                              full->get_has_alpha()),
      full->get_width(), full->get_height(), full->get_has_alpha(),
      /*is_animated=*/false};
  GuardResult g = guard_import(probe, m_policy);
  if (!g.ok) { r.error = g.reason; return r; }

  return finish_from_pixbuf(m_root, m_policy, full, /*animated=*/false,
                            probe.bytes, caption, pool, tier);
}

}  // namespace Folio
