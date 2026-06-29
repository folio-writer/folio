#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ImageImport.hpp — the image import ENGINE (Gallery, build-arc step 3 pipeline).
//
// DESIGN_gallery.md §4a/§5: every import door funnels into ONE pipeline. The
// PURE half (guard / downscale / format-choice / plan) is ImageNormalize; this
// is the SIDE-EFFECT half that EXECUTES the plan with gdk-pixbuf and writes the
// bytes into the live bundle. It owns no policy — it asks ImageNormalize for the
// plan and carries it out:
//
//   probe → guard_import → decode (animation-aware) → plan_normalize →
//   scale+encode asset → scale+encode thumb → write assets/<iid>.<ext> +
//   thumbs/<iid>.<ext> → stamp + pool.add(fragment).
//
// This is the slice that also closes the s44 "copy image into the bundle" gap:
// once a Gallery/object image is a normalized fragment in assets/, no Folio
// image depends on a file outside the project.
//
// HEIC / unknown formats: graceful-fail (Scott's call) — a clear, non-silent
// error result, the import aborts cleanly, the app never crashes and never
// hard-requires the HEIF loader.
//
// The header is deliberately GTK-free (only <filesystem>, std, the pure
// NormalizePolicy, and a forward-declared FolioPrefs), so callers need not pull
// in gdkmm; all pixbuf work lives in ImageImport.cpp. The file door (import_file)
// and the bytes door (import_bytes — paste / image-drop) share one private
// post-decode pipeline (finish_from_pixbuf); only the guard + decode front differ.
// ─────────────────────────────────────────────────────────────────────────────

#include <filesystem>
#include <string>

#include "ImageNormalize.hpp"   // NormalizePolicy (pure)

namespace Folio {

namespace fs = std::filesystem;

class ImagePool;     // the engine adds the finished fragment here
class FolioPrefs;    // policy source (forward-declared; .cpp includes the header)

// Map the six §6 gallery prefs into the pure policy the pipeline reads.
NormalizePolicy normalize_policy_from_prefs(const FolioPrefs& prefs);

// Outcome of one import. `ok` true → `iid` is the new fragment's id; false →
// `error` is a clear, user-facing message (never silent — §5).
struct ImageImportResult {
  bool ok = false;
  std::string iid;
  std::string error;
  bool low_res = false;   // §13b — source below the chosen detail tier (a cue, not a failure)
};

class ImageImporter {
public:
  // `bundle_root` is the live .folio bundle dir (where assets/ + thumbs/ are
  // written). `policy` is normalize_policy_from_prefs(prefs).
  ImageImporter(fs::path bundle_root, NormalizePolicy policy);

  // Door 1 (file picker / DnD-file): decode `path`, normalize at the chosen
  // detail `tier` (§13; defaults to the deepest = today's behaviour), write
  // asset+thumb into the bundle, and add the fragment to `pool`. Returns the new
  // iid or a clear error; sets `low_res` when the source couldn't fill the tier.
  // Does NOT save the project — the caller marks the model modified.
  ImageImportResult import_file(const std::string& path, ImagePool& pool,
                           int tier = kMaxDetailTier);

  // Door 2 (paste / image-drop): import an image from already-encoded bytes — a
  // clipboard payload or a dropped texture the caller has serialized (e.g.
  // Gdk::Texture::save_to_png_bytes). Decodes internally, guards on the decoded
  // dimensions + an estimated in-memory size, then runs the SAME normalize/write/
  // pool path as import_file. `caption` seeds the fragment caption (an embedded
  // pixbuf text field wins if present); empty → Untitled. The signature is
  // deliberately byte-based, not pixbuf-based, so this header stays GTK-free.
  // Does NOT save the project — the caller marks the model modified.
  ImageImportResult import_bytes(const std::string& data, ImagePool& pool,
                            const std::string& caption = {},
                            int tier = kMaxDetailTier);

private:
  fs::path        m_root;
  NormalizePolicy m_policy;
};

}  // namespace Folio
