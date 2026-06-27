#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// GallerySurface.hpp — the owned Gallery surface (s62: wall + lightbox).
//
// A gallery Reference routes here in Write mode (kGalleryTemplateId), mirroring
// the Mind Map canvas / Journal surface. It owns only its LENS-DEF (the wall
// order) in the host node body; the images live in the SHARED pool
// (DocumentModel::image_pool()), handed in via set_context. Deleting a gallery
// node deletes a view, not an image (DESIGN_gallery §1).
//
// Two artboards from the approved mock:
//   • THE WALL  — a Gtk::FlowBox of thumbnail tiles (thumbs/<iid>), drag-to-
//     reorder (DragSource/DropTarget, never arrows), Import… (file-picker door).
//   • THE LIGHTBOX — a Gtk::Overlay child over the wall (the surface never
//     leaves itself): the asset large, an editable caption, object chips
//     (gallery_objects_of → jump via the open-object callback), prev/next
//     wrapping the set, close + Esc + click-out.
//
// The DnD-file / paste / URL import doors are a SEPARATE effort ("doors 2–4");
// this piece is the surface. Object chips are plumbed but quiet until image→
// object links exist (the link UI is a later piece).
// ─────────────────────────────────────────────────────────────────────────────

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <gtkmm.h>

#include "DocumentModel.hpp"
#include "FolioPrefs.hpp"

namespace Folio {

class GallerySurface : public Gtk::Box {
public:
  GallerySurface();

  // The shared pool + bundle root + import policy live behind model/prefs; the
  // Editor wires this once at build time.
  void set_context(DocumentModel* model, FolioPrefs* prefs);

  void load(const std::string& iid, const std::string& title,
            const std::string& body);
  void clear();
  void set_title(const std::string& title);

  using PersistCallback =
      std::function<void(const std::string& iid, const std::string& body)>;
  void set_persist_callback(PersistCallback cb) { m_on_persist = std::move(cb); }

  // Chip click → open that object (character/place). Editor wires it to the same
  // navigate path map-open uses (MapOpenCallback, by iid).
  using OpenObjectCallback = std::function<void(const std::string& iid)>;
  void set_open_object_callback(OpenObjectCallback cb) { m_on_open_object = std::move(cb); }

private:
  void do_import();                       // file-picker door → ImageImporter (NEW files)
  void import_files(const std::vector<std::string>& paths);  // s79 — picker/DnD file door
  void import_bytes_blob(const std::string& data);           // s79 — paste/texture-drop door
  void paste_from_clipboard();                               // s79 — clipboard → import
  void report_import(int ok, const std::vector<std::string>& failures,
                     const std::vector<std::string>& lowres);  // s79 — consolidated notice
  void open_add_existing();               // pool-picker door → hang EXISTING fragments
  void refresh();                         // rebuild the wall + count from m_order
  void persist();                         // write the lens-def back to the body
  Gtk::Widget* make_tile(int pos);        // one wall tile for m_order[pos]
  void reorder(int from_pos, int to_pos); // drag-to-reorder → m_order + persist

  void open_lightbox(int pos);            // present m_order[pos]
  void close_lightbox();
  void populate_lightbox();               // fill image/caption/chips for m_lb_pos
  void apply_zoom();                      // size the lightbox image to m_lb_zoom
  void confirm_and_delete(const std::string& iid);  // confirm → complete removal
  void delete_image(const std::string& iid);        // purge fragment + delete files
  void rebuild_link_row();                           // pills + ＋ Link for m_lb_pos
  void open_link_picker(const std::string& image_iid, Gtk::Widget& anchor);

  // ── §11 — the gallery itself points at objects (mirrors image→object, one
  // level up). The links live in THIS node's body (m_links), not on a fragment;
  // the chips sit on a sub-row under the header, the ＋ Link picker is the same
  // object picker the lightbox uses. ───────────────────────────────────────────
  void rebuild_assoc_row();                          // object chips + ＋ Link for the gallery
  void open_gallery_link_picker(Gtk::Widget& anchor);

  DocumentModel* m_model = nullptr;
  FolioPrefs*    m_prefs = nullptr;
  std::string    m_iid;
  std::vector<std::string> m_order;       // the lens-def: ordered, live fragment iids
  std::vector<std::string> m_links;       // the gallery's own object links (§11)
  bool m_loading = false;
  int  m_drag_src = -1;                    // pos being dragged (-1 = none)
  int  m_lb_pos   = -1;                    // pos shown in the lightbox (-1 = closed)
  PersistCallback    m_on_persist;
  OpenObjectCallback m_on_open_object;

  // ── header ──
  Gtk::Box    m_header{Gtk::Orientation::HORIZONTAL, 8};
  Gtk::Label  m_title;
  Gtk::Label  m_count;
  Gtk::Button m_add_btn;       // "Add…" — hang an EXISTING pool image on this wall
  Gtk::Button m_import_btn;    // "Import…" — bring NEW files into the pool + wall
  Gtk::Button m_paste_btn;     // s79 — "Paste" — clipboard image / file → pool + wall

  // ── association sub-row (under the header separator): the objects this gallery
  //    is about, as removable chips, plus an always-present ＋ Link picker ──
  Gtk::Box    m_assoc_row{Gtk::Orientation::HORIZONTAL, 8};

  // ── body: overlay( stack[ wall | empty ] ) + lightbox ──
  Gtk::Overlay        m_overlay;
  Gtk::Stack          m_body;
  Gtk::ScrolledWindow m_scroller;
  Gtk::FlowBox        m_wall;
  Gtk::Label          m_empty;

  // ── lightbox (built once; populated per open) ──
  Gtk::Box    m_lightbox{Gtk::Orientation::VERTICAL, 0};
  Gtk::Box    m_lb_content{Gtk::Orientation::VERTICAL, 12};
  Gtk::ScrolledWindow m_lb_scroll;   // wraps the image so a zoomed view can pan
  Gtk::Picture m_lb_pic;
  Gtk::Label  m_lb_zoom_lbl;
  Gtk::Entry  m_lb_caption;
  Gtk::Box    m_lb_chips{Gtk::Orientation::HORIZONTAL, 8};
  double m_lb_zoom = 0.0;   // 0 = fit-to-window; >0 = explicit factor of natural size
  int    m_lb_nat_w = 0;    // the shown asset's natural pixel size (for zoom math)
  int    m_lb_nat_h = 0;
};

}  // namespace Folio
