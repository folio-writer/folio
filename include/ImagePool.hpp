#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ImagePool.hpp — the shared image POOL (Gallery data layer, build-arc step 2).
//
// DESIGN_gallery.md §1/§2/§4: "An image is a fragment in the shared pool, and the
// Gallery is a lens that reads it." This is THE ONLY layer that owns image data.
//
//   • An image-fragment owns: its own iid (ast_…), the asset on disk
//     (assets/<iid>.<ext> + thumbs/<iid>.<ext>), a caption, optional flair
//     (accent + mood — the journal/scrapbook flair vocabulary), provenance
//     (source URL), and the recorded {format,width,height,bytes,hash}.
//   • An image-fragment owns its outgoing OBJECT LINKS (the iids of the
//     characters/places it depicts) — exactly as a store Object owns its
//     relation targets. These are not a new edge STORE: StoryGraph::
//     edges_from_backlinks projects them into the one typed StoryEdge list
//     (source 3, beside prose links and object relations), so a character
//     seeing "its" images stays a computed reverse view (gallery_images_of).
//   • The Gallery node is a LENS, not a container: deleting it deletes a view,
//     not an image. So the fragments persist at the POOL (project) level, shared
//     across every lens — NOT inside any one gallery node's body. (Where the
//     pool's JSON lands in the bundle is the one open structural fork; see the
//     header note in Gallery.hpp. This class is that JSON's pure producer either
//     way, exactly as JournalLog is for a journal node's body.)
//
// Soft-delete mirrors the journal: a deleted fragment is retained (hidden from
// the working view, recoverable), never hard-dropped — and the asset bytes on
// disk are independently safe via the §9 asset-class reconcile.
//
// Pure / std-lib + nlohmann::json only (like JournalLog), so the model and its
// round-trip are proven in a sandbox before any pixbuf or bundle I/O is wired.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstddef>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Folio {

// One image in the pool. `iid` is the cross-layer handle (assets/<iid>.<ext>,
// thumbs/<iid>.<ext>, the widget name, the backlink target). The recorded fields
// are stamped by the import pipeline AFTER normalization (post-downscale dims,
// re-encoded byte size, content hash for §9 drift detection).
struct ImageFragment {
  std::string iid;          // "ast_…"; the asset's stable identity
  std::string ext;          // stored asset extension: "jpg" | "png"
  std::string caption;      // lightbox caption; URL imports default it to source_url
  std::string accent;       // flair colour token ("" = none) — shared vocabulary
  std::string mood;         // one emoji ("" = none)
  std::string source_url;   // provenance for URL imports ("" otherwise)

  // Recorded at import (post-normalize):
  std::string format;       // "jpeg" | "png" (what was written)
  int  width  = 0;          // stored asset pixel width
  int  height = 0;          // stored asset pixel height
  long long bytes = 0;      // stored asset byte size
  std::string hash;         // content_hash of the stored asset (drift detection)
  bool flattened_animation = false;  // a multi-frame source was reduced to frame 0

  bool deleted = false;     // soft-deleted (retained, hidden, recoverable)

  // Outgoing object links: iids of the characters/places this image depicts.
  // The author draws them (gallery link UI); projected into the typed edge list
  // by StoryGraph::edges_from_backlinks. Persisted with the fragment.
  std::vector<std::string> links;

  // ── Size, orientation-independent (the unification knob) ─────────────────────
  // "How big is this image" is ONE number: the LONG edge — width when landscape,
  // height when portrait. Import normalization already caps on the long edge
  // (fit_long_edge / gallery_image_max_dim), and every surface that sizes an
  // image (gallery wall/lightbox, the s44 object portrait) keys off this scalar,
  // so a landscape and a portrait at the same setting occupy the same footprint.
  int  long_edge()   const { return width >= height ? width : height; }
  int  short_edge()  const { return width >= height ? height : width; }
  bool is_landscape() const { return width >= height; }
};

class ImagePool {
public:
  ImagePool() = default;

  // ── mutation ────────────────────────────────────────────────────────────────

  // Add a fully-recorded fragment (the import seam builds it after normalize).
  // If `f.iid` is empty, a fresh ast_… is minted. Returns the stored fragment's
  // iid (minted or given). Insertion order is the pool's stable default order.
  std::string add(ImageFragment f);

  // Soft-delete by iid (retained, hidden from live_view). Unknown iid ignored.
  void soft_delete(const std::string& iid);

  // Restore a soft-deleted fragment (recovery). Unknown iid ignored.
  void restore(const std::string& iid);

  // HARD-remove a fragment record entirely (complete removal). Unlike
  // soft_delete (which retains the record + bytes), this erases the fragment so
  // the caller can also delete its on-disk asset/thumbnail and reclaim space —
  // images are expensive in a project. Returns true if a fragment was erased.
  // The asset/thumb FILES are the caller's responsibility (this layer is pure).
  bool purge(const std::string& iid);

  // Object links (the author's "this image depicts X"). Both no-op + return
  // false if the image is unknown, the object iid is empty, or (for link) it is
  // already present. Dedup is preserved. The link projects into the typed edge
  // list via StoryGraph::edges_from_backlinks.
  bool link_object(const std::string& image_iid, const std::string& object_iid);
  bool unlink_object(const std::string& image_iid, const std::string& object_iid);

  // ── access ──────────────────────────────────────────────────────────────────

  ImageFragment*       find(const std::string& iid);
  const ImageFragment* find(const std::string& iid) const;

  const std::vector<ImageFragment>& all() const { return m_fragments; }
  std::size_t size() const { return m_fragments.size(); }

  // Live fragments (not soft-deleted), in insertion order. Indices into all().
  std::vector<std::size_t> live_view() const;

  // ── serialization (the project manifest's "images" section) ─────────────────
  // The pool persists as a BARE JSON ARRAY of fragment objects — the shape
  // ProjectBundle's reconcile validates as the §9 asset class, and the shape
  // DocumentModel embeds at blob["images"]. (The Gallery is a lens, so the pool
  // lives at the PROJECT level — shared across every lens — not in any one
  // gallery node's body.) Soft-deleted fragments ARE written (retained for
  // recovery and the honest record).
  nlohmann::json to_json() const;

  // Rebuild from the manifest's array. A non-array value (absent / null / object)
  // yields an empty pool: unlike a prose journal there is no text to losslessly
  // wrap, and the asset BYTES are the real content — recovered independently by
  // the §9 asset-class reconcile (orphan assets on disk that no fragment
  // references get surfaced), so an unreadable index is recoverable without
  // guessing here.
  static ImagePool from_json(const nlohmann::json& arr);

private:
  std::vector<ImageFragment> m_fragments;  // insertion order = stable pool order
};

}  // namespace Folio
