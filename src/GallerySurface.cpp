// ─────────────────────────────────────────────────────────────────────────────
// GallerySurface.cpp — the owned Gallery surface (s62: wall + lightbox).
// Wall = FlowBox of thumbnail tiles (drag-to-reorder) + Import… door; lightbox =
// overlay with the asset large, editable caption, object chips, prev/next.
// Compile-and-confirm GTK seam (the pure layer it stands on is sandbox-proven).
// ─────────────────────────────────────────────────────────────────────────────

#include "GallerySurface.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <system_error>

#include <gdk/gdkkeysyms.h>

#include "Gallery.hpp"        // kGalleryTemplateId + lens codec + lens reads
#include "ImageImport.hpp"    // ImageImporter + normalize_policy_from_prefs
#include "ImagePool.hpp"
#include "ProjectBundle.hpp"  // asset_path / thumb_path
#include "StoryGraph.hpp"     // edges_from_backlinks → gallery_objects_of

namespace Folio {

namespace {
constexpr int kTileW = 200;
constexpr int kTileH = 144;

// Load an image file into a texture, scaled to fit a box if a cap is given.
// Returns an empty ref on any failure (missing file, undecodable) — callers show
// a placeholder rather than crash.
Glib::RefPtr<Gdk::Texture> load_texture(const std::string& path) {
  try {
    auto pix = Gdk::Pixbuf::create_from_file(path);
    if (!pix) return {};
    return Gdk::Texture::create_for_pixbuf(pix);
  } catch (...) {
    return {};
  }
}

// One linkable object (a character or place) for the picker.
struct ObjPick {
  std::string iid;
  std::string name;
  std::string group;  // "Characters" | "Places"
};

// Every linkable node — Characters, then Places, then Scenes. Groups are
// excluded naturally: only chr_/plc_/scn_ iids are link targets, so an
// organisational group never appears.
std::vector<ObjPick> gather_link_targets(DocumentModel& model) {
  std::vector<ObjPick> chars, places, scenes;
  for (const auto& nr : model.collect_all_nodes()) {
    if (!nr.node) continue;
    switch (iid_kind_of(nr.node->iid)) {
      case IidKind::Character:
        chars.push_back({nr.node->iid, nr.node->title, "Characters"});
        break;
      case IidKind::Place:
        places.push_back({nr.node->iid, nr.node->title, "Places"});
        break;
      case IidKind::Scene:
        scenes.push_back({nr.node->iid, nr.node->title, "Scenes"});
        break;
      default:
        break;
    }
  }
  chars.insert(chars.end(), places.begin(), places.end());
  chars.insert(chars.end(), scenes.begin(), scenes.end());
  return chars;
}

// A small round colour dot (background, so it beats the universal color rule).
Gtk::Widget* make_accent_dot(IidKind kind) {
  const char* hex = kind == IidKind::Character ? "#89b4fa"   // blue
                    : kind == IidKind::Place   ? "#a6e3a1"   // green
                    : kind == IidKind::Scene   ? "#cba6f7"   // mauve
                                               : "#6c7086";
  auto* d = Gtk::make_managed<Gtk::Box>();
  d->set_valign(Gtk::Align::CENTER);
  auto p = Gtk::CssProvider::create();
  p->load_from_data(std::string("box{background-color:") + hex +
                    ";border-radius:9999px;min-width:8px;min-height:8px;}");
  d->get_style_context()->add_provider(p, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  return d;
}
}  // namespace

GallerySurface::GallerySurface() : Gtk::Box(Gtk::Orientation::VERTICAL, 0) {
  add_css_class("gallery-surface");

  // backdrop scrim styling (background is not touched by the universal color rule)
  auto css = Gtk::CssProvider::create();
  css->load_from_data(
      ".gallery-lightbox{background-color:rgba(17,17,27,0.94);}"
      ".gallery-tile{background-color:#313244;border-radius:6px;}"
      ".gallery-cap{padding:2px 6px;}"
      ".gallery-pill{background-color:#313244;border-radius:15px;padding:1px 4px;}"
      ".gallery-pill-add{border:1px dashed #585b70;border-radius:15px;}");
  Gtk::StyleContext::add_provider_for_display(
      Gdk::Display::get_default(), css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  // ── slim header ──
  m_header.add_css_class("gallery-header");
  m_header.set_margin_start(16);
  m_header.set_margin_end(16);
  m_header.set_margin_top(10);
  m_header.set_margin_bottom(10);
  m_title.set_halign(Gtk::Align::START);
  m_title.set_xalign(0.0f);
  m_title.add_css_class("gallery-title");
  m_count.add_css_class("dim-label");
  m_import_btn.set_label("Import\xE2\x80\xA6");
  m_import_btn.add_css_class("suggested-action");
  m_import_btn.signal_clicked().connect(
      sigc::mem_fun(*this, &GallerySurface::do_import));
  m_paste_btn.set_label("Paste");
  m_paste_btn.set_tooltip_text("Paste an image (or a copied image file) from the clipboard");
  m_paste_btn.signal_clicked().connect(
      sigc::mem_fun(*this, &GallerySurface::paste_from_clipboard));
  m_add_btn.set_label("Add\xE2\x80\xA6");
  m_add_btn.set_tooltip_text("Hang an image already in the project on this wall");
  m_add_btn.signal_clicked().connect(
      sigc::mem_fun(*this, &GallerySurface::open_add_existing));
  auto* spacer = Gtk::make_managed<Gtk::Box>();
  spacer->set_hexpand(true);
  m_header.append(m_title);
  m_header.append(m_count);
  m_header.append(*spacer);
  m_header.append(m_add_btn);
  m_header.append(m_paste_btn);
  m_header.append(m_import_btn);
  append(m_header);
  append(*Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL));

  // ── association sub-row: "this wall is about …" (chips + ＋ Link), under the
  //    header separator so the title/count/buttons line stays clean (§11) ──
  m_assoc_row.add_css_class("gallery-assoc-row");
  m_assoc_row.set_margin_start(16);
  m_assoc_row.set_margin_end(16);
  m_assoc_row.set_margin_top(8);
  m_assoc_row.set_margin_bottom(8);
  m_assoc_row.set_halign(Gtk::Align::START);
  append(m_assoc_row);
  append(*Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL));

  // ── the wall ──
  m_wall.set_selection_mode(Gtk::SelectionMode::NONE);
  m_wall.set_homogeneous(true);
  m_wall.set_row_spacing(14);
  m_wall.set_column_spacing(14);
  m_wall.set_min_children_per_line(2);
  m_wall.set_max_children_per_line(8);
  m_wall.set_margin_start(16);
  m_wall.set_margin_end(16);
  m_wall.set_margin_top(14);
  m_wall.set_margin_bottom(14);
  m_wall.set_valign(Gtk::Align::START);
  m_scroller.set_child(m_wall);
  m_scroller.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  m_scroller.set_vexpand(true);

  m_empty.set_text("No images yet — Import, Paste, or drop images here.");
  m_empty.add_css_class("dim-label");
  m_empty.set_halign(Gtk::Align::CENTER);
  m_empty.set_valign(Gtk::Align::CENTER);
  m_empty.set_vexpand(true);

  m_body.add(m_scroller, "wall");
  m_body.add(m_empty, "empty");
  m_body.set_vexpand(true);
  m_overlay.set_child(m_body);

  // s79 — drop files / images onto the wall (covers the empty state too). Internal
  // tile reorder uses a G_TYPE_INT target on each tile; this matches the external
  // FILE_LIST / TEXTURE types, so the two never collide.
  {
    auto drop = Gtk::DropTarget::create(GDK_TYPE_TEXTURE, Gdk::DragAction::COPY);
    drop->set_gtypes({GDK_TYPE_TEXTURE, GDK_TYPE_FILE_LIST});
    drop->signal_drop().connect(
        [this](const Glib::ValueBase& value, double, double) -> bool {
          const GValue* gv = value.gobj();
          if (G_VALUE_HOLDS(gv, GDK_TYPE_TEXTURE)) {
            GdkTexture* t = GDK_TEXTURE(g_value_get_object(gv));
            if (!t) return false;
            auto tex = Glib::wrap(t, /*take_copy=*/true);
            auto bytes = tex->save_to_png_bytes();
            gsize n = 0;
            gconstpointer d = g_bytes_get_data(bytes->gobj(), &n);
            import_bytes_blob(std::string(static_cast<const char*>(d), n));
            return true;
          }
          if (G_VALUE_HOLDS(gv, GDK_TYPE_FILE_LIST)) {
            GdkFileList* fl = static_cast<GdkFileList*>(g_value_get_boxed(gv));
            if (!fl) return false;
            GSList* files = gdk_file_list_get_files(fl);
            std::vector<std::string> paths;
            for (GSList* l = files; l; l = l->next) {
              char* p = g_file_get_path(G_FILE(l->data));
              if (p) { paths.emplace_back(p); g_free(p); }
            }
            g_slist_free(files);
            if (paths.empty()) return false;
            import_files(paths);
            return true;
          }
          return false;
        }, false);
    m_overlay.add_controller(drop);
  }
  // Ctrl+V anywhere on the surface → paste (the Paste button is the discoverable
  // route; this serves keyboard users when the surface has focus).
  {
    auto keys = Gtk::EventControllerKey::create();
    keys->signal_key_pressed().connect(
        [this](guint keyval, guint, Gdk::ModifierType state) -> bool {
          if ((state & Gdk::ModifierType::CONTROL_MASK) ==
                  Gdk::ModifierType::CONTROL_MASK &&
              (keyval == GDK_KEY_v || keyval == GDK_KEY_V)) {
            paste_from_clipboard();
            return true;
          }
          return false;
        }, false);
    add_controller(keys);
  }

  // ── the lightbox (overlay child; hidden until a tile is clicked) ──
  m_lightbox.add_css_class("gallery-lightbox");
  m_lightbox.set_visible(false);
  m_lightbox.set_focusable(true);   // so Esc reaches its key controller
  m_lightbox.set_halign(Gtk::Align::FILL);
  m_lightbox.set_valign(Gtk::Align::FILL);

  // close button + zoom controls row
  auto* topbar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
  topbar->set_margin_top(10);
  topbar->set_margin_start(14);
  topbar->set_margin_end(14);

  auto* zoom_out = Gtk::make_managed<Gtk::Button>();
  zoom_out->set_label("\xE2\x88\x92");          // minus
  zoom_out->add_css_class("circular");
  zoom_out->set_tooltip_text("Zoom out");
  zoom_out->signal_clicked().connect([this]() {
    if (m_lb_zoom <= 0.0) return;               // already fit (the floor)
    m_lb_zoom /= 1.25;
    if (m_lb_zoom < 0.20) m_lb_zoom = 0.0;       // snap back to fit
    apply_zoom();
  });
  m_lb_zoom_lbl.set_text("Fit");
  m_lb_zoom_lbl.set_width_chars(5);
  m_lb_zoom_lbl.add_css_class("dim-label");
  auto* zoom_in = Gtk::make_managed<Gtk::Button>();
  zoom_in->set_label("+");
  zoom_in->add_css_class("circular");
  zoom_in->set_tooltip_text("Zoom in");
  zoom_in->signal_clicked().connect([this]() {
    m_lb_zoom = (m_lb_zoom <= 0.0) ? 1.0 : std::min(8.0, m_lb_zoom * 1.25);
    apply_zoom();
  });
  auto* zoom_fit = Gtk::make_managed<Gtk::Button>("Fit");
  zoom_fit->add_css_class("flat");
  zoom_fit->set_tooltip_text("Fit to window");
  zoom_fit->signal_clicked().connect([this]() { m_lb_zoom = 0.0; apply_zoom(); });

  auto* lb_trash = Gtk::make_managed<Gtk::Button>();
  lb_trash->set_icon_name("user-trash-symbolic");
  lb_trash->add_css_class("circular");
  lb_trash->set_tooltip_text("Delete this image from the project");
  lb_trash->signal_clicked().connect([this]() {
    if (m_lb_pos >= 0 && m_lb_pos < (int)m_order.size())
      confirm_and_delete(m_order[m_lb_pos]);
  });

  auto* tb_spacer = Gtk::make_managed<Gtk::Box>();
  tb_spacer->set_hexpand(true);
  auto* close_btn = Gtk::make_managed<Gtk::Button>();
  close_btn->set_label("\xE2\x9C\x95");
  close_btn->add_css_class("circular");
  close_btn->signal_clicked().connect(
      sigc::mem_fun(*this, &GallerySurface::close_lightbox));

  topbar->append(*zoom_out);
  topbar->append(m_lb_zoom_lbl);
  topbar->append(*zoom_in);
  topbar->append(*zoom_fit);
  topbar->append(*lb_trash);
  topbar->append(*tb_spacer);
  topbar->append(*close_btn);
  m_lightbox.append(*topbar);

  // center row: ‹  [image]  ›
  auto* center = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
  center->set_vexpand(true);
  center->set_margin_start(8);
  center->set_margin_end(8);
  auto* prev_btn = Gtk::make_managed<Gtk::Button>();
  prev_btn->set_label("\xE2\x80\xB9");
  prev_btn->add_css_class("circular");
  prev_btn->set_valign(Gtk::Align::CENTER);
  prev_btn->set_tooltip_text("Previous (\xE2\x86\x90)");
  prev_btn->signal_clicked().connect([this]() {
    int i = gallery_prev_index(m_order, m_lb_pos >= 0 && m_lb_pos < (int)m_order.size()
                                            ? m_order[m_lb_pos] : std::string{});
    if (i >= 0) open_lightbox(i);
  });
  auto* next_btn = Gtk::make_managed<Gtk::Button>();
  next_btn->set_label("\xE2\x80\xBA");
  next_btn->add_css_class("circular");
  next_btn->set_valign(Gtk::Align::CENTER);
  next_btn->set_tooltip_text("Next (\xE2\x86\x92)");
  next_btn->signal_clicked().connect([this]() {
    int i = gallery_next_index(m_order, m_lb_pos >= 0 && m_lb_pos < (int)m_order.size()
                                            ? m_order[m_lb_pos] : std::string{});
    if (i >= 0) open_lightbox(i);
  });
  m_lb_pic.set_hexpand(true);
  m_lb_pic.set_vexpand(true);
  m_lb_pic.set_can_shrink(true);
  m_lb_pic.set_content_fit(Gtk::ContentFit::SCALE_DOWN);  // never upscale → never fuzzy
  m_lb_scroll.set_child(m_lb_pic);
  m_lb_scroll.set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
  m_lb_scroll.set_hexpand(true);
  m_lb_scroll.set_vexpand(true);
  center->append(*prev_btn);
  center->append(m_lb_scroll);
  center->append(*next_btn);
  m_lightbox.append(*center);

  // content: caption + chips
  m_lb_content.set_halign(Gtk::Align::CENTER);
  m_lb_content.set_margin_bottom(18);
  m_lb_content.set_size_request(560, -1);
  m_lb_caption.set_placeholder_text("Add a caption\xE2\x80\xA6");
  m_lb_caption.signal_changed().connect([this]() {
    if (m_loading || !m_model || m_lb_pos < 0 || m_lb_pos >= (int)m_order.size()) return;
    if (ImageFragment* f = m_model->image_pool().find(m_order[m_lb_pos])) {
      f->caption = m_lb_caption.get_text();
      m_model->mark_modified();
    }
  });
  m_lb_chips.set_halign(Gtk::Align::CENTER);
  m_lb_content.append(m_lb_caption);
  m_lb_content.append(m_lb_chips);
  m_lightbox.append(m_lb_content);

  // Keyboard: Esc closes; ←/→ navigate; +/−/0 zoom. The caption entry keeps its
  // own keys (only Esc bubbles out of it). Click-out is a later polish — a
  // correct scrim-only hit-test must exclude the image AND the caption column.
  auto key = Gtk::EventControllerKey::create();
  key->signal_key_pressed().connect(
      [this](guint keyval, guint, Gdk::ModifierType) -> bool {
        if (!m_lightbox.get_visible()) return false;
        // While editing the caption, let it own every key but Escape.
        if (m_lb_caption.has_focus()) {
          if (keyval == GDK_KEY_Escape) { close_lightbox(); return true; }
          return false;
        }
        const std::string cur = (m_lb_pos >= 0 && m_lb_pos < (int)m_order.size())
                                    ? m_order[m_lb_pos] : std::string{};
        switch (keyval) {
          case GDK_KEY_Escape:
            close_lightbox();
            return true;
          case GDK_KEY_Left: {
            int i = gallery_prev_index(m_order, cur);
            if (i >= 0) open_lightbox(i);
            return true;
          }
          case GDK_KEY_Right: {
            int i = gallery_next_index(m_order, cur);
            if (i >= 0) open_lightbox(i);
            return true;
          }
          case GDK_KEY_plus:
          case GDK_KEY_equal:
          case GDK_KEY_KP_Add:
            m_lb_zoom = (m_lb_zoom <= 0.0) ? 1.0 : std::min(8.0, m_lb_zoom * 1.25);
            apply_zoom();
            return true;
          case GDK_KEY_minus:
          case GDK_KEY_KP_Subtract:
            if (m_lb_zoom > 0.0) {
              m_lb_zoom /= 1.25;
              if (m_lb_zoom < 0.20) m_lb_zoom = 0.0;
              apply_zoom();
            }
            return true;
          case GDK_KEY_0:
          case GDK_KEY_KP_0:
            m_lb_zoom = 0.0;
            apply_zoom();
            return true;
          default:
            return false;
        }
      }, false);
  m_lightbox.add_controller(key);

  m_overlay.add_overlay(m_lightbox);
  append(m_overlay);
}

void GallerySurface::set_context(DocumentModel* model, FolioPrefs* prefs) {
  m_model = model;
  m_prefs = prefs;
}

void GallerySurface::set_title(const std::string& title) {
  m_title.set_text(title.empty() ? "Gallery" : title);
}

void GallerySurface::clear() {
  m_iid.clear();
  m_order.clear();
  m_links.clear();
  close_lightbox();
  refresh();
  rebuild_assoc_row();
}

void GallerySurface::load(const std::string& iid, const std::string& title,
                          const std::string& body) {
  m_loading = true;
  m_iid = iid;
  set_title(title);
  close_lightbox();

  // The body is the gallery's lens-def: its member order (§11b) AND its own
  // object links (§11a). A gallery is a COLLECTOR — empty body = empty wall, NOT
  // a view of the whole pool. Keep only members whose fragment still lives.
  m_order.clear();
  m_links.clear();
  if (m_model) {
    const ImagePool& pool = m_model->image_pool();
    for (const std::string& fid : gallery_lens_from_json(body)) {
      const ImageFragment* f = pool.find(fid);
      if (f && !f->deleted) m_order.push_back(fid);
    }
    m_links = gallery_links_from_json(body);
  }
  refresh();
  rebuild_assoc_row();
  m_loading = false;
}

void GallerySurface::do_import() {
  if (!m_model || !m_prefs) return;
  auto* win = dynamic_cast<Gtk::Window*>(get_root());
  if (!win) return;
  if (m_model->current_path.empty()) {
    Gtk::AlertDialog::create("Save the project before importing images.")->show(*win);
    return;
  }
  auto dlg = Gtk::FileChooserNative::create(
      "Import Images", *win, Gtk::FileChooser::Action::OPEN, "Import", "Cancel");
  dlg->set_select_multiple(true);
  auto filter = Gtk::FileFilter::create();
  filter->set_name("Images");
  filter->add_mime_type("image/png");
  filter->add_mime_type("image/jpeg");
  filter->add_mime_type("image/webp");
  filter->add_mime_type("image/gif");
  filter->add_mime_type("image/tiff");
  filter->add_mime_type("image/bmp");
  dlg->add_filter(filter);
  dlg->signal_response().connect([this, dlg](int response) {
    if (response != Gtk::ResponseType::ACCEPT) return;
    auto files = dlg->get_files();
    if (!files) return;
    // Read paths through the GIO C API: glibmm can't wrap the concrete GLocalFile
    // items (it warns and would drop them), so go straight to g_file_get_path.
    std::vector<std::string> paths;
    GListModel* lm = files->gobj();
    const guint n = g_list_model_get_n_items(lm);
    for (guint i = 0; i < n; ++i) {
      GFile* gf = G_FILE(g_list_model_get_item(lm, i));   // transfer full
      if (!gf) continue;
      char* cpath = g_file_get_path(gf);
      if (cpath) { paths.emplace_back(cpath); g_free(cpath); }
      g_object_unref(gf);
    }
    import_files(paths);
  });
  dlg->show();
}

// s79 — the shared file import path: picker AND drag-and-drop of files funnel
// here. Loops the file door, appends new fragments to the wall, persists, and
// reports anything that failed or came in low-res. Guarded on a saved project.
void GallerySurface::import_files(const std::vector<std::string>& paths) {
  if (!m_model || !m_prefs) return;
  if (m_model->current_path.empty()) {
    if (auto* win = dynamic_cast<Gtk::Window*>(get_root()))
      Gtk::AlertDialog::create("Save the project before importing images.")->show(*win);
    return;
  }
  ImageImporter imp(m_model->current_path, normalize_policy_from_prefs(*m_prefs));
  const int tier = m_prefs->gallery_default_detail_tier;
  int ok = 0;
  std::vector<std::string> failures;   // one line per failed file
  std::vector<std::string> lowres;     // §13b — imported, but below the chosen detail
  for (const std::string& path : paths) {
    if (path.empty()) continue;
    ImportResult r = imp.import_file(path, m_model->image_pool(), tier);
    if (r.ok) {
      m_order.push_back(r.iid);
      ++ok;
      if (r.low_res) {
        const std::string base = std::filesystem::path(path).filename().string();
        lowres.push_back(base.empty() ? path : base);
      }
    } else {
      failures.push_back(r.error);
    }
  }
  if (ok > 0) { m_model->mark_modified(); persist(); refresh(); }
  report_import(ok, failures, lowres);
}

// s79 — the shared bytes import path: paste AND image-drop (texture) funnel here.
void GallerySurface::import_bytes_blob(const std::string& data) {
  if (!m_model || !m_prefs || data.empty()) return;
  if (m_model->current_path.empty()) {
    if (auto* win = dynamic_cast<Gtk::Window*>(get_root()))
      Gtk::AlertDialog::create("Save the project before importing images.")->show(*win);
    return;
  }
  ImageImporter imp(m_model->current_path, normalize_policy_from_prefs(*m_prefs));
  ImportResult r = imp.import_bytes(data, m_model->image_pool(), std::string{},
                                    m_prefs->gallery_default_detail_tier);
  if (r.ok) {
    m_order.push_back(r.iid);
    m_model->mark_modified();
    persist();
    refresh();
    if (r.low_res) report_import(1, {}, {"pasted image"});
  } else {
    report_import(0, {r.error}, {});
  }
}

void GallerySurface::report_import(int ok, const std::vector<std::string>& failures,
                                   const std::vector<std::string>& lowres) {
  if (failures.empty() && lowres.empty()) return;   // silent only on full success
  auto* win = dynamic_cast<Gtk::Window*>(get_root());
  if (!win) return;
  std::string msg = std::to_string(ok) + " imported";
  if (!failures.empty()) msg += ", " + std::to_string(failures.size()) + " failed";
  msg += ".";
  for (const std::string& e : failures)
    msg += "\n\xE2\x80\xA2 " + e;
  if (!lowres.empty()) {
    msg += "\n\nLower resolution than the chosen detail (stored at native size, "
           "not upscaled):";
    for (const std::string& nm : lowres)
      msg += "\n\xE2\x80\xA2 " + nm;
  }
  Gtk::AlertDialog::create(msg)->show(*win);
}

// s79 — read an image off the clipboard. Image DATA → bytes door; otherwise a
// copied FILE path / file:// URI → file door (arbitrary text ignored).
void GallerySurface::paste_from_clipboard() {
  auto clip = get_clipboard();
  if (!clip) return;
  clip->read_texture_async([this, clip](Glib::RefPtr<Gio::AsyncResult>& res) {
    try {
      auto tex = clip->read_texture_finish(res);
      if (tex) {
        auto bytes = tex->save_to_png_bytes();
        gsize n = 0;
        gconstpointer d = g_bytes_get_data(bytes->gobj(), &n);
        import_bytes_blob(std::string(static_cast<const char*>(d), n));
        return;
      }
    } catch (const Glib::Error&) {
      // No image data — fall through to the file/path fallback.
    }
    clip->read_text_async([this, clip](Glib::RefPtr<Gio::AsyncResult>& r2) {
      std::string path;
      try {
        path = clip->read_text_finish(r2).raw();
      } catch (const Glib::Error&) {
        return;
      }
      while (!path.empty() &&
             (path.back() == '\n' || path.back() == '\r' || path.back() == ' '))
        path.pop_back();
      if (path.rfind("file://", 0) == 0)
        path = Gio::File::create_for_uri(path)->get_path();
      if (!path.empty() && path.front() == '/')
        import_files({path});
    });
  });
}

Gtk::Widget* GallerySurface::make_tile(int pos) {
  const ImagePool& pool = m_model->image_pool();
  const ImageFragment* f = pool.find(m_order[pos]);
  if (!f) return nullptr;

  auto* tile = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
  tile->add_css_class("gallery-tile");
  tile->set_overflow(Gtk::Overflow::HIDDEN);
  tile->set_size_request(kTileW, -1);

  auto* pic = Gtk::make_managed<Gtk::Picture>();
  pic->set_size_request(kTileW, kTileH);
  pic->set_can_shrink(true);
  pic->set_content_fit(Gtk::ContentFit::COVER);
  auto tex = load_texture(thumb_path(m_model->current_path, f->iid, f->ext).string());
  if (tex) pic->set_paintable(tex);

  // hover-revealed delete (✕) over the thumbnail → complete removal (confirmed)
  auto* picwrap = Gtk::make_managed<Gtk::Overlay>();
  picwrap->set_child(*pic);
  auto* del = Gtk::make_managed<Gtk::Button>();
  del->set_icon_name("user-trash-symbolic");
  del->add_css_class("circular");
  del->add_css_class("osd");
  del->set_halign(Gtk::Align::END);
  del->set_valign(Gtk::Align::START);
  del->set_margin_top(4);
  del->set_margin_end(4);
  del->set_visible(false);
  del->set_tooltip_text("Delete image from the project");
  const std::string fid = f->iid;
  del->signal_clicked().connect([this, fid]() { confirm_and_delete(fid); });
  picwrap->add_overlay(*del);
  tile->append(*picwrap);

  auto motion = Gtk::EventControllerMotion::create();
  motion->signal_enter().connect([del](double, double) { del->set_visible(true); });
  motion->signal_leave().connect([del]() { del->set_visible(false); });
  tile->add_controller(motion);

  auto* caprow = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
  caprow->add_css_class("gallery-cap");
  auto* cap = Gtk::make_managed<Gtk::Label>(
      f->caption.empty() ? std::string("\xE2\x80\x94") : f->caption);
  cap->set_ellipsize(Pango::EllipsizeMode::END);
  cap->set_xalign(0.0f);
  cap->set_hexpand(true);
  cap->set_halign(Gtk::Align::START);
  caprow->append(*cap);
  if (!f->accent.empty()) {
    auto* dot = Gtk::make_managed<Gtk::Label>("\xE2\x97\x8F");
    caprow->append(*dot);
  }
  tile->append(*caprow);

  // click → lightbox
  auto click = Gtk::GestureClick::create();
  click->signal_released().connect(
      [this, pos](int, double, double) { open_lightbox(pos); });
  tile->add_controller(click);

  // ── drag-to-reorder (DragSource provides this pos; DropTarget receives it) ──
  auto alive = std::make_shared<bool>(true);
  tile->signal_destroy().connect([alive]() { *alive = false; });

  auto src = Gtk::DragSource::create();
  src->set_actions(Gdk::DragAction::MOVE);
  src->signal_prepare().connect(
      [this, pos](double, double) -> Glib::RefPtr<Gdk::ContentProvider> {
        m_drag_src = pos;
        Glib::Value<int> v;
        v.init(G_TYPE_INT);
        v.set(pos);
        return Gdk::ContentProvider::create(v);
      }, false);
  src->signal_drag_begin().connect(
      [tile, alive](const Glib::RefPtr<Gdk::Drag>&) {
        if (*alive) tile->add_css_class("chip-drag-source");
      }, false);
  src->signal_drag_end().connect(
      [this, tile, alive](const Glib::RefPtr<Gdk::Drag>&, bool) {
        m_drag_src = -1;
        if (*alive) tile->remove_css_class("chip-drag-source");
      }, false);
  tile->add_controller(src);

  auto dst = Gtk::DropTarget::create(G_TYPE_INT, Gdk::DragAction::MOVE);
  dst->signal_drop().connect(
      [this, pos](const Glib::ValueBase&, double, double) -> bool {
        const int from = m_drag_src;   // set in the source's prepare; valid in drop
        if (from < 0) return false;
        reorder(from, pos);
        return true;
      }, false);
  tile->add_controller(dst);

  auto* fbc = Gtk::make_managed<Gtk::FlowBoxChild>();
  fbc->set_child(*tile);
  return fbc;
}

void GallerySurface::reorder(int from_pos, int to_pos) {
  if (from_pos == to_pos) return;
  if (from_pos < 0 || from_pos >= (int)m_order.size()) return;
  if (to_pos < 0 || to_pos >= (int)m_order.size()) return;
  std::string moved = m_order[from_pos];
  m_order.erase(m_order.begin() + from_pos);
  // after erase, indices above from_pos shift down by one
  int insert_at = (to_pos > from_pos) ? to_pos - 1 : to_pos;
  if (insert_at < 0) insert_at = 0;
  if (insert_at > (int)m_order.size()) insert_at = (int)m_order.size();
  m_order.insert(m_order.begin() + insert_at, moved);
  persist();
  refresh();
}

void GallerySurface::refresh() {
  while (Gtk::Widget* c = m_wall.get_first_child())
    m_wall.remove(*c);

  int live = 0;
  if (m_model) {
    for (int pos = 0; pos < (int)m_order.size(); ++pos) {
      if (Gtk::Widget* tile = make_tile(pos)) {
        m_wall.append(*tile);
        ++live;
      }
    }
  }
  m_count.set_text(std::to_string(live) + (live == 1 ? " image" : " images"));
  m_body.set_visible_child(live > 0 ? "wall" : "empty");
}

void GallerySurface::open_lightbox(int pos) {
  if (pos < 0 || pos >= (int)m_order.size()) return;
  m_lb_pos = pos;
  populate_lightbox();
  m_lightbox.set_visible(true);
  m_lightbox.grab_focus();
}

void GallerySurface::close_lightbox() {
  m_lb_pos = -1;
  m_lightbox.set_visible(false);
}

void GallerySurface::populate_lightbox() {
  if (!m_model || m_lb_pos < 0 || m_lb_pos >= (int)m_order.size()) return;
  const std::string fid = m_order[m_lb_pos];
  const ImageFragment* f = m_model->image_pool().find(fid);
  if (!f) return;

  auto tex = load_texture(asset_path(m_model->current_path, f->iid, f->ext).string());
  if (tex) {
    m_lb_pic.set_paintable(tex);
    m_lb_nat_w = tex->get_width();
    m_lb_nat_h = tex->get_height();
  } else {
    m_lb_pic.set_paintable(Glib::RefPtr<Gdk::Paintable>{});
    m_lb_nat_w = m_lb_nat_h = 0;
  }
  m_lb_zoom = 0.0;   // every freshly-shown image starts fit-to-window
  apply_zoom();

  // caption (guard the changed-handler against this programmatic fill)
  m_loading = true;
  m_lb_caption.set_text(f->caption);
  m_loading = false;

  // links row: removable object pills + the always-present ＋ Link picker
  rebuild_link_row();
}

void GallerySurface::rebuild_link_row() {
  while (Gtk::Widget* c = m_lb_chips.get_first_child())
    m_lb_chips.remove(*c);
  if (!m_model || m_lb_pos < 0 || m_lb_pos >= (int)m_order.size())
    return;
  const std::string fid = m_order[m_lb_pos];

  auto edges = StoryGraph::edges_from_backlinks(*m_model);
  for (const std::string& obj : gallery_objects_of(edges, fid)) {
    const BinderNode* n = m_model->find_node_by_iid(obj);
    const std::string label = (n && !n->title.empty()) ? n->title : obj;

    auto* pill = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    pill->add_css_class("gallery-pill");
    pill->append(*make_accent_dot(iid_kind_of(obj)));

    auto* nav = Gtk::make_managed<Gtk::Button>(label);
    nav->add_css_class("flat");
    nav->set_tooltip_text("Open " + label);
    const std::string target = obj;
    nav->signal_clicked().connect([this, target]() {
      close_lightbox();
      if (m_on_open_object) m_on_open_object(target);
    });
    pill->append(*nav);

    auto* x = Gtk::make_managed<Gtk::Button>();
    x->set_label("✕");
    x->add_css_class("flat");
    x->set_tooltip_text("Unlink");
    x->signal_clicked().connect([this, fid, target]() {
      if (m_model->image_pool().unlink_object(fid, target)) {
        m_model->mark_modified();
        rebuild_link_row();
      }
    });
    pill->append(*x);
    m_lb_chips.append(*pill);
  }

  // ＋ Link — always present (the affordance is discoverable even with no links)
  auto* add = Gtk::make_managed<Gtk::Button>("＋ Link");
  add->add_css_class("flat");
  add->add_css_class("gallery-pill-add");
  add->signal_clicked().connect([this, fid, add]() { open_link_picker(fid, *add); });
  m_lb_chips.append(*add);
}

void GallerySurface::open_link_picker(const std::string& image_iid,
                                      Gtk::Widget& anchor) {
  if (!m_model) return;
  auto opts = gather_link_targets(*m_model);

  auto* pop = Gtk::make_managed<Gtk::Popover>();
  pop->set_parent(anchor);
  pop->set_autohide(true);

  auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
  box->set_margin(8);
  auto* search = Gtk::make_managed<Gtk::SearchEntry>();
  search->set_placeholder_text("Link to a scene, character, or place\u2026");
  box->append(*search);

  auto* scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
  scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  scroll->set_min_content_height(240);
  scroll->set_min_content_width(240);
  auto* list = Gtk::make_managed<Gtk::ListBox>();
  list->set_selection_mode(Gtk::SelectionMode::NONE);
  scroll->set_child(*list);
  box->append(*scroll);
  pop->set_child(*box);

  // Rebuilds the list for a query. With no query the list is grouped by section;
  // while filtering the headers drop away. A ✓ marks already-linked objects (the
  // picker doubles as the link manager — activating a ✓'d row unlinks it). The
  // iid is stashed in the row name (the proven link-picker idiom).
  auto populate = std::make_shared<std::function<void(const std::string&)>>();
  *populate = [this, list, opts, image_iid](const std::string& q) {
    Gtk::Widget* c = list->get_first_child();
    while (c) { Gtk::Widget* nx = c->get_next_sibling(); list->remove(*c); c = nx; }

    std::string lo = q;
    std::transform(lo.begin(), lo.end(), lo.begin(), ::tolower);
    const bool filtering = !lo.empty();

    std::vector<std::string> linked;
    if (const ImageFragment* f = m_model->image_pool().find(image_iid))
      linked = f->links;
    auto is_linked = [&](const std::string& iid) {
      for (const std::string& l : linked) if (l == iid) return true;
      return false;
    };

    std::string cur_group;
    for (const ObjPick& o : opts) {
      std::string nl = o.name;
      std::transform(nl.begin(), nl.end(), nl.begin(), ::tolower);
      if (filtering && nl.find(lo) == std::string::npos) continue;

      if (!filtering && o.group != cur_group) {
        cur_group = o.group;
        auto* hrow = Gtk::make_managed<Gtk::ListBoxRow>();
        hrow->set_activatable(false);
        hrow->set_selectable(false);
        auto* hl = Gtk::make_managed<Gtk::Label>(cur_group);
        hl->add_css_class("dim-label");
        hl->set_xalign(0.0f);
        hl->set_margin_top(6);
        hl->set_margin_start(6);
        hl->set_margin_bottom(2);
        hrow->set_child(*hl);
        list->append(*hrow);
      }

      auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
      auto* rb = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
      rb->set_margin_top(4);
      rb->set_margin_bottom(4);
      rb->set_margin_start(6);
      rb->set_margin_end(6);
      rb->append(*make_accent_dot(iid_kind_of(o.iid)));
      auto* lab = Gtk::make_managed<Gtk::Label>(o.name.empty() ? o.iid : o.name);
      lab->set_xalign(0.0f);
      lab->set_hexpand(true);
      lab->set_halign(Gtk::Align::START);
      rb->append(*lab);
      if (is_linked(o.iid))
        rb->append(*Gtk::make_managed<Gtk::Label>("\u2713"));  // ✓
      row->set_child(*rb);
      row->set_name(o.iid);
      list->append(*row);
    }
  };
  (*populate)("");
  search->signal_search_changed().connect(
      [search, populate]() { (*populate)(search->get_text()); });

  // Toggle the link. We do NOT rebuild the pills row here — the popover is
  // parented to the ＋ Link button, which that rebuild would destroy mid-show.
  // Instead we re-draw the ✓ live and rebuild the pills when the popover closes.
  list->signal_row_activated().connect(
      [this, image_iid, search, populate](Gtk::ListBoxRow* row) {
        if (!row) return;
        const std::string iid = row->get_name();
        if (iid.empty()) return;  // a section header
        ImagePool& pool = m_model->image_pool();
        bool was = false;
        if (const ImageFragment* f = pool.find(image_iid))
          for (const std::string& l : f->links)
            if (l == iid) { was = true; break; }
        if (was) pool.unlink_object(image_iid, iid);
        else      pool.link_object(image_iid, iid);
        m_model->mark_modified();
        (*populate)(search->get_text());  // update ✓; keep the popover open
      });

  pop->signal_closed().connect([this, pop]() {
    pop->unparent();
    rebuild_link_row();  // now safe: the popover (and its anchor's use) is gone
  });
  pop->popup();
  search->grab_focus();
}

// ── §11 — the gallery's OWN object links (mirror of the lightbox chip row, on
//    the header sub-row). The links live in m_links (this node's body), not on a
//    fragment, so the toggle is a list op + persist, not a pool mutation. ──────
void GallerySurface::rebuild_assoc_row() {
  while (Gtk::Widget* c = m_assoc_row.get_first_child())
    m_assoc_row.remove(*c);
  if (!m_model) return;

  auto* lead = Gtk::make_managed<Gtk::Label>("About:");
  lead->add_css_class("dim-label");
  lead->set_valign(Gtk::Align::CENTER);
  m_assoc_row.append(*lead);

  for (const std::string& obj : m_links) {
    const BinderNode* n = m_model->find_node_by_iid(obj);
    const std::string label = (n && !n->title.empty()) ? n->title : obj;

    auto* pill = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    pill->add_css_class("gallery-pill");
    pill->append(*make_accent_dot(iid_kind_of(obj)));

    auto* nav = Gtk::make_managed<Gtk::Button>(label);
    nav->add_css_class("flat");
    nav->set_tooltip_text("Open " + label);
    const std::string target = obj;
    nav->signal_clicked().connect([this, target]() {
      if (m_on_open_object) m_on_open_object(target);
    });
    pill->append(*nav);

    auto* x = Gtk::make_managed<Gtk::Button>();
    x->set_label("✕");
    x->add_css_class("flat");
    x->set_tooltip_text("Unlink");
    x->signal_clicked().connect([this, target]() {
      if (gallery_member_remove(m_links, target)) {
        m_model->mark_modified();
        persist();
        rebuild_assoc_row();
      }
    });
    pill->append(*x);
    m_assoc_row.append(*pill);
  }

  auto* add = Gtk::make_managed<Gtk::Button>("＋ Link");
  add->add_css_class("flat");
  add->add_css_class("gallery-pill-add");
  add->signal_clicked().connect([this, add]() { open_gallery_link_picker(*add); });
  m_assoc_row.append(*add);
}

void GallerySurface::open_gallery_link_picker(Gtk::Widget& anchor) {
  if (!m_model) return;
  auto opts = gather_link_targets(*m_model);

  auto* pop = Gtk::make_managed<Gtk::Popover>();
  pop->set_parent(anchor);
  pop->set_autohide(true);

  auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
  box->set_margin(8);
  auto* search = Gtk::make_managed<Gtk::SearchEntry>();
  search->set_placeholder_text("Link this gallery to a scene, character, or place\u2026");
  box->append(*search);

  auto* scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
  scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  scroll->set_min_content_height(240);
  scroll->set_min_content_width(240);
  auto* list = Gtk::make_managed<Gtk::ListBox>();
  list->set_selection_mode(Gtk::SelectionMode::NONE);
  scroll->set_child(*list);
  box->append(*scroll);
  pop->set_child(*box);

  // Same grouped list + live-✓ idiom as the image picker; "linked" reads m_links
  // (the gallery's own body list), and the toggle is gallery_member_add/remove.
  auto populate = std::make_shared<std::function<void(const std::string&)>>();
  *populate = [this, list, opts](const std::string& q) {
    Gtk::Widget* c = list->get_first_child();
    while (c) { Gtk::Widget* nx = c->get_next_sibling(); list->remove(*c); c = nx; }

    std::string lo = q;
    std::transform(lo.begin(), lo.end(), lo.begin(), ::tolower);
    const bool filtering = !lo.empty();

    auto is_linked = [&](const std::string& iid) {
      for (const std::string& l : m_links) if (l == iid) return true;
      return false;
    };

    std::string cur_group;
    for (const ObjPick& o : opts) {
      std::string nl = o.name;
      std::transform(nl.begin(), nl.end(), nl.begin(), ::tolower);
      if (filtering && nl.find(lo) == std::string::npos) continue;

      if (!filtering && o.group != cur_group) {
        cur_group = o.group;
        auto* hrow = Gtk::make_managed<Gtk::ListBoxRow>();
        hrow->set_activatable(false);
        hrow->set_selectable(false);
        auto* hl = Gtk::make_managed<Gtk::Label>(cur_group);
        hl->add_css_class("dim-label");
        hl->set_xalign(0.0f);
        hl->set_margin_top(6);
        hl->set_margin_start(6);
        hl->set_margin_bottom(2);
        hrow->set_child(*hl);
        list->append(*hrow);
      }

      auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
      auto* rb = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
      rb->set_margin_top(4);
      rb->set_margin_bottom(4);
      rb->set_margin_start(6);
      rb->set_margin_end(6);
      rb->append(*make_accent_dot(iid_kind_of(o.iid)));
      auto* lab = Gtk::make_managed<Gtk::Label>(o.name.empty() ? o.iid : o.name);
      lab->set_xalign(0.0f);
      lab->set_hexpand(true);
      lab->set_halign(Gtk::Align::START);
      rb->append(*lab);
      if (is_linked(o.iid))
        rb->append(*Gtk::make_managed<Gtk::Label>("\u2713"));  // ✓
      row->set_child(*rb);
      row->set_name(o.iid);
      list->append(*row);
    }
  };
  (*populate)("");
  search->signal_search_changed().connect(
      [search, populate]() { (*populate)(search->get_text()); });

  list->signal_row_activated().connect(
      [this, search, populate](Gtk::ListBoxRow* row) {
        if (!row) return;
        const std::string iid = row->get_name();
        if (iid.empty()) return;  // a section header
        bool was = false;
        for (const std::string& l : m_links)
          if (l == iid) { was = true; break; }
        if (was) gallery_member_remove(m_links, iid);
        else      gallery_member_add(m_links, iid);
        m_model->mark_modified();
        persist();
        (*populate)(search->get_text());  // update ✓; keep the popover open
      });

  pop->signal_closed().connect([this, pop]() {
    pop->unparent();
    rebuild_assoc_row();  // safe once the popover (and its anchor) is gone
  });
  pop->popup();
  search->grab_focus();
}

// ── §11c — the "add existing" door: hang a fragment already in the pool on THIS
//    wall (the second-wall gesture import alone couldn't do). A popover picker
//    mirroring the link picker, listing pool fragments not already hung; a row
//    toggles membership (gallery_member_add/remove) live, persists, re-walls. ──
void GallerySurface::open_add_existing() {
  if (!m_model) return;

  auto* pop = Gtk::make_managed<Gtk::Popover>();
  pop->set_parent(m_add_btn);
  pop->set_autohide(true);

  auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
  box->set_margin(8);
  auto* search = Gtk::make_managed<Gtk::SearchEntry>();
  search->set_placeholder_text("Add an image from the project to this wall\u2026");
  box->append(*search);

  auto* scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
  scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  scroll->set_min_content_height(300);
  scroll->set_min_content_width(300);
  auto* list = Gtk::make_managed<Gtk::ListBox>();
  list->set_selection_mode(Gtk::SelectionMode::NONE);
  scroll->set_child(*list);
  box->append(*scroll);

  auto* empty = Gtk::make_managed<Gtk::Label>(
      "No other images in the project yet — use Import\xE2\x80\xA6 first.");
  empty->add_css_class("dim-label");
  empty->set_wrap(true);
  empty->set_margin(8);
  box->append(*empty);
  pop->set_child(*box);

  auto on_this_wall = [this](const std::string& iid) {
    for (const std::string& m : m_order) if (m == iid) return true;
    return false;
  };

  auto populate = std::make_shared<std::function<void(const std::string&)>>();
  *populate = [this, list, empty, on_this_wall](const std::string& q) {
    ImagePool& pool = m_model->image_pool();
    Gtk::Widget* c = list->get_first_child();
    while (c) { Gtk::Widget* nx = c->get_next_sibling(); list->remove(*c); c = nx; }

    std::string lo = q;
    std::transform(lo.begin(), lo.end(), lo.begin(), ::tolower);

    int shown = 0;
    for (std::size_t i : pool.live_view()) {
      const ImageFragment& f = pool.all()[i];
      std::string cl = f.caption;
      std::transform(cl.begin(), cl.end(), cl.begin(), ::tolower);
      if (!lo.empty() && cl.find(lo) == std::string::npos &&
          f.iid.find(lo) == std::string::npos)
        continue;
      ++shown;

      auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
      auto* rb = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
      rb->set_margin_top(4);
      rb->set_margin_bottom(4);
      rb->set_margin_start(6);
      rb->set_margin_end(6);

      auto* thumb = Gtk::make_managed<Gtk::Picture>();
      thumb->set_size_request(48, 36);
      thumb->set_can_shrink(true);
      thumb->set_content_fit(Gtk::ContentFit::COVER);
      if (auto tex = load_texture(
              thumb_path(m_model->current_path, f.iid, f.ext).string()))
        thumb->set_paintable(tex);
      rb->append(*thumb);

      auto* lab = Gtk::make_managed<Gtk::Label>(
          f.caption.empty() ? f.iid : f.caption);
      lab->set_xalign(0.0f);
      lab->set_hexpand(true);
      lab->set_halign(Gtk::Align::START);
      lab->set_ellipsize(Pango::EllipsizeMode::END);
      rb->append(*lab);

      if (on_this_wall(f.iid))
        rb->append(*Gtk::make_managed<Gtk::Label>("\u2713"));  // already hung
      row->set_child(*rb);
      row->set_name(f.iid);
      list->append(*row);
    }
    empty->set_visible(shown == 0);
    list->set_visible(shown != 0);
  };
  (*populate)("");
  search->signal_search_changed().connect(
      [search, populate]() { (*populate)(search->get_text()); });

  list->signal_row_activated().connect(
      [this, search, populate](Gtk::ListBoxRow* row) {
        if (!row) return;
        const std::string iid = row->get_name();
        if (iid.empty()) return;
        bool was = false;
        for (const std::string& m : m_order) if (m == iid) { was = true; break; }
        if (was) gallery_member_remove(m_order, iid);
        else      gallery_member_add(m_order, iid);
        m_model->mark_modified();
        persist();
        refresh();
        (*populate)(search->get_text());  // live ✓; keep the picker open
      });

  pop->signal_closed().connect([pop]() { pop->unparent(); });
  pop->popup();
  search->grab_focus();
}

void GallerySurface::apply_zoom() {
  if (m_lb_zoom <= 0.0 || m_lb_nat_w <= 0 || m_lb_nat_h <= 0) {
    // Fit-to-window: fill the viewport, scaled down, never upscaled; no pan.
    m_lb_pic.set_size_request(-1, -1);
    m_lb_pic.set_halign(Gtk::Align::FILL);
    m_lb_pic.set_valign(Gtk::Align::FILL);
    m_lb_pic.set_content_fit(Gtk::ContentFit::SCALE_DOWN);
    m_lb_zoom_lbl.set_text("Fit");
    return;
  }
  // Explicit zoom: request natural×factor; the ScrolledWindow pans when it
  // exceeds the viewport, and centres it when smaller.
  const int w = std::max(1, (int)std::lround(m_lb_nat_w * m_lb_zoom));
  const int h = std::max(1, (int)std::lround(m_lb_nat_h * m_lb_zoom));
  m_lb_pic.set_content_fit(Gtk::ContentFit::CONTAIN);
  m_lb_pic.set_halign(Gtk::Align::CENTER);
  m_lb_pic.set_valign(Gtk::Align::CENTER);
  m_lb_pic.set_size_request(w, h);
  m_lb_zoom_lbl.set_text(std::to_string((int)std::lround(m_lb_zoom * 100)) + "%");
}

void GallerySurface::confirm_and_delete(const std::string& iid) {
  if (!m_model || iid.empty()) return;
  auto* win = dynamic_cast<Gtk::Window*>(get_root());
  if (!win) return;
  auto dlg = Gtk::AlertDialog::create("Delete this image from the project?");
  dlg->set_detail("The image file is removed to reclaim space. This can't be undone.");
  dlg->set_modal(true);
  dlg->set_buttons({"Cancel", "Delete"});
  dlg->set_cancel_button(0);
  dlg->set_default_button(0);
  dlg->choose(*win, [this, dlg, iid](Glib::RefPtr<Gio::AsyncResult>& res) mutable {
    int response = 0;
    try { response = dlg->choose_finish(res); } catch (...) {}
    if (response != 1) return;
    close_lightbox();
    delete_image(iid);
    m_model->mark_modified();
    persist();
    refresh();
  });
}

void GallerySurface::delete_image(const std::string& iid) {
  if (!m_model) return;
  std::string ext;
  if (const ImageFragment* f = m_model->image_pool().find(iid))
    ext = f->ext;
  // Reclaim the bytes: remove the asset + thumbnail from the bundle.
  if (!m_model->current_path.empty()) {
    std::error_code ec;
    fs::remove(asset_path(m_model->current_path, iid, ext), ec);
    fs::remove(thumb_path(m_model->current_path, iid, ext), ec);
  }
  m_model->image_pool().purge(iid);
  m_order.erase(std::remove(m_order.begin(), m_order.end(), iid), m_order.end());
}

void GallerySurface::persist() {
  if (m_loading || m_iid.empty() || !m_on_persist) return;
  m_on_persist(m_iid, gallery_lens_to_json(m_order, m_links));
}

}  // namespace Folio
