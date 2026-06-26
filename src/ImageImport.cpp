// ─────────────────────────────────────────────────────────────────────────────
// ImageImport.cpp — the gdk-pixbuf import pipeline (see ImageImport.hpp).
// Executes the pure NormalizePlan; the only place pixels are touched.
// ─────────────────────────────────────────────────────────────────────────────

#include "ImageImport.hpp"

#include <fstream>
#include <system_error>

#include <gdkmm/pixbuf.h>
#include <gdkmm/pixbufanimation.h>
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

  // ── Plan from the DECODED image (now we know real dims + alpha) ─────────────
  SourceProbe dprobe{bytes, full->get_width(), full->get_height(),
                     full->get_has_alpha(), animated};
  NormalizePlan plan = plan_normalize(dprobe, m_policy, tier);
  r.low_res = plan.underfilled;   // surfaced by the door as a non-blocking notice

  Glib::RefPtr<Gdk::Pixbuf> asset_pb = scale_to(full, plan.asset_dim);
  if (!asset_pb) { r.error = "Could not scale \"" + name + "\"."; return r; }
  Glib::RefPtr<Gdk::Pixbuf> thumb_pb = scale_to(asset_pb, plan.thumb_dim);
  if (!thumb_pb) { r.error = "Could not build a thumbnail for \"" + name + "\"."; return r; }

  std::string asset_bytes, thumb_bytes;
  if (!encode_pixbuf(asset_pb, plan.format, plan.quality, asset_bytes) ||
      !encode_pixbuf(thumb_pb, plan.format, plan.quality, thumb_bytes)) {
    r.error = "Could not encode \"" + name + "\".";
    return r;
  }

  // ── Write into the live bundle; mint the iid first (it names the files) ─────
  const std::string iid = make_iid(IidKind::Asset);
  const std::string ext = out_format_ext(plan.format);
  if (!write_bytes_atomic(asset_path(m_root, iid, ext), asset_bytes) ||
      !write_bytes_atomic(thumb_path(m_root, iid, ext), thumb_bytes)) {
    // Clean up a possible half-pair so we don't leave an orphan asset.
    std::error_code rc;
    fs::remove(asset_path(m_root, iid, ext), rc);
    fs::remove(thumb_path(m_root, iid, ext), rc);
    r.error = "Could not write image into the project.";
    return r;
  }

  // ── Record the fragment in the pool ─────────────────────────────────────────
  ImageFragment frag;
  frag.iid    = iid;
  frag.ext    = ext;
  frag.format = out_format_name(plan.format);
  frag.width  = asset_pb->get_width();
  frag.height = asset_pb->get_height();
  frag.bytes  = static_cast<long long>(asset_bytes.size());
  frag.hash   = content_hash(asset_bytes);
  frag.flattened_animation = plan.flatten_animation;
  // Initial caption: prefer an embedded human field (best-effort — gdk-pixbuf
  // exposes PNG text chunks + a JPEG comment; EXIF/IPTC/XMP would need gexiv2,
  // not a dependency here). We harvest it from the decoded pixbuf BEFORE the
  // re-encode strips all metadata (keep the description, drop the GPS). Fall back
  // to a name derived from the file.
  std::string embedded;
  for (const char* key : {"tEXt::Title", "tEXt::Description", "tEXt::Comment", "comment"}) {
    if (const char* v = gdk_pixbuf_get_option(full->gobj(), key)) {
      if (*v) { embedded = v; break; }
    }
  }
  frag.caption = !embedded.empty() ? embedded : caption_from_filename(path);
  pool.add(std::move(frag));

  r.ok  = true;
  r.iid = iid;
  return r;
}

}  // namespace Folio
