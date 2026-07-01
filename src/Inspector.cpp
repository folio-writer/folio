// ─────────────────────────────────────────────────────────────────────────────
// Folio — Inspector.cpp
// ─────────────────────────────────────────────────────────────────────────────
#include "Inspector.hpp"
#include "BarcodeDialog.hpp"
#include "BarcodeGenerator.hpp"
#include "ObjectIO.hpp"   // s32 — floor_field_to_leaf: editable form write-through
#include "TemplateBuilderDialog.hpp"  // s33 — schema editor opened from the form
#include "FolioLog.hpp"
#include "Iid.hpp"
#include <librsvg/rsvg.h>
#include "SnapshotDiff.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <gdk/gdkkeysyms.h>
#include <gdkmm/pixbuf.h>
#include <giomm/appinfo.h>
#include <giomm/file.h>
#include <glibmm/base64.h>
#include <gtkmm/filechoosernative.h>
#include <iomanip>
#include <sstream>

namespace Folio {

Inspector::~Inspector() = default;

static std::string inspector_now_iso8601() {
  auto now = std::chrono::system_clock::now();
  auto time = std::chrono::system_clock::to_time_t(now);
  std::tm tm_buf{};
  gmtime_r(&time, &tm_buf); // thread-safe POSIX alternative to gmtime()
  std::ostringstream ss;
  ss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ");
  return ss.str();
}

Inspector::Inspector(DocumentModel &model, FolioPrefs &prefs)
    : Gtk::Box(Gtk::Orientation::VERTICAL),
      m_model(model), m_prefs(prefs),
      m_tab_bar(Gtk::Orientation::HORIZONTAL, 2),
      m_meta_box(Gtk::Orientation::VERTICAL, 12),
      m_notes_outer(Gtk::Orientation::VERTICAL, 0), m_notes_ctx_label(),
      m_notes_list(Gtk::Orientation::VERTICAL, 6),
      m_notes_add_row(Gtk::Orientation::HORIZONTAL, 6),
      m_history_outer(Gtk::Orientation::VERTICAL, 8) {
  add_css_class("folio-inspector");
  set_size_request(260, -1);
  set_hexpand(false);

  m_synopsis_buffer = Gtk::TextBuffer::create();
  m_char_notes_buffer = Gtk::TextBuffer::create();
  m_place_notes_buffer = Gtk::TextBuffer::create();
  m_proj_synopsis_buffer = Gtk::TextBuffer::create();

  build_tab_bar();
  build_meta_tab();
  build_notes_tab();
  build_history_tab();
  build_project_tab();
  build_annotations_tab();

  // s41 — the object form (and its relation provider) moved to the Editor (the
  // inversion). s44 §11 — the instance "Edit fields…" door is retired entirely;
  // the Inspector keeps only the template builder it owns, reached from a Template
  // node via open_template_builder_for_template_node().
}

// ─────────────────────────────────────────────────────────────────────────────
// Tab bar
// ─────────────────────────────────────────────────────────────────────────────

void Inspector::build_tab_bar() {
  m_tab_bar.add_css_class("inspector-tab-bar");
  m_tab_meta.set_label("Metadata");
  m_tab_notes.set_label("Notes");
  m_tab_history.set_label("Snapshots");
  m_tab_project.set_label("Project");
  m_tab_meta.set_group(m_tab_project);
  m_tab_notes.set_group(m_tab_project);
  m_tab_history.set_group(m_tab_project);
  for (auto *t : {&m_tab_project, &m_tab_meta, &m_tab_notes, &m_tab_history}) {
    t->add_css_class("inspector-tab");
    t->set_hexpand(true);
    m_tab_bar.append(*t);
  }
  m_tab_meta.set_active(true);
  m_tab_project.signal_toggled().connect([this]() {
    if (m_tab_project.get_active())
      show_tab(0);
  });
  m_tab_meta.signal_toggled().connect([this]() {
    if (m_tab_meta.get_active())
      show_tab(1);
  });
  m_tab_notes.signal_toggled().connect([this]() {
    if (m_tab_notes.get_active())
      show_tab(2);
  });
  m_tab_history.signal_toggled().connect([this]() {
    if (m_tab_history.get_active())
      show_tab(3);
  });
  append(m_tab_bar);
  m_stack.set_vexpand(true);
  m_stack.set_transition_type(Gtk::StackTransitionType::CROSSFADE);
  m_stack.set_transition_duration(120);
  append(m_stack);

  // ── Progress footer — fixed at bottom, visible only for node-meta ────────
  m_progress_footer.set_orientation(Gtk::Orientation::VERTICAL);
  m_progress_footer.add_css_class("inspector-progress-footer");
  m_progress_footer.set_margin_start(8);
  m_progress_footer.set_margin_end(8);
  m_progress_footer.set_margin_top(4);
  m_progress_footer.set_margin_bottom(12);

  // ── Disclosure header row ──────────────────────────────────────────────
  auto *prog_hdr = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
  prog_hdr->add_css_class("tile-header-row");
  prog_hdr->set_cursor(Gdk::Cursor::create("pointer"));
  prog_hdr->set_margin_bottom(2);

  auto *prog_lbl_hdr = Gtk::make_managed<Gtk::Label>("Progress");
  prog_lbl_hdr->add_css_class("inspector-section-label");
  prog_lbl_hdr->set_halign(Gtk::Align::START);
  prog_lbl_hdr->set_hexpand(true);

  m_progress_arrow.set_text(m_progress_expanded ? "▾" : "▸");
  m_progress_arrow.add_css_class("section-arrow");
  m_progress_arrow.set_margin_end(2);

  prog_hdr->append(*prog_lbl_hdr);
  prog_hdr->append(m_progress_arrow);
  m_progress_footer.append(*prog_hdr);

  // ── Revealer wraps label + bar ─────────────────────────────────────────
  m_progress_revealer.set_reveal_child(m_progress_expanded);
  m_progress_revealer.set_transition_type(
      Gtk::RevealerTransitionType::SLIDE_DOWN);
  m_progress_revealer.set_transition_duration(180);

  auto *prog_body = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);

  m_target_progress_lbl.add_css_class("pref-row-sub");
  m_target_progress_lbl.set_halign(Gtk::Align::CENTER);
  m_target_progress_lbl.set_margin_top(4);
  prog_body->append(m_target_progress_lbl);

  m_target_bar.set_min_value(0);
  m_target_bar.set_max_value(1.0);
  m_target_bar.set_value(0);
  m_target_bar.set_margin_top(6);
  prog_body->append(m_target_bar);

  m_progress_revealer.set_child(*prog_body);
  m_progress_footer.append(m_progress_revealer);

  // Click on header row toggles the revealer
  auto gc_prog = Gtk::GestureClick::create();
  gc_prog->set_button(1);
  gc_prog->signal_pressed().connect([this](int, double, double) {
    set_progress_expanded(!m_progress_expanded);
    if (m_on_progress_disclosure_changed)
      m_on_progress_disclosure_changed(m_progress_expanded);
  });
  prog_hdr->add_controller(gc_prog);

  // Note: m_progress_footer is built here and appended inside
  // build_node_meta_section()
}

void Inspector::show_tab(int idx) {
  switch (idx) {
  case 0:
    m_stack.set_visible_child(m_proj_wrapper);
    break;
  case 1:
    m_stack.set_visible_child(m_meta_wrapper);
    break;
  case 2:
    refresh_annotations();
    m_stack.set_visible_child(m_notes_paned);
    break;
  case 3:
    m_stack.set_visible_child(m_history_outer);
    break;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

Gtk::ListBox *Inspector::make_listbox() {
  auto *lb = Gtk::make_managed<Gtk::ListBox>();
  lb->set_selection_mode(Gtk::SelectionMode::NONE);
  lb->add_css_class("pref-listbox");
  return lb;
}

Gtk::Box *Inspector::make_pref_row(const std::string &label,
                                   Gtk::Widget &widget) {
  auto *rb = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
  rb->set_margin_start(8);
  rb->set_margin_end(8);
  rb->set_margin_top(3);
  rb->set_margin_bottom(3);
  auto *l = Gtk::make_managed<Gtk::Label>(label);
  l->add_css_class("pref-row-label");
  l->set_hexpand(true);
  l->set_halign(Gtk::Align::START);
  widget.set_halign(Gtk::Align::END);
  rb->append(*l);
  rb->append(widget);
  return rb;
}

// ─────────────────────────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────
// Color dropdown
// ─────────────────────────────────────────────────────────────────────────────

// Default palette used when prefs has no tag_colors defined.
namespace {
struct DefaultColor {
  const char *name;
  const char *hex;
};
static const DefaultColor k_default_colors[] = {
    {"Teal", "#94e2d5"}, {"Mauve", "#cba6f7"},    {"Peach", "#fab387"},
    {"Blue", "#89b4fa"}, {"Flamingo", "#f38ba8"}, {"Green", "#a6e3a1"},
    {"Red", "#f38ba8"},  {"Yellow", "#f9e2af"},   {"Sky", "#89dceb"},
};
} // namespace

Gtk::DropDown *
Inspector::build_color_dropdown(std::function<void(int)> setter) {
  // Build entry list: None + prefs colors (falling back to defaults)
  // Each entry: index 0 = None, 1..N = colors in prefs order
  // We store name+hex+NodeColor. Map name→NodeColor via node_color_from_str.

  auto strings = Gtk::StringList::create({});
  strings->append("None");

  // Use prefs if non-empty, else defaults
  bool use_prefs = !m_prefs.tag_colors.empty();
  if (use_prefs) {
    for (const auto &tc : m_prefs.tag_colors)
      strings->append(tc.name);
  } else {
    for (const auto &dc : k_default_colors)
      strings->append(dc.name);
  }

  auto *dd = Gtk::make_managed<Gtk::DropDown>(strings);
  dd->set_hexpand(true);

  auto factory = Gtk::SignalListItemFactory::create();
  factory->signal_setup().connect([](const Glib::RefPtr<Gtk::ListItem> &item) {
    auto *row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    row->set_margin_start(4);
    auto *chip = Gtk::make_managed<Gtk::Label>(" ");
    chip->add_css_class("color-chip");
    chip->set_size_request(16, 16);
    auto *lbl = Gtk::make_managed<Gtk::Label>();
    lbl->set_halign(Gtk::Align::START);
    row->append(*chip);
    row->append(*lbl);
    item->set_child(*row);
  });
  factory->signal_bind().connect([this, use_prefs](
                                     const Glib::RefPtr<Gtk::ListItem> &item) {
    guint idx = item->get_position();
    auto *row = dynamic_cast<Gtk::Box *>(item->get_child());
    if (!row)
      return;
    auto *chip = dynamic_cast<Gtk::Label *>(row->get_first_child());
    auto *lbl =
        chip ? dynamic_cast<Gtk::Label *>(chip->get_next_sibling()) : nullptr;
    if (!chip || !lbl)
      return;

    if (idx == 0) {
      chip->set_visible(false);
      lbl->set_text("None");
      return;
    }
    chip->set_visible(true);
    std::string name, hex;
    if (use_prefs && idx - 1 < m_prefs.tag_colors.size()) {
      name = m_prefs.tag_colors[idx - 1].name;
      hex = m_prefs.tag_colors[idx - 1].hex;
    } else {
      int di = (int)(idx - 1);
      int nd = (int)(sizeof(k_default_colors) / sizeof(k_default_colors[0]));
      if (di < nd) {
        name = k_default_colors[di].name;
        hex = k_default_colors[di].hex;
      }
    }
    // Apply inline CSS for the chip background
    auto provider = Gtk::CssProvider::create();
    provider->load_from_data(".dyn-chip { background-color: " + hex + "; }");
    chip->add_css_class("dyn-chip");
    chip->get_style_context()->add_provider(
        provider, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    lbl->set_text(name);
  });
  dd->set_factory(factory);
  dd->set_list_factory(factory);

  dd->property_selected().signal_changed().connect([this, dd, setter]() {
    if (m_loading)
      return;
    guint idx = dd->get_selected();
    setter((int)idx); // 0=None, 1-based into prefs
    m_model.mark_modified();
    notify_meta_changed();
  });
  return dd;
}

void Inspector::rebuild_status_dropdown() {
  if (!m_status_dropdown)
    return;
  auto *sl =
      dynamic_cast<Gtk::StringList *>(m_status_dropdown->get_model().get());
  if (!sl)
    return;
  // NOTE: m_loading is managed by the caller — do not set/clear it here
  while (sl->get_n_items() > 0)
    sl->remove(0);
  sl->append("None");
  const auto &statuses =
      m_prefs.statuses.empty()
          ? std::vector<StatusDef>{{"Rough Draft", "#f9e2af"},
                                   {"In Progress", "#89b4fa"},
                                   {"Polished", "#a6e3a1"},
                                   {"Skip", "#6c7086"}}
          : m_prefs.statuses;
  for (const auto &s : statuses)
    sl->append(s.name);
  // Rebuild factory so colour chips reflect current prefs
  auto status_factory = Gtk::SignalListItemFactory::create();
  status_factory->signal_setup().connect(
      [](const Glib::RefPtr<Gtk::ListItem> &item) {
        auto *r = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        r->set_margin_start(4);
        auto *chip = Gtk::make_managed<Gtk::Label>(" ");
        chip->add_css_class("status-chip");
        chip->set_size_request(16, 16);
        auto *lbl = Gtk::make_managed<Gtk::Label>();
        lbl->set_halign(Gtk::Align::START);
        r->append(*chip);
        r->append(*lbl);
        item->set_child(*r);
      });
  status_factory->signal_bind().connect(
      [this](const Glib::RefPtr<Gtk::ListItem> &item) {
        guint idx = item->get_position();
        auto *r = dynamic_cast<Gtk::Box *>(item->get_child());
        if (!r)
          return;
        auto *chip = dynamic_cast<Gtk::Label *>(r->get_first_child());
        auto *lbl = chip ? dynamic_cast<Gtk::Label *>(chip->get_next_sibling())
                         : nullptr;
        if (!chip || !lbl)
          return;
        if (idx == 0) {
          chip->set_visible(false);
          lbl->set_text("None");
          return;
        }
        chip->set_visible(true);
        const auto &sts =
            m_prefs.statuses.empty()
                ? std::vector<StatusDef>{{"Rough Draft", "#f9e2af"},
                                         {"In Progress", "#89b4fa"},
                                         {"Polished", "#a6e3a1"},
                                         {"Skip", "#6c7086"}}
                : m_prefs.statuses;
        if (idx - 1 < sts.size()) {
          lbl->set_text(sts[idx - 1].name);
          std::string hex = sts[idx - 1].color_hex.empty()
                                ? "#888888"
                                : sts[idx - 1].color_hex;
          auto prov = Gtk::CssProvider::create();
          prov->load_from_data(".dyn-status-chip { background-color: " + hex +
                               "; }");
          chip->add_css_class("dyn-status-chip");
          chip->get_style_context()->add_provider(
              prov, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        }
      });
  m_status_dropdown->set_factory(status_factory);
  m_status_dropdown->set_list_factory(status_factory);
}

static void rebuild_color_string_list(Gtk::DropDown *dd,
                                      const std::vector<TagColor> &tag_colors) {
  if (!dd)
    return;
  auto *sl = dynamic_cast<Gtk::StringList *>(dd->get_model().get());
  if (!sl)
    return;
  while (sl->get_n_items() > 0)
    sl->remove(0);
  sl->append("None");
  if (!tag_colors.empty()) {
    for (const auto &tc : tag_colors)
      sl->append(tc.name);
  } else {
    static const char *defaults[] = {"Teal", "Mauve",    "Peach",
                                     "Blue", "Flamingo", "Green",
                                     "Red",  "Yellow",   "Sky"};
    for (auto *n : defaults)
      sl->append(n);
  }
}

void Inspector::refresh_prefs_dropdowns() {
  m_loading =
      true; // prevent signal_changed from clobbering model data during rebuild

  rebuild_status_dropdown();
  rebuild_color_string_list(m_color_dropdown, m_prefs.tag_colors);
  rebuild_color_string_list(m_char_color_dropdown, m_prefs.tag_colors);
  rebuild_color_string_list(m_place_color_dropdown, m_prefs.tag_colors);

  // Rebuild role dropdown from prefs
  if (m_char_role_dropdown) {
    auto *sl = dynamic_cast<Gtk::StringList *>(
        m_char_role_dropdown->get_model().get());
    if (sl) {
      while (sl->get_n_items() > 0)
        sl->remove(0);
      sl->append("—");
      for (auto &r : m_prefs.character_roles)
        sl->append(r);
    }
  }

  // Rebuild genre dropdown list from prefs, then restore the current selection
  // by matching m_model.genre (a plain string) against the new items.
  // m_model.genre is written only when the user explicitly picks an item —
  // rebuilding the list must never overwrite it.
  if (m_proj_genre_dropdown) {
    auto new_model = Gtk::StringList::create({"—"});
    for (auto &g : m_prefs.genres)
      new_model->append(g);
    m_proj_genre_dropdown->set_model(new_model);
    // Restore visual selection without triggering signal_changed
    guint gidx = 0;
    if (!m_model.genre.empty())
      for (guint i = 1; i < new_model->get_n_items(); ++i)
        if (std::string(new_model->get_string(i)) == m_model.genre) {
          gidx = i;
          break;
        }
    m_proj_genre_dropdown->set_selected(gidx);
  }

  // Re-sync node-specific dropdowns (color, status, role)
  if (m_current_node) {
    sync_color_dropdown(m_color_dropdown, m_current_node->color_idx);
    sync_color_dropdown(m_char_color_dropdown, m_current_node->color_idx);
    sync_color_dropdown(m_place_color_dropdown, m_current_node->color_idx);
    if (m_status_dropdown) {
      std::string target;
      switch (m_current_node->status) {
      case NodeStatus::RoughDraft:  target = "Rough Draft"; break;
      case NodeStatus::InProgress:  target = "In Progress"; break;
      case NodeStatus::Polished:    target = "Polished";    break;
      case NodeStatus::Skip:        target = "Skip";        break;
      default: break;
      }
      guint sidx = 0;
      auto *sl = dynamic_cast<Gtk::StringList *>(m_status_dropdown->get_model().get());
      if (sl && !target.empty())
        for (guint i = 1; i < sl->get_n_items(); ++i)
          if (std::string(sl->get_string(i)) == target) { sidx = i; break; }
      m_status_dropdown->set_selected(sidx);
    }
    if (m_char_role_dropdown && m_current_node->kind == BinderKind::Character) {
      guint ridx = 0;
      auto *sl = dynamic_cast<Gtk::StringList *>(m_char_role_dropdown->get_model().get());
      if (sl && !m_current_node->role.empty())
        for (guint i = 1; i < sl->get_n_items(); ++i)
          if (std::string(sl->get_string(i)) == m_current_node->role) { ridx = i; break; }
      m_char_role_dropdown->set_selected(ridx);
    }
    sync_thread_dropdown(m_current_node);   // s84 — assigned thread (None + registry)
  }

  m_loading = false;
}

void Inspector::sync_color_dropdown(Gtk::DropDown *dd, int color_idx) {
  if (!dd)
    return;
  auto *model = dynamic_cast<Gtk::StringList *>(dd->get_model().get());
  if (!model)
    return;
  guint n = model->get_n_items();
  guint sel = (color_idx >= 0 && (guint)color_idx < n) ? (guint)color_idx : 0;
  dd->set_selected(sel);
}

// s84 — rebuild the Thread dropdown from the project thread registry (None + one
// row per thread, by label) and select the node's assigned thread. Called from
// the node-load sync path (m_loading is true there, so set_model/set_selected do
// not re-fire the assignment handler) and from the add-thread flow (self-guarded).
void Inspector::sync_thread_dropdown(const BinderNode* node) {
  if (!m_thread_dropdown)
    return;
  auto sl = Gtk::StringList::create({});
  sl->append("None");
  const auto& th = m_model.threads();
  for (const auto& t : th)
    sl->append(t.label.empty() ? t.iid : t.label);
  m_thread_dropdown->set_model(sl);

  guint sel = 0;
  if (node && !node->thread.empty())
    for (guint i = 0; i < th.size(); ++i)
      if (th[i].iid == node->thread) { sel = i + 1; break; }
  m_thread_dropdown->set_selected(sel);
}

// s81 — drive the Key-Point cycle button (off → key → pin) from a node. The icon
// swaps (key for off/beat, pin for the promoted target) and the lit state rides
// on CSS classes (kp-off ghosted / kp-beat lit key / kp-target pin-on-accent),
// mirroring the old pin toggle's class-driven look. Gated on the scene having a
// colour: a beat needs an identity (its swatch), so the cycle is disabled until
// a colour is set — the tooltip says so.
void Inspector::sync_kp_cycle(const BinderNode* node) {
  const bool is_scene  = node && node->kind == BinderKind::Scene;
  const bool has_colour = node && node->color_idx > 0;
  const bool beat = node && node->is_key_point;
  const bool kp   = node && node->pin;   // pin implies a Key Point target

  m_kp_cycle.set_sensitive(is_scene && has_colour);
  m_kp_cycle.remove_css_class("kp-off");
  m_kp_cycle.remove_css_class("kp-beat");
  m_kp_cycle.remove_css_class("kp-target");

  if (kp) {
    m_kp_cycle.set_icon_name("folio-pin-symbolic");
    m_kp_cycle.add_css_class("kp-target");
    m_kp_cycle.set_tooltip_text(
        "Key Point — a target you write toward. Click to clear.");
  } else if (beat) {
    m_kp_cycle.set_icon_name("folio-key-symbolic");
    m_kp_cycle.add_css_class("kp-beat");
    m_kp_cycle.set_tooltip_text(
        "Pattern beat. Click to promote to a Key Point target.");
  } else {
    m_kp_cycle.set_icon_name("folio-key-symbolic");
    m_kp_cycle.add_css_class("kp-off");
    m_kp_cycle.set_tooltip_text(
        has_colour ? "Click to mark this scene a pattern beat."
                   : "Give the scene a colour first, then mark it a beat.");
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Metadata tab
// ─────────────────────────────────────────────────────────────────────────────

void Inspector::build_meta_tab() {
  // Wrapper: scroll on top (vexpand), progress footer pinned at bottom
  m_meta_wrapper.set_orientation(Gtk::Orientation::VERTICAL);
  m_meta_wrapper.set_vexpand(true);

  m_meta_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  m_meta_scroll.set_vexpand(true);
  m_meta_box.set_margin_top(8);
  m_meta_box.set_margin_start(8);
  m_meta_box.set_margin_end(8);
  m_meta_box.set_margin_bottom(8);

  auto *ns = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
  ns->set_name("node-meta");
  build_node_meta_section(*ns);
  m_meta_box.append(*ns);

  auto *cs = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
  cs->set_name("char-meta");
  cs->set_visible(false);
  build_character_meta_section(*cs);
  m_meta_box.append(*cs);

  auto *ps = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
  ps->set_name("place-meta");
  ps->set_visible(false);
  build_place_meta_section(*ps);
  m_meta_box.append(*ps);

  auto *rs = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
  rs->set_name("ref-meta");
  rs->set_visible(false);
  build_reference_meta_section(*rs);
  m_meta_box.append(*rs);

  m_meta_scroll.set_child(m_meta_box);
  m_meta_wrapper.append(m_meta_scroll);

  // Progress footer — floats at the bottom of the pane, outside the scroll
  m_progress_footer.set_visible(
      false); // load_node shows it for scene/group/template
  m_meta_wrapper.append(m_progress_footer);

  m_stack.add(m_meta_wrapper, "meta");
}

void Inspector::show_meta_section(const std::string &name) {
  auto *child = m_meta_box.get_first_child();
  while (child) {
    child->set_visible(child->get_name() == Glib::ustring(name));
    child = child->get_next_sibling();
  }
}

void Inspector::build_node_meta_section(Gtk::Box &s) {
  // ── Identity ────────────────────────────────────────────────────────────
  s.append(*make_disclosure_hdr("Identity",
      m_meta_node_identity_revealer, m_meta_node_identity_arrow,
      m_prefs.inspector_meta_node_identity_expanded));
  {
    auto *lb = make_listbox();
    { // Title
      auto *row = Gtk::make_managed<Gtk::ListBoxRow>();
      auto *rb = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
      rb->set_margin_start(8);
      rb->set_margin_end(8);
      rb->set_margin_top(3);
      rb->set_margin_bottom(3);
      auto *l = Gtk::make_managed<Gtk::Label>("Title");
      l->add_css_class("pref-row-label");
      l->set_hexpand(true);
      l->set_halign(Gtk::Align::START);
      m_title_entry.set_placeholder_text("Title…");
      m_title_entry.set_size_request(130, -1);
      m_title_entry.set_halign(Gtk::Align::END);
      m_title_entry.signal_changed().connect([this]() {
        if (m_loading || !m_current_node)
          return;
        m_current_node->title = std::string(m_title_entry.get_text());
        m_model.mark_modified();
        notify_meta_changed();
      });
      rb->append(*l);
      rb->append(m_title_entry);
      row->set_child(*rb);
      lb->append(*row);
    }
    m_meta_node_identity_revealer.set_child(*lb);
    s.append(m_meta_node_identity_revealer);
  }

  // ── Synopsis ────────────────────────────────────────────────────────────
  s.append(*make_disclosure_hdr("Synopsis",
      m_meta_node_synopsis_revealer, m_meta_node_synopsis_arrow,
      m_prefs.inspector_meta_node_synopsis_expanded));
  {
    m_synopsis_view.set_buffer(m_synopsis_buffer);
    m_synopsis_view.set_wrap_mode(Gtk::WrapMode::WORD_CHAR);
    m_synopsis_view.add_css_class("synopsis-view");
    m_synopsis_view.set_top_margin(8);
    m_synopsis_view.set_bottom_margin(8);
    m_synopsis_view.set_left_margin(8);
    m_synopsis_view.set_right_margin(8);
    auto *sc = Gtk::make_managed<Gtk::ScrolledWindow>();
    sc->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    sc->set_size_request(-1, 100);
    sc->set_child(m_synopsis_view);
    sc->add_css_class("synopsis-view");
    m_synopsis_buffer->signal_changed().connect([this]() {
      if (m_loading || !m_current_node)
        return;
      m_current_node->synopsis = std::string(m_synopsis_buffer->get_text(
          m_synopsis_buffer->begin(), m_synopsis_buffer->end(), false));
      m_model.mark_modified();
    });
    m_meta_node_synopsis_revealer.set_child(*sc);
    s.append(m_meta_node_synopsis_revealer);
  }

  // ── Status ──────────────────────────────────────────────────────────────
  s.append(*make_disclosure_hdr("Status",
      m_meta_node_status_revealer, m_meta_node_status_arrow,
      m_prefs.inspector_meta_node_status_expanded));
  {
    auto *lb = make_listbox();
    auto *row = Gtk::make_managed<Gtk::ListBoxRow>();
    auto *rb = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    rb->set_margin_start(8);
    rb->set_margin_end(8);
    rb->set_margin_top(3);
    rb->set_margin_bottom(3);
    auto *l = Gtk::make_managed<Gtk::Label>("Status");
    l->add_css_class("pref-row-label");
    l->set_hexpand(true);
    l->set_halign(Gtk::Align::START);
    m_status_dropdown =
        Gtk::make_managed<Gtk::DropDown>(Gtk::StringList::create({}));
    rebuild_status_dropdown();
    m_status_dropdown->property_selected().signal_changed().connect([this]() {
      if (m_loading || !m_current_node)
        return;
      guint idx = m_status_dropdown->get_selected();
      if (idx == 0) {
        m_current_node->status = NodeStatus::Untitled;
        m_model.mark_modified();
        notify_meta_changed();
        return;
      }
      const auto &statuses =
          m_prefs.statuses.empty()
              ? std::vector<StatusDef>{{"Rough Draft", "#f9e2af"},
                                       {"In Progress", "#89b4fa"},
                                       {"Polished", "#a6e3a1"},
                                       {"Skip", "#6c7086"}}
              : m_prefs.statuses;
      if (idx - 1 < statuses.size()) {
        const std::string &name = statuses[idx - 1].name;
        NodeStatus s = NodeStatus::Untitled;
        if (name == "Rough Draft")
          s = NodeStatus::RoughDraft;
        else if (name == "In Progress")
          s = NodeStatus::InProgress;
        else if (name == "Polished")
          s = NodeStatus::Polished;
        else if (name == "Skip")
          s = NodeStatus::Skip;
        m_current_node->status = s;
        m_model.mark_modified();
        notify_meta_changed();
      }
    });
    rb->append(*l);
    rb->append(*m_status_dropdown);
    row->set_child(*rb);
    lb->append(*row);
    m_meta_node_status_revealer.set_child(*lb);
    s.append(m_meta_node_status_revealer);
  }

  // ── Label ───────────────────────────────────────────────────────────────
  s.append(*make_disclosure_hdr("Label",
      m_meta_node_label_revealer, m_meta_node_label_arrow,
      m_prefs.inspector_meta_node_label_expanded));
  {
    auto *lb = make_listbox();
    auto *row = Gtk::make_managed<Gtk::ListBoxRow>();
    auto *rb = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    rb->set_margin_start(8);
    rb->set_margin_end(8);
    rb->set_margin_top(3);
    rb->set_margin_bottom(3);
    auto *l = Gtk::make_managed<Gtk::Label>("Color");
    l->add_css_class("pref-row-label");
    l->set_hexpand(true);
    l->set_halign(Gtk::Align::START);
    m_color_dropdown = build_color_dropdown([this](int idx) {
      if (!m_current_node) return;
      m_current_node->color_idx = idx;
      // The palette IS the arc: a swatch is a named Key Point, so re-colouring a
      // scene re-tags it. Sync the tag label off the swatch name (idx 0 = None →
      // untagged) so tag and colour can't drift; anchor the beat's identity to
      // the swatch's STABLE id (s81) so it survives a palette rename / reorder.
      m_current_node->kp_label = m_prefs.color_name_for_idx(idx);
      m_current_node->kp_id =
          (idx >= 1 && idx <= (int)m_prefs.tag_colors.size())
              ? m_prefs.tag_colors[idx - 1].id
              : std::string();
      if (idx == 0) {   // colour removed → no identity → cannot be a beat
        m_current_node->is_key_point = false;
        m_current_node->pin = false;
      }
      m_tag_value.set_text(m_current_node->kp_label.empty()
                               ? "\u2014" : m_current_node->kp_label);
      sync_kp_cycle(m_current_node);   // colour gained/cleared → cycle enables/disables
      m_model.mark_modified();
      notify_meta_changed();   // rebuild the binder so the swatch updates
    });
    rb->append(*l);
    rb->append(*m_color_dropdown);
    row->set_child(*rb);
    lb->append(*row);
    // s23: Tag row — the scene's Key Point (set via the colour swatch above; the
    // palette IS the arc). s81: the three-way key cycle sits inline — one click
    // makes the tagged scene a pattern beat, a second promotes it to a Key Point
    // target, a third clears it (off → key → pin → off), right where you set its
    // metadata. Disabled until the scene has a colour (a beat needs an identity).
    {
      auto *trow = Gtk::make_managed<Gtk::ListBoxRow>();
      auto *tb = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
      tb->set_margin_start(8);
      tb->set_margin_end(8);
      tb->set_margin_top(3);
      tb->set_margin_bottom(3);
      auto *tl = Gtk::make_managed<Gtk::Label>("Tag");
      tl->add_css_class("pref-row-label");
      tl->set_hexpand(true);
      tl->set_halign(Gtk::Align::START);
      m_tag_value.set_halign(Gtk::Align::END);
      m_tag_value.add_css_class("dim-label");
      m_kp_cycle.add_css_class("kp-cycle");     // own class — three lit states via CSS
      m_kp_cycle.set_valign(Gtk::Align::CENTER);
      m_kp_cycle.signal_clicked().connect([this]() {
        if (m_loading || !m_current_node) return;
        BinderNode* n = m_current_node;
        // Cycle off → key → pin → off. pin always implies is_key_point.
        if (!n->is_key_point)      { n->is_key_point = true;  n->pin = false; }
        else if (!n->pin)          { n->pin = true; }
        else                       { n->is_key_point = false; n->pin = false; }
        // Lighting a beat anchors its identity to its swatch (kp_id = swatch id),
        // so the lane can group it; the colour was required to enable the cycle.
        if (n->is_key_point && n->color_idx >= 1 &&
            n->color_idx <= (int)m_prefs.tag_colors.size()) {
          n->kp_id = m_prefs.tag_colors[n->color_idx - 1].id;
          n->kp_label = m_prefs.color_name_for_idx(n->color_idx);
        }
        sync_kp_cycle(n);
        m_model.mark_modified();
        notify_meta_changed();   // rebuild the binder + lens so the marker updates
      });
      tb->append(*tl);
      tb->append(m_tag_value);
      tb->append(m_kp_cycle);
      trow->set_child(*tb);
      lb->append(*trow);
    }
    // s84 — Thread row: the scene's assigned story THREAD (the "assigned arc",
    // §9.12). A dropdown (None + each registered thread) assigns BinderNode.thread;
    // an inline entry + "+" mints a new thread (auto-coloured from the palette so
    // threads read distinctly) and assigns it. Inspector-first assign path; the
    // rail-arm/sweep batch path follows. The timeline thread band is the relief.
    {
      auto *trow = Gtk::make_managed<Gtk::ListBoxRow>();
      auto *tb = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
      tb->set_margin_start(8);
      tb->set_margin_end(8);
      tb->set_margin_top(3);
      tb->set_margin_bottom(3);
      auto *tl = Gtk::make_managed<Gtk::Label>("Thread");
      tl->add_css_class("pref-row-label");
      tl->set_halign(Gtk::Align::START);

      m_thread_dropdown =
          Gtk::make_managed<Gtk::DropDown>(Gtk::StringList::create({"None"}));
      m_thread_dropdown->set_hexpand(true);
      m_thread_dropdown->property_selected().signal_changed().connect([this]() {
        if (m_loading || !m_current_node) return;
        guint idx = m_thread_dropdown->get_selected();
        if (idx == 0) {
          m_current_node->thread.clear();
        } else {
          const auto& th = m_model.threads();
          if (idx - 1 < th.size()) m_current_node->thread = th[idx - 1].iid;
        }
        m_model.mark_modified();
        notify_meta_changed();   // refresh the binder + the timeline thread band
      });

      m_thread_new_entry = Gtk::make_managed<Gtk::Entry>();
      m_thread_new_entry->set_placeholder_text("New thread…");
      m_thread_new_entry->set_max_width_chars(12);
      auto *add_btn = Gtk::make_managed<Gtk::Button>();
      add_btn->set_icon_name("list-add-symbolic");
      add_btn->set_tooltip_text("Create a thread and assign this scene to it");
      add_btn->add_css_class("flat");

      auto add_thread = [this]() {
        if (m_loading || !m_current_node) return;
        std::string name = m_thread_new_entry->get_text().raw();
        // trim surrounding whitespace
        const auto b = name.find_first_not_of(" \t\n\r");
        const auto e = name.find_last_not_of(" \t\n\r");
        name = (b == std::string::npos) ? std::string()
                                        : name.substr(b, e - b + 1);
        if (name.empty()) return;
        // auto-colour: cycle the palette so each new thread reads distinctly.
        int cidx = 0;
        if (!m_prefs.tag_colors.empty())
          cidx = static_cast<int>(m_model.threads().size()
                                  % m_prefs.tag_colors.size()) + 1;
        ThreadDef& td = m_model.add_thread(name, cidx);
        m_current_node->thread = td.iid;
        m_thread_new_entry->set_text("");
        m_model.mark_modified();
        // resync to include + select the new thread (guarded so the dropdown's
        // own signal does not re-fire during the rebuild).
        const bool was = m_loading;
        m_loading = true;
        sync_thread_dropdown(m_current_node);
        m_loading = was;
        notify_meta_changed();
      };
      add_btn->signal_clicked().connect(add_thread);
      m_thread_new_entry->signal_activate().connect(add_thread);

      tb->append(*tl);
      tb->append(*m_thread_dropdown);
      tb->append(*m_thread_new_entry);
      tb->append(*add_btn);
      trow->set_child(*tb);
      lb->append(*trow);
    }
    // s30 — per-scene energies, editable. Pacing (frenetic) + Tension (arc) as
    // 0..100% sliders that write straight to the node — the scaffold's energies
    // are clay you retune where you write, not a fixed stamp.
    {
      auto energy_row = [&](const char* name,
                            Glib::RefPtr<Gtk::Adjustment>& adj,
                            std::function<void(double)> setter) {
        auto *r  = Gtk::make_managed<Gtk::ListBoxRow>();
        auto *bx = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        bx->set_margin_start(8); bx->set_margin_end(8);
        bx->set_margin_top(3);    bx->set_margin_bottom(3);
        auto *nl = Gtk::make_managed<Gtk::Label>(name);
        nl->add_css_class("pref-row-label");
        nl->set_halign(Gtk::Align::START);
        adj = Gtk::Adjustment::create(0, 0, 100, 1, 10, 0);
        auto *sc = Gtk::make_managed<Gtk::Scale>(adj, Gtk::Orientation::HORIZONTAL);
        sc->set_draw_value(true);
        sc->set_value_pos(Gtk::PositionType::RIGHT);
        sc->set_digits(0);
        sc->set_hexpand(true);
        sc->set_size_request(150, -1);
        adj->signal_value_changed().connect([this, adj, setter]{
          if (m_loading || !m_current_node) return;
          setter(adj->get_value() / 100.0);
          m_model.mark_modified();
        });
        bx->append(*nl);
        bx->append(*sc);
        r->set_child(*bx);
        lb->append(*r);
      };
      energy_row("Pacing", m_pacing_adj,
                 [this](double v) { m_current_node->frenetic = v; });
      energy_row("Tension", m_tension_adj,
                 [this](double v) { m_current_node->arc = v; });
    }
    m_meta_node_label_revealer.set_child(*lb);
    s.append(m_meta_node_label_revealer);
  }

  // ── Scene Settings ──────────────────────────────────────────────────────
  s.append(*make_disclosure_hdr("Scene Settings",
      m_meta_node_scene_revealer, m_meta_node_scene_arrow,
      m_prefs.inspector_meta_node_scene_expanded));
  {
    auto *lb = make_listbox();
    { // POV
      auto *row = Gtk::make_managed<Gtk::ListBoxRow>();
      auto *rb = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
      rb->set_margin_start(8);
      rb->set_margin_end(8);
      rb->set_margin_top(3);
      rb->set_margin_bottom(3);
      auto *tb = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
      auto *rl = Gtk::make_managed<Gtk::Label>("POV Character");
      rl->add_css_class("pref-row-label");
      rl->set_halign(Gtk::Align::START);
      auto *rs = Gtk::make_managed<Gtk::Label>("Narrative perspective");
      rs->add_css_class("pref-row-sub");
      rs->set_halign(Gtk::Align::START);
      tb->append(*rl);
      tb->append(*rs);
      tb->set_hexpand(true);
      m_pov_dropdown =
          Gtk::make_managed<Gtk::DropDown>(Gtk::StringList::create({"—"}));
      m_pov_dropdown->set_selected(0);
      m_pov_dropdown->property_selected().signal_changed().connect([this]() {
        if (m_loading || !m_current_node)
          return;
        guint sel = m_pov_dropdown->get_selected();
        if (sel == 0) {
          m_current_node->pov_character_name.clear();
          return;
        }
        auto *sl =
            dynamic_cast<Gtk::StringList *>(m_pov_dropdown->get_model().get());
        if (sl && sel < sl->get_n_items())
          m_current_node->pov_character_name = std::string(sl->get_string(sel));
        m_model.mark_modified();
      });
      rb->append(*tb);
      rb->append(*m_pov_dropdown);
      row->set_child(*rb);
      lb->append(*row);
    }
    { // Include in export
      auto *row = Gtk::make_managed<Gtk::ListBoxRow>();
      auto *rb = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
      rb->set_margin_start(8);
      rb->set_margin_end(8);
      rb->set_margin_top(3);
      rb->set_margin_bottom(3);
      auto *l = Gtk::make_managed<Gtk::Label>("Include in Export");
      l->add_css_class("pref-row-label");
      l->set_hexpand(true);
      l->set_halign(Gtk::Align::START);
      m_include_switch.set_active(true);
      m_include_switch.property_active().signal_changed().connect([this]() {
        if (m_loading || !m_current_node)
          return;
        m_current_node->include_in_export = m_include_switch.get_active();
        m_model.mark_modified();
      });
      rb->append(*l);
      rb->append(m_include_switch);
      row->set_child(*rb);
      lb->append(*row);
    }
    { // Word target
      auto *row = Gtk::make_managed<Gtk::ListBoxRow>();
      auto *rb = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
      rb->set_margin_start(8);
      rb->set_margin_end(8);
      rb->set_margin_top(3);
      rb->set_margin_bottom(3);
      auto *l = Gtk::make_managed<Gtk::Label>("Word Target");
      l->add_css_class("pref-row-label");
      l->set_hexpand(true);
      l->set_halign(Gtk::Align::START);
      m_word_target_spin.set_adjustment(
          Gtk::Adjustment::create(0, 0, 100000, 100, 1000));
      m_word_target_spin.set_digits(0);
      m_word_target_spin.set_numeric(true);
      m_word_target_spin.set_size_request(80, -1);
      m_word_target_spin.set_halign(Gtk::Align::END);
      m_word_target_spin.signal_value_changed().connect([this]() {
        if (m_loading || !m_current_node)
          return;
        m_current_node->word_target = (int)m_word_target_spin.get_value();
        m_model.mark_modified();
        refresh_scene_progress();
        notify_meta_changed();
      });
      rb->append(*l);
      rb->append(m_word_target_spin);
      row->set_child(*rb);
      lb->append(*row);
    }
    m_meta_node_scene_revealer.set_child(*lb);
    s.append(m_meta_node_scene_revealer);
  }
}

// s41 — populate_object_form() moved to the Editor (the inversion). The Editor
// now resolves object + template from the store and renders the form as its
// document, running the same floor->leaf / custom->store write-through.

// s44 §11 — open_template_builder_for_current() (the instance-side "Edit fields…"
// clone-to-customize path) is RETIRED. Schema editing lives only on the Template
// node (open_template_builder_for_template_node); a Character is born on a Template
// and reshaped only by editing that Template. No-mutate.

// s38 — author a Template binder node's form. Unlike the character-driven path
// above, there is no Object here: the node carries the schema directly in
// form_schema. Seed the draft from it (or the Character floor when the node is
// freshly added and empty), pin the draft id to the node iid (identity from the
// node, § unified-model §4), and on commit write the schema BACK to the node and
// re-project the registry — all on an idle tick, never inside the live modal
// (the s24 rule).
void Inspector::open_template_builder_for_template_node(const std::string& node_iid) {
  if (node_iid.empty()) return;
  BinderNode* node = m_model.find_node_by_iid(node_iid);
  if (!node || node->kind != BinderKind::Template) return;

  Folio::Template draft;
  if (node->form_schema.is_object() && !node->form_schema.empty())
    draft = Folio::ObjectIO::template_from_json(node->form_schema);
  else
    draft = Folio::built_in_character_template();   // floor fallback for a new node
  draft.id      = node_iid;   // identity from the node
  draft.builtin = false;      // node-backed templates are editable

  auto* root = dynamic_cast<Gtk::Window*>(get_root());
  if (!root) return;

  m_template_builder = std::make_unique<TemplateBuilderDialog>(*root);
  m_template_builder->set_type_provider([this]() {
    std::vector<Folio::FieldChoice> out;
    for (const auto& t : m_model.object_store().templates)
      out.push_back({ t.id, t.type_name });
    return out;
  });
  m_template_builder->set_apply_callback([this, node_iid](const Folio::Template& edited) {
    Folio::Template t = edited;   // capture by value — survives the dialog teardown
    t.id      = node_iid;         // keep identity pinned to the node
    t.builtin = false;
    Glib::signal_idle().connect_once([this, node_iid, t]() {
      BinderNode* n = m_model.find_node_by_iid(node_iid);   // re-resolve (no held ptr)
      if (!n) return;
      n->form_schema = Folio::ObjectIO::template_to_json(t);
      if (!t.type_name.empty()) n->title = t.type_name;     // the binder shows the form's name
      // s44 §11 — at most one explicit default per category: if this Template was
      // marked default, clear the flag on its category siblings.
      if (t.is_default)
        m_model.clear_default_template_for_category(t.category, node_iid);
      m_model.mark_modified();
      m_model.rebuild_object_store();   // re-project so the registry reflects the edit
      notify_meta_changed();            // refresh the binder/sidebar
    });
  });
  m_template_builder->signal_hide().connect([this]() {
    Glib::signal_idle().connect_once([this]() { m_template_builder.reset(); });
  });
  m_template_builder->open_for(draft);
  m_template_builder->present();
}

void Inspector::build_character_meta_section(Gtk::Box &s) {
  // ── Identity ────────────────────────────────────────────────────────────
  s.append(*make_disclosure_hdr("Identity",
      m_meta_char_identity_revealer, m_meta_char_identity_arrow,
      m_prefs.inspector_meta_char_identity_expanded));
  {
    auto *lb = make_listbox();
    // Name retired (s32) — the object form now owns name -> title editing, so a
    // second Name entry here would double-bind. Identity keeps the non-floor Role.
    { // Role
      auto *row = Gtk::make_managed<Gtk::ListBoxRow>();
      auto *rb = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
      rb->set_margin_start(8);
      rb->set_margin_end(8);
      rb->set_margin_top(3);
      rb->set_margin_bottom(3);
      auto *l = Gtk::make_managed<Gtk::Label>("Role");
      l->add_css_class("pref-row-label");
      l->set_hexpand(true);
      l->set_halign(Gtk::Align::START);
      auto role_model = Gtk::StringList::create({"—"});
      for (auto &r : m_prefs.character_roles)
        role_model->append(r);
      m_char_role_dropdown = Gtk::make_managed<Gtk::DropDown>(role_model);
      m_char_role_dropdown->property_selected().signal_changed().connect(
          [this]() {
            if (m_loading || !m_current_node)
              return;
            guint idx = m_char_role_dropdown->get_selected();
            auto *sl = dynamic_cast<Gtk::StringList *>(
                m_char_role_dropdown->get_model().get());
            if (idx == 0)
              m_current_node->role = "";
            else if (sl && idx < sl->get_n_items())
              m_current_node->role = std::string(sl->get_string(idx));
            m_model.mark_modified();
          });
      rb->append(*l);
      rb->append(*m_char_role_dropdown);
      row->set_child(*rb);
      lb->append(*row);
    }
    m_meta_char_identity_revealer.set_child(*lb);
    s.append(m_meta_char_identity_revealer);
  }

  // ── Tagline ─────────────────────────────────────────────────────────────
  s.append(*make_disclosure_hdr("Tagline",
      m_meta_char_tagline_revealer, m_meta_char_tagline_arrow,
      m_prefs.inspector_meta_char_tagline_expanded));
  {
    auto *card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    card->add_css_class("pomo-tile-card");
    m_char_desc_entry.set_placeholder_text("e.g. Protagonist · Analyst");
    m_char_desc_entry.set_margin_top(4);
    m_char_desc_entry.set_margin_bottom(4);
    m_char_desc_entry.signal_changed().connect([this]() {
      if (m_loading || !m_current_node)
        return;
      m_current_node->description = std::string(m_char_desc_entry.get_text());
      m_model.mark_modified();
    });
    card->append(m_char_desc_entry);
    m_meta_char_tagline_revealer.set_child(*card);
    s.append(m_meta_char_tagline_revealer);
  }

  // ── Colour ──────────────────────────────────────────────────────────────
  s.append(*make_disclosure_hdr("Colour",
      m_meta_char_colour_revealer, m_meta_char_colour_arrow,
      m_prefs.inspector_meta_char_colour_expanded));
  {
    auto *lb = make_listbox();
    auto *row = Gtk::make_managed<Gtk::ListBoxRow>();
    auto *rb = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    rb->set_margin_start(8);
    rb->set_margin_end(8);
    rb->set_margin_top(3);
    rb->set_margin_bottom(3);
    auto *l = Gtk::make_managed<Gtk::Label>("Color");
    l->add_css_class("pref-row-label");
    l->set_hexpand(true);
    l->set_halign(Gtk::Align::START);
    m_char_color_dropdown = build_color_dropdown([this](int idx) {
      if (m_current_node) {
        m_current_node->color_idx = idx;
        m_model.mark_modified();
      }
    });
    rb->append(*l);
    rb->append(*m_char_color_dropdown);
    row->set_child(*rb);
    lb->append(*row);
    m_meta_char_colour_revealer.set_child(*lb);
    s.append(m_meta_char_colour_revealer);
  }

  // s41 — the editable object form moved to the Editor (the inversion). This
  // panel is now chrome: status / colour / snapshots / notes live elsewhere in
  // the inspector; the form is the Editor document.
}

void Inspector::build_place_meta_section(Gtk::Box &s) {
  // Identity (Name) retired (s32) — the object form now owns name -> title, so a
  // second Name entry here would double-bind. The place panel keeps only its
  // non-floor fields (Tagline, Colour); the form supplies name / image / buffer.

  // ── Tagline ─────────────────────────────────────────────────────────────
  // (Was "Description": relabelled to kill the same-label/different-meaning clash
  // with the object form's "Description" buffer. This edits the one-liner
  // node->description; the form's Description edits the long-form node->content.)
  s.append(*make_disclosure_hdr("Tagline",
      m_meta_place_description_revealer, m_meta_place_description_arrow,
      m_prefs.inspector_meta_place_description_expanded));
  {
    auto *card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    card->add_css_class("pomo-tile-card");
    m_place_desc_entry.set_placeholder_text("e.g. Research complex · Remote");
    m_place_desc_entry.set_margin_top(4);
    m_place_desc_entry.set_margin_bottom(4);
    m_place_desc_entry.signal_changed().connect([this]() {
      if (m_loading || !m_current_node)
        return;
      m_current_node->description = std::string(m_place_desc_entry.get_text());
      m_model.mark_modified();
    });
    card->append(m_place_desc_entry);
    m_meta_place_description_revealer.set_child(*card);
    s.append(m_meta_place_description_revealer);
  }

  // ── Colour ──────────────────────────────────────────────────────────────
  s.append(*make_disclosure_hdr("Colour",
      m_meta_place_colour_revealer, m_meta_place_colour_arrow,
      m_prefs.inspector_meta_place_colour_expanded));
  {
    auto *lb = make_listbox();
    auto *row = Gtk::make_managed<Gtk::ListBoxRow>();
    auto *rb = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    rb->set_margin_start(8);
    rb->set_margin_end(8);
    rb->set_margin_top(3);
    rb->set_margin_bottom(3);
    auto *l = Gtk::make_managed<Gtk::Label>("Color");
    l->add_css_class("pref-row-label");
    l->set_hexpand(true);
    l->set_halign(Gtk::Align::START);
    m_place_color_dropdown = build_color_dropdown([this](int idx) {
      if (m_current_node) {
        m_current_node->color_idx = idx;
        m_model.mark_modified();
      }
    });
    rb->append(*l);
    rb->append(*m_place_color_dropdown);
    row->set_child(*rb);
    lb->append(*row);
    m_meta_place_colour_revealer.set_child(*lb);
    s.append(m_meta_place_colour_revealer);
  }

  // s41 — the editable object form moved to the Editor (the inversion). Chrome
  // only here now.
}

void Inspector::build_reference_meta_section(Gtk::Box &s) {
  // ── Reference ───────────────────────────────────────────────────────────
  s.append(*make_disclosure_hdr("Reference",
      m_meta_ref_reference_revealer, m_meta_ref_reference_arrow,
      m_prefs.inspector_meta_ref_reference_expanded));
  {
    auto *lb_id = make_listbox();
    // s42 — Title moved into the Editor form (name -> title). This chrome keeps
    // the Reference's orphan field (URL, with its open-in-browser affordance);
    // Notes follow below. Same pattern as Character/Place after the inversion.
    // URL row
    {
      auto *row = Gtk::make_managed<Gtk::ListBoxRow>();
      auto *rb = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
      rb->set_margin_start(8);
      rb->set_margin_end(8);
      rb->set_margin_top(3);
      rb->set_margin_bottom(3);
      auto *l = Gtk::make_managed<Gtk::Label>("URL");
      l->add_css_class("pref-row-label");
      l->set_hexpand(true);
      l->set_halign(Gtk::Align::START);
      m_ref_url_entry.set_placeholder_text("https://");
      m_ref_url_entry.set_size_request(140, -1);
      m_ref_url_entry.set_halign(Gtk::Align::END);
      m_ref_url_entry.signal_changed().connect([this]() {
        if (m_loading || !m_current_node)
          return;
        m_current_node->url = std::string(m_ref_url_entry.get_text());
        m_model.mark_modified();
        notify_meta_changed();
      });
      auto *open_btn = Gtk::make_managed<Gtk::Button>();
      open_btn->set_icon_name("web-browser-symbolic");
      open_btn->add_css_class("flat");
      open_btn->set_tooltip_text("Open in browser");
      open_btn->set_valign(Gtk::Align::CENTER);
      open_btn->signal_clicked().connect([this]() {
        if (!m_current_node || m_current_node->url.empty())
          return;
        try {
          Gio::AppInfo::launch_default_for_uri(m_current_node->url);
        } catch (...) {}
      });
      rb->append(*l);
      rb->append(m_ref_url_entry);
      rb->append(*open_btn);
      row->set_child(*rb);
      lb_id->append(*row);
    }
    m_meta_ref_reference_revealer.set_child(*lb_id);
    s.append(m_meta_ref_reference_revealer);
  }

  // ── Notes ───────────────────────────────────────────────────────────────
  s.append(*make_disclosure_hdr("Notes",
      m_meta_ref_notes_revealer, m_meta_ref_notes_arrow,
      m_prefs.inspector_meta_ref_notes_expanded));
  {
    auto *card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    card->add_css_class("pomo-tile-card");
    m_ref_notes_buffer = Gtk::TextBuffer::create();
    m_ref_notes_view.set_buffer(m_ref_notes_buffer);
    m_ref_notes_view.set_wrap_mode(Gtk::WrapMode::WORD);
    m_ref_notes_view.add_css_class("inspector-notes-view");
    m_ref_notes_view.set_size_request(-1, 100);
    m_ref_notes_buffer->signal_changed().connect([this]() {
      if (m_loading || !m_current_node)
        return;
      m_current_node->synopsis = m_ref_notes_buffer->get_text();
      m_model.mark_modified();
    });
    card->append(m_ref_notes_view);
    m_meta_ref_notes_revealer.set_child(*card);
    s.append(m_meta_ref_notes_revealer);
  }
}

void Inspector::refresh_pov_dropdown() {
  if (!m_pov_dropdown || !m_current_node)
    return;
  auto sl = Gtk::StringList::create({"—"});
  // Walk the characters tree, collecting leaf (Character kind) titles
  std::function<void(const std::vector<BinderNode> &)> collect =
      [&](const std::vector<BinderNode> &nodes) {
        for (const auto &n : nodes) {
          if (n.kind == BinderKind::Character)
            sl->append(n.title);
          else if (n.kind == BinderKind::Group)
            collect(n.children);
        }
      };
  collect(m_model.root(Section::Characters));
  m_pov_dropdown->set_model(sl);
  guint idx = 0;
  if (!m_current_node->pov_character_name.empty())
    for (guint i = 1; i < sl->get_n_items(); ++i)
      if (sl->get_string(i) ==
          Glib::ustring(m_current_node->pov_character_name)) {
        idx = i;
        break;
      }
  m_pov_dropdown->set_selected(idx);
}

void Inspector::refresh_scene_progress() {
  if (!m_current_node) {
    m_target_bar.set_value(0);
    m_target_progress_lbl.set_text("");
    return;
  }
  int wc = m_current_node->word_count();
  int tgt = std::max(1, m_current_node->word_target);
  m_target_bar.set_value(std::min(1.0, (double)wc / tgt));
  m_target_progress_lbl.set_text(std::to_string(wc) + " / " +
                                 std::to_string(m_current_node->word_target) +
                                 " words");
}

// ─────────────────────────────────────────────────────────────────────────────
// Public load methods
// ─────────────────────────────────────────────────────────────────────────────

void Inspector::focus_meta_tab() {
  m_tab_meta.set_active(true);
  show_tab(1);
}

void Inspector::navigate_to_tab(int idx) {
  switch (idx) {
  case 0:
    m_tab_project.set_active(true);
    show_tab(0);
    break;
  case 1:
    m_tab_meta.set_active(true);
    show_tab(1);
    break;
  case 2:
    m_tab_notes.set_active(true);
    show_tab(2);
    break;
  case 3:
    m_tab_history.set_active(true);
    show_tab(3);
    break;
  case 4:
    m_tab_annotations.set_active(true);
    show_tab(4);
    break;
  default:
    break;
  }
}

void Inspector::notify_annotations_changed() {
  if (m_tab_notes.get_active())
    refresh_annotations();
  if (m_ann_report)
    m_ann_report->refresh();
}

bool Inspector::progress_expanded() const { return m_progress_expanded; }

void Inspector::set_meta_changed_callback(MetaChangedCallback cb) {
  m_on_meta_changed = std::move(cb);
}
void Inspector::set_content_changed_callback(ContentChangedCallback cb) {
  m_on_content_changed = std::move(cb);
}
void Inspector::set_progress_disclosure_callback(std::function<void(bool)> cb) {
  m_on_progress_disclosure_changed = std::move(cb);
}
void Inspector::set_toast_callback(ToastCallback cb) {
  m_on_toast = std::move(cb);
}

void Inspector::set_progress_expanded(bool expanded) {
  m_progress_expanded = expanded;
  m_progress_revealer.set_reveal_child(expanded);
  m_progress_arrow.set_text(expanded ? "▾" : "▸");
}

void Inspector::load_node(BinderNode *node) {
  m_in_jv_mode = false;
  m_jv_nodes.clear();
  m_loading = true;
  m_current_node = node;

  // s19: track which node the inspector is bound to (inspector ↔ iid ↔ log).
  set_name(Folio::widget_name("inspector", node ? node->iid : std::string()));
  if (node)
    LOG_DEBUG("Inspector load {} ({})", node->iid, node->title);

  if (!node) {
    m_mode = InspectorMode::Empty;
    show_meta_section("node-meta");
    m_tab_history.set_sensitive(false);
    if (m_tab_history.get_active() || m_tab_project.get_active())
      m_tab_meta.set_active(true);
    m_title_entry.set_text("");
    m_synopsis_buffer->set_text("");
    m_target_bar.set_value(0);
    m_target_progress_lbl.set_text("");

    m_loading = false;
    refresh_notes();
    refresh_history();
    m_progress_footer.set_visible(false);
    return;
  }

  if (node->kind == BinderKind::Character) {
    m_mode = InspectorMode::Character;
    show_meta_section("char-meta");
    m_char_desc_entry.set_text(node->description);
    m_char_notes_buffer->set_text(node->synopsis);
    if (m_char_role_dropdown) {
      guint ridx = 0;
      auto *sl = dynamic_cast<Gtk::StringList *>(
          m_char_role_dropdown->get_model().get());
      if (sl && !node->role.empty())
        for (guint i = 1; i < sl->get_n_items(); ++i)
          if (std::string(sl->get_string(i)) == node->role) {
            ridx = i;
            break;
          }
      m_char_role_dropdown->set_selected(ridx);
    }
    sync_color_dropdown(m_char_color_dropdown, node->color_idx);
    m_tab_history.set_sensitive(true);

    m_loading = false;
    refresh_notes();
    refresh_history();
    m_progress_footer.set_visible(false);
    return;
  }

  if (node->kind == BinderKind::Place) {
    m_mode = InspectorMode::Place;
    show_meta_section("place-meta");
    m_place_desc_entry.set_text(node->description);
    m_place_notes_buffer->set_text(node->synopsis);
    sync_color_dropdown(m_place_color_dropdown, node->color_idx);
    m_tab_history.set_sensitive(true);

    m_loading = false;
    refresh_notes();
    refresh_history();
    m_progress_footer.set_visible(false);
    return;
  }

  if (node->kind == BinderKind::Reference) {
    m_mode = InspectorMode::Reference;
    show_meta_section("ref-meta");
    // s42 — Title now lives in the Editor form (name -> title); chrome keeps URL + Notes.
    m_ref_url_entry.set_text(node->url);
    m_ref_notes_buffer->set_text(node->synopsis);
    m_tab_history.set_sensitive(true);

    m_loading = false;
    refresh_notes();
    refresh_history();
    m_progress_footer.set_visible(false);
    return;
  }

  if (node->kind == BinderKind::Template) {
    // Templates use the same node-meta view as scenes — fall through below
    // so title, synopsis, color, word-target etc. are all populated.
  } else if (node->kind != BinderKind::Scene &&
             node->kind != BinderKind::Group) {
    // Unknown future kind — show node-meta minimally
    m_mode = InspectorMode::Node;
    show_meta_section("node-meta");
    m_tab_history.set_sensitive(true);

    m_loading = false;
    refresh_notes();
    refresh_history();
    m_progress_footer.set_visible(false);
    return;
  }

  // Scene, Group, or Template — show progress footer
  m_progress_footer.set_visible(true);

  // Scene or Group
  m_mode = InspectorMode::Node;
  show_meta_section("node-meta");
  m_tab_history.set_sensitive(true);
  m_title_entry.set_text(node->title);
  m_synopsis_buffer->set_text(node->synopsis);
  if (m_status_dropdown) {
    // Find matching status name in the dropdown
    guint sidx = 0;
    auto *sl =
        dynamic_cast<Gtk::StringList *>(m_status_dropdown->get_model().get());
    if (sl) {
      std::string target;
      switch (node->status) {
      case NodeStatus::RoughDraft:
        target = "Rough Draft";
        break;
      case NodeStatus::InProgress:
        target = "In Progress";
        break;
      case NodeStatus::Polished:
        target = "Polished";
        break;
      case NodeStatus::Skip:
        target = "Skip";
        break;
      default:
        break;
      }
      if (!target.empty())
        for (guint i = 1; i < sl->get_n_items(); ++i)
          if (std::string(sl->get_string(i)) == target) {
            sidx = i;
            break;
          }
    }
    m_status_dropdown->set_selected(sidx);
  }
  sync_color_dropdown(m_color_dropdown, node->color_idx);
  m_tag_value.set_text(node->kp_label.empty() ? "\u2014" : node->kp_label);
  // s81 — prime the three-way Key-Point cycle from the node (off / key / pin),
  // gated on the scene having a colour. m_loading is true here, so this is a
  // pure visual sync with no data write.
  sync_kp_cycle(node);
  if (m_pacing_adj)  m_pacing_adj->set_value(std::lround(node->frenetic * 100.0));
  if (m_tension_adj) m_tension_adj->set_value(std::lround(node->arc * 100.0));
  m_word_target_spin.set_value(node->word_target);
  bool inc = node->include_in_export;
  m_include_switch.set_active(inc);
  bool is_scene = (node->kind == BinderKind::Scene);
  if (m_pov_dropdown)
    m_pov_dropdown->set_visible(is_scene);
  if (is_scene)
    refresh_pov_dropdown();
  refresh_scene_progress();
  m_loading = false;
  refresh_notes();
  refresh_history();
}

void Inspector::load_empty() { load_node(nullptr); }

void Inspector::load_joined_nodes(const std::vector<BinderNode *> &nodes) {
  m_in_jv_mode = true;
  m_jv_nodes = nodes;
  m_current_node = nullptr;
  m_loading = true;

  // Notes is the only meaningful tab in JV mode — always switch to it.
  // Metadata and Snapshots don't apply to a multi-node aggregate view.
  m_tab_history.set_sensitive(false);
  m_tab_notes.set_active(true);
  show_tab(2);
  m_progress_footer.set_visible(false);

  m_loading = false;
  refresh_notes();
  refresh_annotations();
}

void Inspector::load_project() {
  m_loading = true;
  m_mode = InspectorMode::Project;
  m_current_node = nullptr;
  m_tab_history.set_sensitive(false);
  m_tab_project.set_active(true);
  m_stack.set_visible_child(m_proj_wrapper);
  refresh_project_meta();
  refresh_notes();
  m_loading = false;
}

void Inspector::refresh_project_tab() {
  m_loading = true;
  refresh_project_meta();
  m_loading = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Notes tab
// ─────────────────────────────────────────────────────────────────────────────

std::vector<Note> *Inspector::active_notes() {
  if (m_current_node)
    return &m_current_node->notes;
  return nullptr;
}

void Inspector::refresh_notes() {
  m_note_drag_idx = -1;
  m_note_drop_idx = -1;
  m_note_drop_line = nullptr;

  // ── JV mode: aggregate notes from all segments ────────────────────────────
  if (m_in_jv_mode) {
    m_notes_ctx_label.set_text("Joined View — Notes");
    m_notes_add_row.set_visible(false); // no Add in JV mode

    while (auto *c = m_notes_list.get_first_child())
      m_notes_list.remove(*c);

    // Count total notes across all nodes
    int total = 0;
    for (auto *n : m_jv_nodes)
      if (n)
        total += (int)n->notes.size();

    if (total == 0) {
      auto *empty = Gtk::make_managed<Gtk::Label>(
          "No notes in this selection.");
      empty->add_css_class("board-placeholder");
      empty->set_justify(Gtk::Justification::CENTER);
      empty->set_margin_top(24);
      m_notes_list.append(*empty);
      return;
    }

    // Render each node's notes newest-first, with a node label header
    for (auto *node : m_jv_nodes) {
      if (!node || node->notes.empty())
        continue;

      // Node label
      auto *node_lbl = Gtk::make_managed<Gtk::Label>(node->title);
      node_lbl->add_css_class("pref-group-title");
      node_lbl->set_halign(Gtk::Align::START);
      node_lbl->set_margin_top(3);
      node_lbl->set_margin_start(4);
      node_lbl->set_margin_bottom(2);
      m_notes_list.append(*node_lbl);

      int n = (int)node->notes.size();
      for (int display_pos = 0; display_pos < n; ++display_pos) {
        int note_idx = n - 1 - display_pos;
        auto &note = node->notes[note_idx];

        auto *card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
        card->add_css_class("note-card");
        card->set_margin_bottom(2);

        auto *hdr = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);

        auto *date = Gtk::make_managed<Gtk::Label>(note.title);
        date->add_css_class("snap-date");
        date->set_halign(Gtk::Align::START);
        date->set_hexpand(true);

        // Delete button — captures node* and note_idx
        auto *del = Gtk::make_managed<Gtk::Button>("✕");
        del->add_css_class("note-delete-btn");
        del->set_tooltip_text("Delete this note");
        del->signal_clicked().connect([this, node, note_idx]() {
          if (note_idx >= (int)node->notes.size())
            return;
          node->notes.erase(node->notes.begin() + note_idx);
          m_model.mark_modified();
          refresh_notes();
        });

        hdr->append(*date);
        hdr->append(*del);

        auto *text = Gtk::make_managed<Gtk::Label>(note.body);
        text->set_wrap(true);
        text->set_xalign(0.0f);
        text->set_halign(Gtk::Align::START);
        text->add_css_class("note-text");
        card->append(*hdr);
        card->append(*text);
        m_notes_list.append(*card);
        // No DnD in JV mode — reorder within aggregated view is ambiguous
      }
    }
    return;
  }

  // ── Single-node mode ──────────────────────────────────────────────────────
  m_notes_add_row.set_visible(true);

  std::string ctx;
  if (m_current_node) {
    switch (m_current_node->kind) {
    case BinderKind::Character:
      ctx = "Character: " + m_current_node->title;
      break;
    case BinderKind::Place:
      ctx = "Place: " + m_current_node->title;
      break;
    case BinderKind::Group:
      ctx = "Group: " + m_current_node->title;
      break;
    default:
      ctx = "Scene: " + m_current_node->title;
      break;
    }
  } else {
    ctx = "Project Notes";
  }
  m_notes_ctx_label.set_text(ctx);

  while (auto *c = m_notes_list.get_first_child())
    m_notes_list.remove(*c);
  auto *notes = active_notes();
  if (!notes || notes->empty()) {
    auto *empty = Gtk::make_managed<Gtk::Label>(
        "No notes yet.\nUse the field below to add one.");
    empty->add_css_class("board-placeholder");
    empty->set_justify(Gtk::Justification::CENTER);
    empty->set_margin_top(24);
    m_notes_list.append(*empty);
    return;
  }
  // Cards are displayed newest-first: display_pos 0 = notes[size-1]
  int n = (int)notes->size();
  for (int display_pos = 0; display_pos < n; ++display_pos) {
    int note_idx = n - 1 - display_pos; // actual index in notes[]
    auto &note = (*notes)[note_idx];

    auto *card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    card->add_css_class("note-card");
    card->set_margin_bottom(2);

    auto *hdr = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);

    // Drag handle
    auto *handle = Gtk::make_managed<Gtk::Label>("⠿");
    handle->add_css_class("note-drag-handle");
    handle->set_valign(Gtk::Align::CENTER);
    handle->set_cursor(Gdk::Cursor::create("grab"));
    hdr->append(*handle);

    auto *date = Gtk::make_managed<Gtk::Label>(note.title);
    date->add_css_class("snap-date");
    date->set_halign(Gtk::Align::START);
    date->set_hexpand(true);
    auto *del = Gtk::make_managed<Gtk::Button>("✕");
    del->add_css_class("note-delete-btn");
    del->set_tooltip_text("Delete this note");
    del->signal_clicked().connect([this, note_idx]() {
      auto *nv = active_notes();
      if (!nv || note_idx >= (int)nv->size())
        return;
      nv->erase(nv->begin() + note_idx);
      m_model.mark_modified();
      refresh_notes();
    });
    hdr->append(*date);
    hdr->append(*del);

    auto *text = Gtk::make_managed<Gtk::Label>(note.body);
    text->set_wrap(true);
    text->set_xalign(0.0f);
    text->set_halign(Gtk::Align::START);
    text->add_css_class("note-text");
    card->append(*hdr);
    card->append(*text);
    m_notes_list.append(*card);

    setup_note_dnd(card, display_pos, note_idx);
  }
}

// ── Note drag-and-drop helpers
// ────────────────────────────────────────────────

void Inspector::clear_note_drop_line() {
  if (m_note_drop_line) {
    m_notes_list.remove(*m_note_drop_line);
    m_note_drop_line = nullptr;
  }
  m_note_drop_idx = -1;
}

void Inspector::apply_note_drop_line(int drop_display_pos) {
  if (m_note_drop_idx == drop_display_pos)
    return;
  clear_note_drop_line();
  m_note_drop_idx = drop_display_pos;

  // Insert a thin separator at position drop_display_pos in the list.
  // Each card occupies one child slot; we insert before the child at
  // drop_display_pos, or append if drop_display_pos == number of cards.
  auto *line = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
  line->add_css_class("note-drop-line");
  m_note_drop_line = line;

  // Find the child at drop_display_pos
  Gtk::Widget *before = nullptr;
  int pos = 0;
  for (auto *child = m_notes_list.get_first_child(); child;
       child = child->get_next_sibling()) {
    if (pos == drop_display_pos) {
      before = child;
      break;
    }
    ++pos;
  }

  if (before) {
    Gtk::Widget *prev = before->get_prev_sibling();
    if (prev)
      m_notes_list.insert_child_after(*line, *prev);
    else
      m_notes_list.prepend(*line);
  } else
    m_notes_list.append(*line);
}

void Inspector::setup_note_dnd(Gtk::Widget *card, int display_pos,
                               int note_idx) {
  // ── Drag source ───────────────────────────────────────────────────────────
  auto src = Gtk::DragSource::create();
  src->set_actions(Gdk::DragAction::MOVE);

  src->signal_prepare().connect(
      [this, note_idx, card](double,
                             double) -> Glib::RefPtr<Gdk::ContentProvider> {
        m_note_drag_idx = note_idx;
        card->add_css_class("note-dragging");
        Glib::Value<int> val;
        val.init(G_TYPE_INT);
        val.set(note_idx);
        return Gdk::ContentProvider::create(val);
      },
      false);

  src->signal_drag_end().connect(
      [this, card](const Glib::RefPtr<Gdk::Drag> &, bool) {
        card->remove_css_class("note-dragging");
        clear_note_drop_line();
        m_note_drag_idx = -1;
      },
      false);

  card->add_controller(src);

  // ── Drop target (the whole list acts as target; each card also accepts) ───
  auto tgt = Gtk::DropTarget::create(G_TYPE_INT, Gdk::DragAction::MOVE);

  tgt->signal_motion().connect(
      [this, display_pos, card](double /*x*/, double y) -> Gdk::DragAction {
        if (m_note_drag_idx < 0)
          return Gdk::DragAction{};
        // Top half → drop before this card; bottom half → drop after
        int h = card->get_height();
        int slot = (h > 0 && y < h * 0.5) ? display_pos : display_pos + 1;
        apply_note_drop_line(slot);
        return Gdk::DragAction::MOVE;
      },
      false);

  tgt->signal_leave().connect(
      []() {
        // Don't clear immediately — another card's motion will update it
      },
      false);

  tgt->signal_drop().connect(
      [this](const Glib::ValueBase & /*val*/, double, double) -> bool {
        auto *notes = active_notes();
        if (!notes || m_note_drag_idx < 0 || m_note_drop_idx < 0) {
          clear_note_drop_line();
          return false;
        }
        int n = (int)notes->size();
        int src_note = m_note_drag_idx;  // actual index in notes[]
        int drop_disp = m_note_drop_idx; // insertion slot in display order

        // Convert drop display slot → target index in notes[].
        // Display order is newest-first, so display_pos 0 = notes[n-1].
        // drop_disp == 0  → insert before notes[n-1], i.e. at notes[n-1] → dest
        // = n-1 drop_disp == n  → insert after  notes[0],   i.e. at end → dest
        // = 0
        int dest_note = n - 1 - drop_disp;
        // Clamp to valid range
        dest_note = std::clamp(dest_note, 0, n - 1);

        if (src_note == dest_note) {
          clear_note_drop_line();
          return true;
        }

        Note moving = (*notes)[src_note];
        notes->erase(notes->begin() + src_note);
        // Adjust dest after erase
        int adjusted = dest_note;
        if (src_note < dest_note)
          --adjusted;
        adjusted = std::clamp(adjusted, 0, (int)notes->size());
        notes->insert(notes->begin() + adjusted, std::move(moving));

        m_model.mark_modified();
        refresh_notes();
        return true;
      },
      false);

  card->add_controller(tgt);
}

void Inspector::build_notes_tab() {
  // ── Top pane: Notes ───────────────────────────────────────────────────────
  m_notes_outer.set_orientation(Gtk::Orientation::VERTICAL);
  m_notes_outer.set_margin_top(12);
  m_notes_outer.set_margin_start(8);
  m_notes_outer.set_margin_end(8);
  m_notes_outer.set_margin_bottom(6);
  m_notes_ctx_label.add_css_class("pref-group-title");
  m_notes_ctx_label.set_halign(Gtk::Align::START);
  m_notes_ctx_label.set_margin_bottom(6);
  m_notes_outer.append(m_notes_ctx_label);
  m_notes_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  m_notes_scroll.set_vexpand(true);
  m_notes_scroll.set_child(m_notes_list);
  m_notes_outer.append(m_notes_scroll);
  m_notes_add_row.set_orientation(Gtk::Orientation::VERTICAL);
  m_notes_add_row.set_spacing(6);
  m_notes_add_row.set_margin_top(3);

  // Multi-line note entry — 3 lines tall
  m_notes_entry_buf = Gtk::TextBuffer::create();
  m_notes_entry.set_buffer(m_notes_entry_buf);
  m_notes_entry.set_wrap_mode(Gtk::WrapMode::WORD_CHAR);
  m_notes_entry.set_accepts_tab(false);
  m_notes_entry.add_css_class("notes-entry-view");
  m_notes_entry_scroll.set_policy(Gtk::PolicyType::NEVER,
                                  Gtk::PolicyType::AUTOMATIC);
  m_notes_entry_scroll.set_min_content_height(64); // ~3 lines
  m_notes_entry_scroll.set_max_content_height(64);
  m_notes_entry_scroll.set_child(m_notes_entry);
  m_notes_entry_scroll.add_css_class("notes-entry-frame");

  m_notes_add_btn.set_label("Add Note");
  m_notes_add_btn.add_css_class("pill-btn");
  m_notes_add_btn.set_halign(Gtk::Align::END);

  auto do_add = [this]() {
    auto text = std::string(m_notes_entry_buf->get_text());
    // trim trailing whitespace
    while (!text.empty() && (text.back() == '\n' || text.back() == ' '))
      text.pop_back();
    if (text.empty())
      return;
    auto *nv = active_notes();
    if (!nv)
      return;
    nv->push_back({inspector_now_iso8601(), text});
    m_model.mark_modified();
    m_notes_entry_buf->set_text("");
    refresh_notes();
  };
  m_notes_add_btn.signal_clicked().connect(do_add);
  // Ctrl+Enter to submit
  auto key_ctrl = Gtk::EventControllerKey::create();
  key_ctrl->signal_key_pressed().connect(
      [do_add](guint kv, guint, Gdk::ModifierType state) -> bool {
        bool ctrl =
            (state & Gdk::ModifierType::CONTROL_MASK) != Gdk::ModifierType{};
        if (ctrl && (kv == GDK_KEY_Return || kv == GDK_KEY_KP_Enter)) {
          do_add();
          return true;
        }
        return false;
      },
      false);
  m_notes_entry.add_controller(key_ctrl);

  m_notes_add_row.append(m_notes_entry_scroll);
  m_notes_add_row.append(m_notes_add_btn);
  m_notes_outer.append(m_notes_add_row);

  // ── Bottom pane: Annotations ──────────────────────────────────────────────
  // Header with label + Report button
  auto *ann_hdr = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
  ann_hdr->set_margin_start(8);
  ann_hdr->set_margin_end(8);
  ann_hdr->set_margin_top(3);
  ann_hdr->set_margin_bottom(4);
  auto *ann_lbl = Gtk::make_managed<Gtk::Label>("Annotations");
  ann_lbl->add_css_class("pref-group-title");
  ann_lbl->set_halign(Gtk::Align::START);
  auto *ann_spacer = Gtk::make_managed<Gtk::Box>();
  ann_spacer->set_hexpand(true);
  auto *report_btn = Gtk::make_managed<Gtk::Button>("Report…");
  report_btn->add_css_class("flat");
  report_btn->set_tooltip_text("View all annotations across the project");
  report_btn->signal_clicked().connect([this]() {
    auto *root = dynamic_cast<Gtk::Window *>(get_root());
    if (!root)
      return;
    if (!m_ann_report)
      m_ann_report = std::make_unique<AnnotationReportDialog>(*root, m_model);
    else
      m_ann_report->refresh();
    m_ann_report->present();
  });
  ann_hdr->append(*ann_lbl);
  ann_hdr->append(*ann_spacer);
  ann_hdr->append(*report_btn);

  auto *ann_sep =
      Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);

  m_ann_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  m_ann_scroll.set_vexpand(true);
  m_ann_box.set_orientation(Gtk::Orientation::VERTICAL);
  m_ann_box.set_spacing(0);
  m_ann_scroll.set_child(m_ann_box);

  m_ann_section.append(*ann_hdr);
  m_ann_section.append(*ann_sep);
  m_ann_section.append(m_ann_scroll);

  // ── Paned ─────────────────────────────────────────────────────────────────
  m_notes_paned.set_start_child(m_notes_outer);
  m_notes_paned.set_end_child(m_ann_section);
  m_notes_paned.set_resize_start_child(true);
  m_notes_paned.set_resize_end_child(true);
  m_notes_paned.set_shrink_start_child(true);
  m_notes_paned.set_shrink_end_child(true);
  m_notes_paned.set_position(m_prefs.notes_anno_pane_pos);
  m_notes_paned.property_position().signal_changed().connect([this]() {
    m_prefs.notes_anno_pane_pos = m_notes_paned.get_position();
    m_prefs.save();
  });

  m_stack.add(m_notes_paned, "notes");
  refresh_notes();
}

// ─────────────────────────────────────────────────────────────────────────────
// History tab
// ─────────────────────────────────────────────────────────────────────────────

void Inspector::build_history_tab() {
  m_history_outer.set_margin_top(12);
  m_history_outer.set_margin_start(8);
  m_history_outer.set_margin_end(8);
  auto *hdr = Gtk::make_managed<Gtk::Label>("Snapshots");
  hdr->add_css_class("pref-group-title");
  hdr->set_halign(Gtk::Align::START);
  m_history_outer.append(*hdr);

  m_last_snap_lbl.add_css_class("snap-date");
  m_last_snap_lbl.set_halign(Gtk::Align::START);
  m_last_snap_lbl.set_margin_bottom(6);
  m_history_outer.append(m_last_snap_lbl);

  m_history_list.set_orientation(Gtk::Orientation::VERTICAL);
  m_history_list.set_spacing(4);
  m_history_scroll.set_child(m_history_list);
  m_history_scroll.set_vexpand(true);
  m_history_scroll.set_policy(Gtk::PolicyType::NEVER,
                              Gtk::PolicyType::AUTOMATIC);
  m_history_outer.append(m_history_scroll);
  m_snap_btn.set_label("Save Snapshot");
  m_snap_btn.set_icon_name("org.gnome.Settings-camera-access-symbolic");
  m_snap_btn.add_css_class("pill-btn");
  m_snap_btn.set_hexpand(true);
  m_snap_btn.set_tooltip_text("Save the current text as a named snapshot");
  m_snap_btn.signal_clicked().connect([this]() {
    if (!m_current_node)
      return;
    m_current_node->save_snapshot("Manual snapshot");
    m_model.mark_modified();
    refresh_history();
    notify_toast("📷  Snapshot saved");
  });
  m_history_outer.append(m_snap_btn);
  m_stack.add(m_history_outer, "history");
}

void Inspector::refresh_history() {
  while (auto *c = m_history_list.get_first_child())
    m_history_list.remove(*c);
  if (!m_current_node) {
    m_last_snap_lbl.set_text("");
    return;
  }
  const auto &snaps = m_current_node->snapshots;

  // Update the "last snapshot" indicator above the list
  if (snaps.empty()) {
    m_last_snap_lbl.set_text("No snapshots yet");
  } else {
    m_last_snap_lbl.set_text("Last: " + snaps.back().timestamp);
  }

  if (snaps.empty()) {
    auto *lbl = Gtk::make_managed<Gtk::Label>(
        "No snapshots yet.\nUse the button below to save one.");
    lbl->add_css_class("board-placeholder");
    lbl->set_justify(Gtk::Justification::CENTER);
    lbl->set_margin_top(16);
    m_history_list.append(*lbl);
    return;
  }

  // Display newest-first
  for (int i = (int)snaps.size() - 1; i >= 0; --i) {
    const auto &snap = snaps[i];

    // ── Card ──────────────────────────────────────────────────────────────
    auto *card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    card->add_css_class("snap-item");

    // Top row: name + index badge
    auto *top = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    auto *name_lbl = Gtk::make_managed<Gtk::Label>(snap.name);
    name_lbl->add_css_class("snap-name");
    name_lbl->set_hexpand(true);
    name_lbl->set_halign(Gtk::Align::START);
    name_lbl->set_ellipsize(Pango::EllipsizeMode::END);
    auto *idx_badge =
        Gtk::make_managed<Gtk::Label>("#" + std::to_string(i + 1));
    idx_badge->add_css_class("badge-chip");
    top->append(*name_lbl);
    top->append(*idx_badge);
    card->append(*top);

    // Timestamp
    auto *ts_lbl = Gtk::make_managed<Gtk::Label>(snap.timestamp);
    ts_lbl->add_css_class("snap-date");
    ts_lbl->set_halign(Gtk::Align::START);
    card->append(*ts_lbl);

    // Action buttons row — icon-only to keep cards compact
    auto *btns = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 2);
    btns->set_margin_top(4);

    auto make_icon_btn = [](const std::string &icon, const std::string &tip) {
      auto *b = Gtk::make_managed<Gtk::Button>();
      b->set_icon_name(icon);
      b->add_css_class("flat");
      b->add_css_class("snap-action-btn");
      b->set_tooltip_text(tip);
      return b;
    };

    auto *btn_review =
        make_icon_btn("document-open-symbolic", "Review snapshot text");
    auto *btn_diff =
        make_icon_btn("edit-find-replace-symbolic", "Compare to current text");
    auto *btn_restore = make_icon_btn(
        "edit-undo-symbolic", "Restore this snapshot (saves current first)");
    auto *btn_rename =
        make_icon_btn("document-edit-symbolic", "Rename this snapshot");
    auto *btn_delete =
        make_icon_btn("edit-delete-symbolic", "Delete this snapshot");

    btn_review->signal_clicked().connect(
        [this, i]() { show_snapshot_review(i); });
    btn_diff->signal_clicked().connect([this, i]() {
        // s98 — prefer the integrated side-by-side editor view; fall back to the
        // inline modal only if the host hasn't wired the callback.
        if (on_open_diff && m_current_node)
            on_open_diff(m_current_node, i);
        else
            show_snapshot_diff(i);
    });
    btn_restore->signal_clicked().connect([this, i]() {
      if (!m_current_node)
        return;
      m_current_node->save_snapshot("Before restore");
      m_current_node->content = m_current_node->snapshots[i].content;
      m_model.mark_modified();
      notify_content_changed();
      notify_meta_changed();
      refresh_history();
    });
    btn_rename->signal_clicked().connect([this, i]() { rename_snapshot(i); });
    btn_delete->signal_clicked().connect([this, i]() { delete_snapshot(i); });

    btns->append(*btn_review);
    btns->append(*btn_diff);
    btns->append(*btn_restore);
    btns->append(*btn_rename);
    btns->append(*btn_delete);
    card->append(*btns);

    m_history_list.append(*card);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// html_to_plain / split_words / compute_diff — delegated to SnapshotDiff
// ─────────────────────────────────────────────────────────────────────────────

std::string Inspector::html_to_plain(const std::string &html) {
  return SnapshotDiff::html_to_plain(html);
}

std::vector<std::string> Inspector::split_words(const std::string &text) {
  return SnapshotDiff::split_words(text);
}

std::vector<Inspector::DiffOp>
Inspector::compute_diff(const std::vector<std::string> &a,
                        const std::vector<std::string> &b) {
  auto ops = SnapshotDiff::compute(a, b);
  std::vector<Inspector::DiffOp> result;
  result.reserve(ops.size());
  for (auto &op : ops) {
    Inspector::DiffOp::Kind k;
    switch (op.kind) {
    case Folio::DiffOp::Kind::Insert:
      k = Inspector::DiffOp::Kind::Insert;
      break;
    case Folio::DiffOp::Kind::Delete:
      k = Inspector::DiffOp::Kind::Delete;
      break;
    default:
      k = Inspector::DiffOp::Kind::Equal;
      break;
    }
    result.push_back({k, op.text});
  }
  return result;
}

// show_snapshot_review — plain-text preview dialog

// ─────────────────────────────────────────────────────────────────────────────
void Inspector::show_snapshot_review(int snap_idx) {
  if (!m_current_node || snap_idx < 0 ||
      snap_idx >= (int)m_current_node->snapshots.size())
    return;
  const auto &snap = m_current_node->snapshots[snap_idx];
  auto *win = dynamic_cast<Gtk::Window *>(get_root());
  if (!win)
    return;

  auto *dlg = Gtk::make_managed<Gtk::Window>();
  dlg->set_transient_for(*win);
  dlg->set_modal(true);
  dlg->set_title(snap.name + "  ·  " + snap.timestamp);
  dlg->set_default_size(560, 500);
  dlg->add_css_class("folio-dialog");

  auto *outer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);

  // Header bar
  auto *hbar = Gtk::make_managed<Gtk::HeaderBar>();
  hbar->set_show_title_buttons(true);
  auto *title_lbl = Gtk::make_managed<Gtk::Label>(snap.name);
  title_lbl->add_css_class("snap-name");
  auto *ts_lbl = Gtk::make_managed<Gtk::Label>(snap.timestamp);
  ts_lbl->add_css_class("snap-date");
  auto *hbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
  hbox->set_valign(Gtk::Align::CENTER);
  hbox->append(*title_lbl);
  hbox->append(*ts_lbl);
  hbar->set_title_widget(*hbox);
  dlg->set_titlebar(*hbar);

  // Scrolled text view (read-only)
  auto *scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
  scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  scroll->set_vexpand(true);
  auto *tv = Gtk::make_managed<Gtk::TextView>();
  tv->set_editable(false);
  tv->set_cursor_visible(false);
  tv->set_wrap_mode(Gtk::WrapMode::WORD_CHAR);
  tv->set_left_margin(24);
  tv->set_right_margin(24);
  tv->set_top_margin(16);
  tv->set_bottom_margin(16);
  tv->add_css_class("paper-body");
  {
    auto prov = Gtk::CssProvider::create();
    prov->load_from_data(
        "textview text { font-size: 15px; line-height: 1.8; }");
    tv->get_style_context()->add_provider(prov,
                                          GTK_STYLE_PROVIDER_PRIORITY_USER + 1);
  }
  tv->get_buffer()->set_text(html_to_plain(snap.content));
  scroll->set_child(*tv);
  outer->append(*scroll);
  dlg->set_child(*outer);
  dlg->present();
}

// ─────────────────────────────────────────────────────────────────────────────
// show_snapshot_diff — word-level diff dialog
// ─────────────────────────────────────────────────────────────────────────────
void Inspector::show_snapshot_diff(int snap_idx) {
  if (!m_current_node || snap_idx < 0 ||
      snap_idx >= (int)m_current_node->snapshots.size())
    return;
  const auto &snap = m_current_node->snapshots[snap_idx];
  auto *win = dynamic_cast<Gtk::Window *>(get_root());
  if (!win)
    return;

  std::string snap_plain = html_to_plain(snap.content);
  std::string current_plain = html_to_plain(m_current_node->content);

  auto snap_words = split_words(snap_plain);
  auto current_words = split_words(current_plain);
  auto ops = compute_diff(snap_words, current_words);

  auto *dlg = Gtk::make_managed<Gtk::Window>();
  dlg->set_transient_for(*win);
  dlg->set_modal(true);
  dlg->set_title("Diff  ·  " + snap.name + "  →  Current");
  dlg->set_default_size(620, 580);

  auto *hbar = Gtk::make_managed<Gtk::HeaderBar>();
  hbar->set_show_title_buttons(true);

  // Legend in header
  auto *legend = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
  legend->set_valign(Gtk::Align::CENTER);
  auto make_chip = [](const std::string &txt, const std::string &css) {
    auto *l = Gtk::make_managed<Gtk::Label>(txt);
    l->add_css_class("badge-chip");
    l->add_css_class(css);
    return l;
  };
  legend->append(*make_chip("− removed", "diff-del"));
  legend->append(*make_chip("+ added", "diff-ins"));
  hbar->pack_end(*legend);
  dlg->set_titlebar(*hbar);

  // Build the diff in a TextView with tag-coloured spans
  auto *scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
  scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  scroll->set_vexpand(true);

  auto *tv = Gtk::make_managed<Gtk::TextView>();
  tv->set_editable(false);
  tv->set_cursor_visible(false);
  tv->set_wrap_mode(Gtk::WrapMode::WORD_CHAR);
  tv->set_left_margin(24);
  tv->set_right_margin(24);
  tv->set_top_margin(16);
  tv->set_bottom_margin(16);
  tv->add_css_class("paper-body");
  {
    auto prov = Gtk::CssProvider::create();
    prov->load_from_data(
        "textview text { font-size: 15px; line-height: 1.8; }");
    tv->get_style_context()->add_provider(prov,
                                          GTK_STYLE_PROVIDER_PRIORITY_USER + 1);
  }

  auto buf = tv->get_buffer();
  auto tag_del = buf->create_tag("diff_del");
  tag_del->property_foreground() = "#f38ba8";
  tag_del->property_strikethrough() = true;
  auto tag_ins = buf->create_tag("diff_ins");
  tag_ins->property_foreground() = "#a6e3a1";

  for (const auto &op : ops) {
    auto iter = buf->end();
    buf->insert(iter, op.text);
    // Apply tag over the inserted text
    if (op.kind != DiffOp::Kind::Equal) {
      auto end_iter = buf->end();
      auto start_iter = end_iter;
      int len = (int)Glib::ustring(op.text).size();
      start_iter.backward_chars(len);
      if (op.kind == DiffOp::Kind::Delete)
        buf->apply_tag(tag_del, start_iter, end_iter);
      else
        buf->apply_tag(tag_ins, start_iter, end_iter);
    }
  }

  scroll->set_child(*tv);

  // Stats bar
  int n_del = 0, n_ins = 0;
  for (const auto &op : ops) {
    auto wc = split_words(op.text);
    int words = 0;
    for (auto &w : wc)
      if (w != " " && w != "\n" && w != "\t")
        ++words;
    if (op.kind == DiffOp::Kind::Delete)
      n_del += words;
    if (op.kind == DiffOp::Kind::Insert)
      n_ins += words;
  }
  auto *stats = Gtk::make_managed<Gtk::Label>(
      "  −" + std::to_string(n_del) + " words removed    +" +
      std::to_string(n_ins) + " words added");
  stats->add_css_class("snap-date");
  stats->set_halign(Gtk::Align::START);
  stats->set_margin_start(8);
  stats->set_margin_top(6);
  stats->set_margin_bottom(3);

  auto *outer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
  outer->append(*scroll);
  outer->append(*stats);
  dlg->set_child(*outer);
  dlg->present();
}

// ─────────────────────────────────────────────────────────────────────────────
// rename_snapshot
// ─────────────────────────────────────────────────────────────────────────────
void Inspector::rename_snapshot(int snap_idx) {
  if (!m_current_node || snap_idx < 0 ||
      snap_idx >= (int)m_current_node->snapshots.size())
    return;
  auto *win = dynamic_cast<Gtk::Window *>(get_root());
  if (!win)
    return;

  auto *dlg = Gtk::make_managed<Gtk::Window>();
  dlg->set_transient_for(*win);
  dlg->set_modal(true);
  dlg->set_title("Rename Snapshot");
  dlg->set_default_size(360, -1);

  auto *hbar = Gtk::make_managed<Gtk::HeaderBar>();
  hbar->set_show_title_buttons(false);
  auto *cancel_btn = Gtk::make_managed<Gtk::Button>("Cancel");
  cancel_btn->signal_clicked().connect([dlg]() { dlg->close(); });
  auto *ok_btn = Gtk::make_managed<Gtk::Button>("Rename");
  ok_btn->add_css_class("pill-btn");
  ok_btn->add_css_class("pill-btn-primary");
  hbar->pack_start(*cancel_btn);
  hbar->pack_end(*ok_btn);
  dlg->set_titlebar(*hbar);

  auto *box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
  box->set_margin_top(16);
  box->set_margin_bottom(16);
  box->set_margin_start(16);
  box->set_margin_end(16);
  auto *entry = Gtk::make_managed<Gtk::Entry>();
  entry->set_text(m_current_node->snapshots[snap_idx].name);
  entry->set_placeholder_text("Snapshot name…");
  box->append(*entry);
  dlg->set_child(*box);

  auto do_rename = [this, dlg, entry, snap_idx]() {
    std::string new_name = std::string(entry->get_text());
    if (new_name.empty())
      return;
    if (snap_idx < (int)m_current_node->snapshots.size())
      m_current_node->snapshots[snap_idx].name = new_name;
    m_model.mark_modified();
    refresh_history();
    dlg->close();
  };
  ok_btn->signal_clicked().connect(do_rename);
  entry->signal_activate().connect(do_rename);
  dlg->present();
}

// ─────────────────────────────────────────────────────────────────────────────
// delete_snapshot
// ─────────────────────────────────────────────────────────────────────────────
void Inspector::delete_snapshot(int snap_idx) {
  if (!m_current_node || snap_idx < 0 ||
      snap_idx >= (int)m_current_node->snapshots.size())
    return;
  auto *win = dynamic_cast<Gtk::Window *>(get_root());
  if (!win)
    return;

  std::string sname = m_current_node->snapshots[snap_idx].name;
  auto dlg = Gtk::AlertDialog::create("Delete \"" + sname + "\"?");
  dlg->set_detail(
      "This snapshot will be permanently removed and cannot be recovered.");
  dlg->set_modal(true);
  dlg->set_buttons({"Cancel", "Delete"});
  dlg->set_cancel_button(0);
  dlg->set_default_button(0);
  dlg->choose(
      *win, [this, dlg, snap_idx](Glib::RefPtr<Gio::AsyncResult> &res) mutable {
        int r = 0;
        try {
          r = dlg->choose_finish(res);
        } catch (...) {
        }
        if (r == 1 && m_current_node &&
            snap_idx < (int)m_current_node->snapshots.size()) {
          m_current_node->snapshots.erase(m_current_node->snapshots.begin() +
                                          snap_idx);
          m_model.mark_modified();
          refresh_history();
        }
      });
}

// ─────────────────────────────────────────────────────────────────────────────
// Project tab
// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
// Shared disclosure header builder
// ─────────────────────────────────────────────────────────────────────────────

Gtk::Box *Inspector::make_disclosure_hdr(const std::string &title,
                                          Gtk::Revealer &revealer,
                                          Gtk::Label &arrow,
                                          bool &expanded_flag) {
  auto *hdr = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
  hdr->add_css_class("tile-header-row");
  hdr->set_cursor(Gdk::Cursor::create("pointer"));

  auto *lbl = Gtk::make_managed<Gtk::Label>(title);
  lbl->add_css_class("inspector-section-label");
  lbl->set_halign(Gtk::Align::START);
  lbl->set_hexpand(true);

  arrow.set_text(expanded_flag ? "▾" : "▸");
  arrow.add_css_class("section-arrow");
  arrow.set_margin_end(2);

  hdr->append(*lbl);
  hdr->append(arrow);

  revealer.set_reveal_child(expanded_flag);
  revealer.set_transition_type(Gtk::RevealerTransitionType::SLIDE_DOWN);
  revealer.set_transition_duration(180);

  auto gc = Gtk::GestureClick::create();
  gc->set_button(1);
  gc->signal_pressed().connect([this, &revealer, &arrow, &expanded_flag](int, double, double) {
    expanded_flag = !expanded_flag;
    revealer.set_reveal_child(expanded_flag);
    arrow.set_text(expanded_flag ? "▾" : "▸");
    m_prefs.save();
  });
  hdr->add_controller(gc);
  return hdr;
}

void Inspector::build_project_tab() {
  m_proj_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  m_proj_scroll.set_vexpand(true);
  m_proj_outer.set_orientation(Gtk::Orientation::VERTICAL);
  m_proj_outer.set_spacing(16);
  m_proj_outer.set_margin_top(12);
  m_proj_outer.set_margin_start(8);
  m_proj_outer.set_margin_end(8);
  m_proj_outer.set_margin_bottom(12);

  auto row = [](const std::string &label, Gtk::Widget &w) {
    auto *r = Gtk::make_managed<Gtk::ListBoxRow>();
    auto *rb = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    rb->set_margin_start(8);
    rb->set_margin_end(8);
    rb->set_margin_top(3);
    rb->set_margin_bottom(3);
    auto *l = Gtk::make_managed<Gtk::Label>(label);
    l->add_css_class("pref-row-label");
    l->set_hexpand(true);
    l->set_halign(Gtk::Align::START);
    w.set_size_request(140, -1);
    w.set_halign(Gtk::Align::END);
    rb->append(*l);
    rb->append(w);
    r->set_child(*rb);
    return r;
  };

  // Helper: build a disclosure header row, wire it to a revealer + arrow label,
  // and persist state via a bool ref + pref save. Returns the header Box.
  auto make_disclosure_hdr = [this](const std::string &title,
                                    Gtk::Revealer &revealer,
                                    Gtk::Label &arrow,
                                    bool &expanded_flag) -> Gtk::Box * {
    return this->make_disclosure_hdr(title, revealer, arrow, expanded_flag);
  };

  // ── Project ───────────────────────────────────────────────────────────────
  m_proj_outer.append(*make_disclosure_hdr("Project",
      m_proj_project_revealer, m_proj_project_arrow,
      m_prefs.inspector_proj_project_expanded));
  {
    auto *lb = make_listbox();
    m_proj_title_entry.set_placeholder_text("Project title…");
    m_proj_title_entry.signal_changed().connect([this]() {
      if (m_loading)
        return;
      m_model.project_title = std::string(m_proj_title_entry.get_text());
      m_model.mark_modified();
      notify_meta_changed();
    });
    lb->append(*row("Title", m_proj_title_entry));
    m_proj_author_entry.set_placeholder_text("Author name…");
    m_proj_author_entry.signal_changed().connect([this]() {
      if (m_loading)
        return;
      m_model.author = std::string(m_proj_author_entry.get_text());
      m_model.mark_modified();
    });
    lb->append(*row("Author", m_proj_author_entry));
    { // Genre dropdown
      auto genre_model = Gtk::StringList::create({"—"});
      for (auto &g : m_prefs.genres)
        genre_model->append(g);
      m_proj_genre_dropdown = Gtk::make_managed<Gtk::DropDown>(genre_model);
      m_proj_genre_dropdown->property_selected().signal_changed().connect(
          [this]() {
            if (m_loading)
              return;
            guint idx = m_proj_genre_dropdown->get_selected();
            auto *sl = dynamic_cast<Gtk::StringList *>(
                m_proj_genre_dropdown->get_model().get());
            m_model.genre =
                (idx == 0 || !sl) ? "" : std::string(sl->get_string(idx));
            m_model.mark_modified();
          });
      lb->append(*row("Genre", *m_proj_genre_dropdown));
    }
    m_proj_project_revealer.set_child(*lb);
    m_proj_outer.append(m_proj_project_revealer);
  }

  // ── Synopsis ──────────────────────────────────────────────────────────────
  m_proj_outer.append(*make_disclosure_hdr("Synopsis",
      m_proj_synopsis_revealer, m_proj_synopsis_arrow,
      m_prefs.inspector_proj_synopsis_expanded));
  {
    m_proj_synopsis_view.set_buffer(m_proj_synopsis_buffer);
    m_proj_synopsis_view.add_css_class("synopsis-view");
    m_proj_synopsis_view.set_wrap_mode(Gtk::WrapMode::WORD_CHAR);
    m_proj_synopsis_view.set_top_margin(8);
    m_proj_synopsis_view.set_bottom_margin(8);
    m_proj_synopsis_view.set_left_margin(8);
    m_proj_synopsis_view.set_right_margin(8);
    auto *sc = Gtk::make_managed<Gtk::ScrolledWindow>();
    sc->set_child(m_proj_synopsis_view);
    sc->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    sc->set_size_request(-1, 100);
    sc->add_css_class("synopsis-view");
    m_proj_synopsis_buffer->signal_changed().connect([this]() {
      if (m_loading)
        return;
      m_model.project_synopsis = std::string(m_proj_synopsis_buffer->get_text(
          m_proj_synopsis_buffer->begin(), m_proj_synopsis_buffer->end(),
          false));
      m_model.mark_modified();
    });
    m_proj_synopsis_revealer.set_child(*sc);
    m_proj_outer.append(m_proj_synopsis_revealer);
  }

  // ── Publication ───────────────────────────────────────────────────────────
  m_proj_outer.append(*make_disclosure_hdr("Publication",
      m_proj_publication_revealer, m_proj_publication_arrow,
      m_prefs.inspector_proj_publication_expanded));
  {
    auto *lb = make_listbox();
    m_proj_publisher_entry.set_placeholder_text("Publisher…");
    m_proj_publisher_entry.signal_changed().connect([this]() {
      if (m_loading)
        return;
      m_model.publisher = std::string(m_proj_publisher_entry.get_text());
      m_model.mark_modified();
    });
    lb->append(*row("Publisher", m_proj_publisher_entry));
    m_proj_isbn_entry.set_placeholder_text("###-#-##-######-#");
    m_proj_isbn_entry.set_max_length(17); // ISBN-13 with hyphens
    m_proj_isbn_entry.signal_changed().connect([this]() {
      if (m_loading)
        return;
      m_model.isbn = std::string(m_proj_isbn_entry.get_text());
      m_model.mark_modified();
      update_barcode_thumbnail();
    });
    // ── ISBN row: barcode thumbnail button + entry ─────────────────────
    {
      auto* r   = Gtk::make_managed<Gtk::ListBoxRow>();
      auto* rb  = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
      rb->set_margin_start(8); rb->set_margin_end(8);
      rb->set_margin_top(3);    rb->set_margin_bottom(3);
      auto* lbl = Gtk::make_managed<Gtk::Label>("ISBN");
      lbl->add_css_class("pref-row-label");
      lbl->set_hexpand(true);
      lbl->set_halign(Gtk::Align::START);
      // Miniature barcode thumbnail — acts as a button to open BarcodeDialog
      m_proj_barcode_btn.set_size_request(48, 32);
      m_proj_barcode_btn.set_tooltip_text("View / export ISBN barcode");
      m_proj_barcode_btn.set_cursor(Gdk::Cursor::create("pointer"));
      m_proj_barcode_btn.set_visible(false); // hidden until valid ISBN
      m_proj_barcode_btn.set_draw_func([this](const Cairo::RefPtr<Cairo::Context>& cr,
                                               int w, int h) {
        cr->set_source_rgb(0.15, 0.15, 0.18);
        cr->paint();
        if (m_model.barcode_svg.empty()) return;
        GError* err = nullptr;
        RsvgHandle* rsvg = rsvg_handle_new_from_data(
            (const guint8*)m_model.barcode_svg.data(),
            (gsize)m_model.barcode_svg.size(), &err);
        if (!rsvg) { if (err) g_error_free(err); return; }
        rsvg_handle_set_dpi(rsvg, 72.0);
        gdouble svg_w = 0.0, svg_h = 0.0;
        rsvg_handle_get_intrinsic_size_in_pixels(rsvg, &svg_w, &svg_h);
        if (svg_w <= 0.0 || svg_h <= 0.0) {
            RsvgRectangle ink_r = {};
            rsvg_handle_get_geometry_for_element(rsvg, nullptr, &ink_r, nullptr, &err);
            if (err) { g_error_free(err); err = nullptr; }
            svg_w = ink_r.width  > 0 ? ink_r.width  : w;
            svg_h = ink_r.height > 0 ? ink_r.height : h;
        }
        double scale = std::min((w-4.0)/svg_w, (h-4.0)/svg_h);
        int buf_w = (int)std::ceil(svg_w*scale);
        int buf_h = (int)std::ceil(svg_h*scale);
        cairo_surface_t* offscreen = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, buf_w, buf_h);
        cairo_t* ocr = cairo_create(offscreen);
        cairo_set_source_rgb(ocr, 1.0, 1.0, 1.0); cairo_paint(ocr);
        RsvgRectangle vp = {0.0, 0.0, (double)buf_w, (double)buf_h};
        rsvg_handle_render_document(rsvg, ocr, &vp, &err);
        cairo_destroy(ocr); cairo_surface_flush(offscreen);
        double ox2 = (w-buf_w)/2.0, oy2 = (h-buf_h)/2.0;
        cairo_set_source_surface(cr->cobj(), offscreen, ox2, oy2);
        cairo_paint(cr->cobj());
        if (err) g_error_free(err);
        g_object_unref(rsvg);
      });
      auto click = Gtk::GestureClick::create();
      click->signal_released().connect([this](int, double, double) {
        if (m_barcode_dialog) { m_barcode_dialog->present(); return; }
        auto* win = dynamic_cast<Gtk::Window*>(get_root());
        if (!win) return;
        m_barcode_dialog = std::make_unique<BarcodeDialog>(*win, m_model);
        m_barcode_dialog->set_svg_saved_callback([this]() {
          m_proj_barcode_btn.queue_draw();
        });
        m_barcode_dialog->set_hide_on_close(true);
        m_barcode_dialog->present();
      });
      m_proj_barcode_btn.add_controller(click);
      m_proj_isbn_entry.set_size_request(140, -1);
      m_proj_isbn_entry.set_halign(Gtk::Align::END);
      rb->append(*lbl);
      rb->append(m_proj_barcode_btn);
      rb->append(m_proj_isbn_entry);
      r->set_child(*rb);
      lb->append(*r);
    }
    m_proj_year_entry.set_placeholder_text("e.g. 2025…");
    m_proj_year_entry.signal_changed().connect([this]() {
      if (m_loading)
        return;
      m_model.year = std::string(m_proj_year_entry.get_text());
      m_model.mark_modified();
    });
    lb->append(*row("Year", m_proj_year_entry));
    m_proj_publication_revealer.set_child(*lb);
    m_proj_outer.append(m_proj_publication_revealer);
  }

  // ── Cover Image ───────────────────────────────────────────────────────────
  m_proj_outer.append(*make_disclosure_hdr("Cover Image",
      m_proj_cover_revealer, m_proj_cover_arrow,
      m_prefs.inspector_proj_cover_expanded));
  {
    auto *cover_card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    cover_card->add_css_class("pomo-tile-card");

    // Thumbnail drawing area — 96×144 px (4:6 ratio at screen size)
    m_cover_thumbnail_area.set_size_request(96, 144);
    m_cover_thumbnail_area.set_halign(Gtk::Align::CENTER);
    m_cover_thumbnail_area.set_margin_top(3);
    m_cover_thumbnail_area.set_margin_bottom(3);
    m_cover_thumbnail_area.set_cursor(Gdk::Cursor::create("pointer"));
    m_cover_thumbnail_area.set_tooltip_text("Click to view cover");
    m_cover_thumbnail_area.set_draw_func([this](const Cairo::RefPtr<Cairo::Context>& cr,
                                                 int w, int h) {
      if (m_cover_pixbuf) {
        double scale = std::min((double)w / m_cover_pixbuf->get_width(),
                                (double)h / m_cover_pixbuf->get_height());
        int dw = (int)(m_cover_pixbuf->get_width()  * scale);
        int dh = (int)(m_cover_pixbuf->get_height() * scale);
        double ox = (w - dw) / 2.0;
        double oy = (h - dh) / 2.0;
        auto scaled = m_cover_pixbuf->scale_simple(dw, dh, Gdk::InterpType::BILINEAR);
        Gdk::Cairo::set_source_pixbuf(cr, scaled, ox, oy);
        cr->paint();
      } else {
        cr->set_source_rgb(0.2, 0.2, 0.25);
        cr->rectangle(0, 0, w, h);
        cr->fill();
        cr->set_source_rgb(0.5, 0.5, 0.55);
        cr->select_font_face("sans", Cairo::ToyFontFace::Slant::NORMAL,
                             Cairo::ToyFontFace::Weight::NORMAL);
        cr->set_font_size(11);
        Cairo::TextExtents te;
        cr->get_text_extents("No Cover", te);
        cr->move_to((w - te.width) / 2.0 - te.x_bearing,
                    (h - te.height) / 2.0 - te.y_bearing);
        cr->show_text("No Cover");
      }
    });
    auto cover_click = Gtk::GestureClick::create();
    cover_click->signal_released().connect([this](int, double, double) {
      if (!m_model.cover_thumbnail.empty())
        show_cover_dialog();
    });
    m_cover_thumbnail_area.add_controller(cover_click);
    cover_card->append(m_cover_thumbnail_area);

    // Path entry + Browse button
    auto *cover_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    cover_row->set_margin_top(6);
    cover_row->set_margin_bottom(4);
    m_cover_path_entry.set_placeholder_text("Image path…");
    m_cover_path_entry.set_hexpand(true);
    m_cover_path_entry.set_editable(false);
    cover_row->append(m_cover_path_entry);
    auto *browse_btn = Gtk::make_managed<Gtk::Button>("Browse…");
    browse_btn->add_css_class("pill-btn");
    browse_btn->signal_clicked().connect([this]() {
      auto *win = dynamic_cast<Gtk::Window *>(get_root());
      if (!win) return;
      auto chooser = Gtk::FileChooserNative::create(
          "Select Cover Image", *win, Gtk::FileChooser::Action::OPEN,
          "Open", "Cancel");
      auto filter = Gtk::FileFilter::create();
      filter->set_name("Images (JPEG, PNG)");
      filter->add_mime_type("image/jpeg");
      filter->add_mime_type("image/png");
      chooser->add_filter(filter);
      chooser->signal_response().connect([this, chooser](int response) {
        if (response == Gtk::ResponseType::ACCEPT) {
          auto file = chooser->get_file();
          if (file)
            load_cover_from_path(file->get_path());
        }
      });
      chooser->show();
    });
    cover_row->append(*browse_btn);
    cover_card->append(*cover_row);

    m_proj_cover_revealer.set_child(*cover_card);
    m_proj_outer.append(m_proj_cover_revealer);
  }

  // ── Goals ─────────────────────────────────────────────────────────────────
  m_proj_outer.append(*make_disclosure_hdr("Goals",
      m_proj_goals_revealer, m_proj_goals_arrow,
      m_prefs.inspector_proj_goals_expanded));
  {
    auto *lb = make_listbox();

    // Daily session goal
    m_proj_daily_target_entry.set_placeholder_text("words / day…");
    m_proj_daily_target_entry.set_input_purpose(Gtk::InputPurpose::DIGITS);
    m_proj_daily_target_entry.signal_changed().connect([this]() {
      if (m_loading)
        return;
      try {
        int v = std::stoi(std::string(m_proj_daily_target_entry.get_text()));
        if (v > 0) {
          m_model.daily_target = v;
          m_prefs.daily_word_goal = v;
          try {
            m_prefs.save();
          } catch (...) {
          }
          m_model.mark_modified();
          notify_meta_changed();
          refresh_goal_computed();
        }
      } catch (...) {
      }
    });
    lb->append(*row("Daily goal (words)", m_proj_daily_target_entry));

    // Project word target
    m_proj_word_target_spin.set_adjustment(
        Gtk::Adjustment::create(80000, 0, 2000000, 1000, 10000));
    m_proj_word_target_spin.set_digits(0);
    m_proj_word_target_spin.set_numeric(true);
    m_proj_word_target_spin.set_size_request(130, -1);
    m_proj_word_target_spin.signal_value_changed().connect([this]() {
      if (m_loading)
        return;
      m_model.project_word_target = (int)m_proj_word_target_spin.get_value();
      m_model.mark_modified();
      refresh_goal_computed();
      if (m_goal_dialog)
        m_goal_dialog->refresh();
    });
    lb->append(*row("Project target (words)", m_proj_word_target_spin));

    // Due date
    m_proj_due_date_entry.set_placeholder_text("YYYY-MM-DD");
    m_proj_due_date_entry.set_max_length(10);
    m_proj_due_date_entry.signal_changed().connect([this]() {
      if (m_loading)
        return;
      m_model.due_date = std::string(m_proj_due_date_entry.get_text());
      m_model.mark_modified();
      refresh_goal_computed();
      if (m_goal_dialog)
        m_goal_dialog->refresh();
    });
    lb->append(*row("Due date", m_proj_due_date_entry));

    // Computed pace (read-only)
    m_proj_pace_lbl.set_text("—");
    m_proj_pace_lbl.add_css_class("proj-computed-lbl");
    m_proj_pace_lbl.set_halign(Gtk::Align::END);
    lb->append(*row("Pace needed / day", m_proj_pace_lbl));

    m_proj_goals_revealer.set_child(*lb);
    m_proj_outer.append(m_proj_goals_revealer);
  }

  m_proj_scroll.set_child(m_proj_outer);

  // ── Pinned statistics button — always visible below the scroll ────────────
  auto *stats_btn =
      Gtk::make_managed<Gtk::Button>("View Statistics & Burnup Chart…");
  stats_btn->add_css_class("pill-btn");
  stats_btn->add_css_class("pill-btn-primary");
  stats_btn->set_halign(Gtk::Align::CENTER);
  stats_btn->set_margin_top(3);
  stats_btn->set_margin_bottom(12);
  stats_btn->signal_clicked().connect([this]() {
    auto *top = dynamic_cast<Gtk::Window *>(get_root());
    if (!top)
      return;
    if (!m_goal_dialog)
      m_goal_dialog =
          std::make_unique<ProjectGoalDialog>(*top, m_model, m_prefs);
    m_goal_dialog->refresh();
    m_goal_dialog->present();
  });

  m_proj_wrapper.set_orientation(Gtk::Orientation::VERTICAL);
  m_proj_wrapper.append(m_proj_scroll);
  m_proj_wrapper.append(*stats_btn);

  m_stack.add(m_proj_wrapper, "project");
}

// ─────────────────────────────────────────────────────────────────────────────
// Cover image
// ─────────────────────────────────────────────────────────────────────────────

void Inspector::load_cover_from_path(const std::string &path) {
  try {
    // Load original image via pixbuf
    auto orig = Gdk::Pixbuf::create_from_file(path);
    if (!orig) return;

    // Scale to 384×576 (4×6 inches at 96dpi) preserving aspect ratio
    const int TARGET_W = 384;
    const int TARGET_H = 576;
    double scale = std::min((double)TARGET_W / orig->get_width(),
                            (double)TARGET_H / orig->get_height());
    int tw = (int)(orig->get_width()  * scale);
    int th = (int)(orig->get_height() * scale);
    auto thumb = orig->scale_simple(tw, th, Gdk::InterpType::BILINEAR);
    if (!thumb) return;

    // Encode thumbnail as base64 PNG
    gchar *buf = nullptr;
    gsize  len = 0;
    GError *err = nullptr;
    gdk_pixbuf_save_to_buffer(thumb->gobj(), &buf, &len, "png", &err, nullptr);
    if (err) { g_error_free(err); return; }
    std::string b64 = Glib::Base64::encode(
        std::string(buf, len));
    g_free(buf);

    m_model.cover_image_path = path;
    m_model.cover_thumbnail  = b64;
    m_model.mark_modified();

    m_cover_pixbuf = thumb;
    m_cover_path_entry.set_text(path);
    m_cover_thumbnail_area.queue_draw();
  } catch (...) {}
}

void Inspector::refresh_cover_thumbnail() {
  if (m_model.cover_thumbnail.empty()) {
    m_cover_pixbuf.reset();
    m_cover_path_entry.set_text("");
    m_cover_thumbnail_area.queue_draw();
    return;
  }
  m_cover_path_entry.set_text(m_model.cover_image_path);
  try {
    std::string png_data = Glib::Base64::decode(m_model.cover_thumbnail);
    GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
    gdk_pixbuf_loader_write(loader, (const guchar *)png_data.data(),
                            png_data.size(), nullptr);
    gdk_pixbuf_loader_close(loader, nullptr);
    GdkPixbuf *pb = gdk_pixbuf_loader_get_pixbuf(loader);
    if (pb)
      m_cover_pixbuf = Glib::wrap(pb, true);
    g_object_unref(loader);
  } catch (...) { m_cover_pixbuf.reset(); }
  m_cover_thumbnail_area.queue_draw();
}

void Inspector::show_cover_dialog() {
  auto *win = dynamic_cast<Gtk::Window *>(get_root());
  if (!win) return;

  auto *dlg = new Gtk::Window();
  dlg->set_title("Cover Image");
  dlg->set_transient_for(*win);
  dlg->set_modal(true);
  dlg->set_default_size(420, 640);
  dlg->set_resizable(false);

  auto *outer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
  outer->set_margin_top(16);
  outer->set_margin_bottom(16);
  outer->set_margin_start(16);
  outer->set_margin_end(16);

  // Full thumbnail display
  auto *img_area = Gtk::make_managed<Gtk::DrawingArea>();
  img_area->set_size_request(384, 576);
  img_area->set_halign(Gtk::Align::CENTER);
  auto cover_pb = m_cover_pixbuf; // capture by value
  img_area->set_draw_func([cover_pb](const Cairo::RefPtr<Cairo::Context> &cr,
                                     int w, int h) {
    if (!cover_pb) return;
    double scale = std::min((double)w / cover_pb->get_width(),
                            (double)h / cover_pb->get_height());
    int dw = (int)(cover_pb->get_width()  * scale);
    int dh = (int)(cover_pb->get_height() * scale);
    auto scaled = cover_pb->scale_simple(dw, dh, Gdk::InterpType::BILINEAR);
    Gdk::Cairo::set_source_pixbuf(cr, scaled,
                                  (w - dw) / 2.0, (h - dh) / 2.0);
    cr->paint();
  });
  outer->append(*img_area);

  // Buttons: Replace | Remove | Close
  auto *btn_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
  btn_row->set_halign(Gtk::Align::END);

  auto *replace_btn = Gtk::make_managed<Gtk::Button>("Replace…");
  replace_btn->add_css_class("pill-btn");
  replace_btn->signal_clicked().connect([this, dlg]() {
    dlg->close();
    // Trigger browse after dialog closes
    Glib::signal_idle().connect_once([this]() {
      auto *win2 = dynamic_cast<Gtk::Window *>(get_root());
      if (!win2) return;
      auto chooser = Gtk::FileChooserNative::create(
          "Select Cover Image", *win2, Gtk::FileChooser::Action::OPEN,
          "Open", "Cancel");
      auto filter = Gtk::FileFilter::create();
      filter->set_name("Images (JPEG, PNG)");
      filter->add_mime_type("image/jpeg");
      filter->add_mime_type("image/png");
      chooser->add_filter(filter);
      chooser->signal_response().connect([this, chooser](int response) {
        if (response == Gtk::ResponseType::ACCEPT) {
          auto file = chooser->get_file();
          if (file) load_cover_from_path(file->get_path());
        }
      });
      chooser->show();
    });
  });

  auto *remove_btn = Gtk::make_managed<Gtk::Button>("Remove");
  remove_btn->add_css_class("destructive-action");
  remove_btn->signal_clicked().connect([this, dlg]() {
    m_model.cover_image_path = "";
    m_model.cover_thumbnail  = "";
    m_model.mark_modified();
    m_cover_pixbuf.reset();
    m_cover_path_entry.set_text("");
    m_cover_thumbnail_area.queue_draw();
    dlg->close();
  });

  auto *close_btn = Gtk::make_managed<Gtk::Button>("Close");
  close_btn->signal_clicked().connect([dlg]() { dlg->close(); });

  btn_row->append(*replace_btn);
  btn_row->append(*remove_btn);
  btn_row->append(*close_btn);
  outer->append(*btn_row);

  dlg->set_child(*outer);
  dlg->signal_close_request().connect([dlg]() -> bool {
    delete dlg; return false;
  }, false);
  dlg->present();
}

void Inspector::update_barcode_thumbnail() {
  // Show thumbnail only when ISBN is valid (EAN-13 or ISBN-10)
  std::string digits = BarcodeGenerator::normalise(m_model.isbn);
  bool valid = BarcodeGenerator::is_valid_ean13(digits) ||
               BarcodeGenerator::is_valid_isbn10(digits);
  m_proj_barcode_btn.set_visible(valid);
  if (valid) {
    // Regenerate SVG if not yet generated or ISBN changed
    if (m_model.barcode_svg.empty()) {
      m_model.barcode_svg =
          BarcodeGenerator::generate_svg_from_isbn(m_model.isbn);
      m_model.mark_modified();
    }
    m_proj_barcode_btn.queue_draw();
  }
}

void Inspector::refresh_project_meta() {
  m_proj_title_entry.set_text(m_model.project_title);
  m_proj_author_entry.set_text(m_model.author);
  if (m_proj_genre_dropdown) {
    guint gidx = 0;
    auto *sl = dynamic_cast<Gtk::StringList *>(
        m_proj_genre_dropdown->get_model().get());
    if (sl && !m_model.genre.empty())
      for (guint i = 1; i < sl->get_n_items(); ++i)
        if (std::string(sl->get_string(i)) == m_model.genre) {
          gidx = i;
          break;
        }
    m_proj_genre_dropdown->set_selected(gidx);
  }
  m_proj_synopsis_buffer->set_text(m_model.project_synopsis);
  m_proj_publisher_entry.set_text(m_model.publisher);
  m_proj_isbn_entry.set_text(m_model.isbn);
  m_proj_year_entry.set_text(m_model.year);
  m_proj_daily_target_entry.set_text(
      m_model.daily_target > 0 ? std::to_string(m_model.daily_target) : "");
  m_proj_word_target_spin.set_value(
      m_model.project_word_target > 0 ? m_model.project_word_target : 80000);
  m_proj_due_date_entry.set_text(m_model.due_date);
  refresh_goal_computed();
  update_barcode_thumbnail();
  refresh_cover_thumbnail();
}

void Inspector::refresh_goal_computed() {
  int target = m_model.project_word_target;
  int written = 0;
  for (const auto &r : m_model.daily_history)
    written += r.words;
  int remaining = std::max(0, target - written);

  const std::string &due = m_model.due_date;
  if (target <= 0 || due.size() < 10) {
    m_proj_pace_lbl.set_text("—");
    return;
  }

  std::tm t_due{};
  {
    std::istringstream ss(due);
    ss >> std::get_time(&t_due, "%Y-%m-%d");
    if (ss.fail()) {
      m_proj_pace_lbl.set_text("—");
      return;
    }
  }
  std::time_t ts_due = std::mktime(&t_due);
  std::time_t ts_now = std::time(nullptr);
  std::tm *t_now_ptr = std::localtime(&ts_now);
  t_now_ptr->tm_hour = 0;
  t_now_ptr->tm_min = 0;
  t_now_ptr->tm_sec = 0;
  ts_now = std::mktime(t_now_ptr);

  int days = (int)std::round(std::difftime(ts_due, ts_now) / 86400.0);
  if (days <= 0) {
    m_proj_pace_lbl.set_text("Overdue");
    return;
  }
  if (remaining == 0) {
    m_proj_pace_lbl.set_text("Done ✓");
    return;
  }

  int pace = (int)std::ceil((double)remaining / days);
  m_proj_pace_lbl.set_text(std::to_string(pace) + " / day");
}

void Inspector::notify_meta_changed() {
  if (m_on_meta_changed)
    m_on_meta_changed(m_current_node);
}

void Inspector::notify_content_changed() {
  if (m_on_content_changed)
    m_on_content_changed(m_current_node);
}

void Inspector::notify_toast(const std::string &msg) {
  if (m_on_toast)
    m_on_toast(msg);
}

// ─────────────────────────────────────────────────────────────────────────────
// Annotations tab
// ─────────────────────────────────────────────────────────────────────────────

void Inspector::build_annotations_tab() {
  // Annotations are now embedded in the Notes tab via m_notes_paned.
  // This function is kept for link compatibility but does nothing.
}

void Inspector::refresh_annotations() {
  // Clear existing cards
  while (auto *child = m_ann_box.get_first_child())
    m_ann_box.remove(*child);

  // ── JV mode: aggregate annotations from all segments ─────────────────────
  if (m_in_jv_mode) {
    int total = 0;
    for (auto *n : m_jv_nodes)
      if (n)
        total += (int)n->annotations.size();

    if (total == 0) {
      auto *lbl = Gtk::make_managed<Gtk::Label>("No annotations in this selection.");
      lbl->add_css_class("dim-label");
      lbl->set_justify(Gtk::Justification::CENTER);
      lbl->set_margin_top(24);
      m_ann_box.append(*lbl);
      return;
    }

    for (auto *node : m_jv_nodes) {
      if (!node || node->annotations.empty())
        continue;

      // Node label header
      auto *node_lbl = Gtk::make_managed<Gtk::Label>(node->title);
      node_lbl->add_css_class("pref-group-title");
      node_lbl->set_halign(Gtk::Align::START);
      node_lbl->set_margin_top(3);
      node_lbl->set_margin_start(8);
      node_lbl->set_margin_bottom(2);
      m_ann_box.append(*node_lbl);

      for (const auto &ann : node->annotations) {
        auto *card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
        card->add_css_class("annotation-card");
        card->set_margin_top(6);
        card->set_margin_bottom(0);
        card->set_margin_start(8);
        card->set_margin_end(8);

        auto *hdr = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        hdr->set_margin_top(6);
        hdr->set_margin_start(8);
        hdr->set_margin_end(8);

        auto *swatch = Gtk::make_managed<Gtk::Label>(" ");
        swatch->set_size_request(12, 12);
        {
          auto css = Gtk::CssProvider::create();
          css->load_from_data(std::string("label { background: ") +
                              ann.color_hex + "; border-radius: 3px; }");
          swatch->get_style_context()->add_provider(
              css, GTK_STYLE_PROVIDER_PRIORITY_USER);
        }
        swatch->set_valign(Gtk::Align::CENTER);
        hdr->append(*swatch);

        auto *kind_lbl = Gtk::make_managed<Gtk::Label>(ann.kind);
        kind_lbl->add_css_class("annotation-kind");
        kind_lbl->set_halign(Gtk::Align::START);
        kind_lbl->set_hexpand(true);
        hdr->append(*kind_lbl);

        if (!ann.created_at.empty()) {
          auto *date_lbl =
              Gtk::make_managed<Gtk::Label>(ann.created_at.substr(0, 10));
          date_lbl->add_css_class("dim-label");
          date_lbl->set_halign(Gtk::Align::END);
          hdr->append(*date_lbl);
        }
        card->append(*hdr);

        std::string preview = ann.text;
        if (preview.size() > 120)
          preview = preview.substr(0, 120) + "…";
        auto *text_lbl = Gtk::make_managed<Gtk::Label>(preview);
        text_lbl->set_wrap(true);
        text_lbl->set_xalign(0.0f);
        text_lbl->set_halign(Gtk::Align::FILL);
        text_lbl->set_margin_start(8);
        text_lbl->set_margin_end(8);
        text_lbl->set_margin_bottom(4);
        card->append(*text_lbl);

        // Buttons: Go | Edit | Delete — all fully functional, routed to node
        auto *btn_row =
            Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
        btn_row->set_halign(Gtk::Align::END);
        btn_row->set_margin_end(8);
        btn_row->set_margin_bottom(6);

        int aid = ann.id;

        auto *go_btn = Gtk::make_managed<Gtk::Button>("Go");
        go_btn->add_css_class("flat");
        go_btn->set_tooltip_text("Scroll to annotation in editor");
        go_btn->signal_clicked().connect([this, aid]() {
          if (on_scroll_to_annotation)
            on_scroll_to_annotation(aid);
        });

        auto *edit_btn = Gtk::make_managed<Gtk::Button>("Edit");
        edit_btn->add_css_class("flat");
        edit_btn->set_tooltip_text("Edit this annotation");

        auto *del_btn = Gtk::make_managed<Gtk::Button>();
        del_btn->set_icon_name("list-remove-symbolic");
        del_btn->add_css_class("flat");
        del_btn->set_tooltip_text("Delete annotation");
        del_btn->signal_clicked().connect([this, node, aid]() {
          if (on_delete_annotation_from_node)
            on_delete_annotation_from_node(node, aid);
          refresh_annotations();
        });

        btn_row->append(*go_btn);
        btn_row->append(*edit_btn);
        btn_row->append(*del_btn);
        card->append(*btn_row);

        // Inline edit revealer
        auto *edit_rev = Gtk::make_managed<Gtk::Revealer>();
        edit_rev->set_transition_type(Gtk::RevealerTransitionType::SLIDE_DOWN);
        edit_rev->set_transition_duration(150);
        edit_rev->set_reveal_child(false);

        auto *edit_box =
            Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
        edit_box->set_margin_start(8);
        edit_box->set_margin_end(8);
        edit_box->set_margin_bottom(3);

        auto *edit_tv = Gtk::make_managed<Gtk::TextView>();
        edit_tv->set_wrap_mode(Gtk::WrapMode::WORD_CHAR);
        edit_tv->add_css_class("annotation-entry");
        edit_tv->get_buffer()->set_text(ann.text);
        auto *edit_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
        edit_scroll->set_policy(Gtk::PolicyType::NEVER,
                                Gtk::PolicyType::AUTOMATIC);
        edit_scroll->set_min_content_height(60);
        edit_scroll->set_max_content_height(120);
        edit_scroll->set_child(*edit_tv);
        edit_box->append(*edit_scroll);

        auto *save_row =
            Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
        save_row->set_halign(Gtk::Align::END);
        auto *cancel_btn = Gtk::make_managed<Gtk::Button>("Cancel");
        cancel_btn->add_css_class("flat");
        auto *save_btn = Gtk::make_managed<Gtk::Button>("Save");
        save_btn->add_css_class("suggested-action");
        save_row->append(*cancel_btn);
        save_row->append(*save_btn);
        edit_box->append(*save_row);
        edit_rev->set_child(*edit_box);
        card->append(*edit_rev);

        edit_btn->signal_clicked().connect([edit_rev, edit_tv, aid, this]() {
          bool now = !edit_rev->get_reveal_child();
          edit_rev->set_reveal_child(now);
          if (now)
            edit_tv->grab_focus();
          if (on_scroll_to_annotation)
            on_scroll_to_annotation(aid);
        });
        cancel_btn->signal_clicked().connect(
            [edit_rev]() { edit_rev->set_reveal_child(false); });
        save_btn->signal_clicked().connect(
            [this, node, edit_tv, aid, edit_rev]() {
              std::string new_text = edit_tv->get_buffer()->get_text();
              if (!new_text.empty()) {
                // Find kind/color from the owning node for the edit call
                std::string kind, color_hex;
                for (const auto &a : node->annotations)
                  if (a.id == aid) { kind = a.kind; color_hex = a.color_hex; break; }
                if (on_edit_annotation_on_node)
                  on_edit_annotation_on_node(node, aid, new_text, kind, color_hex);
              }
              edit_rev->set_reveal_child(false);
              refresh_annotations();
            });

        auto *sep =
            Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
        sep->set_margin_top(6);
        m_ann_box.append(*card);
        m_ann_box.append(*sep);
      }
    }
    return;
  }

  // ── Single-node mode ──────────────────────────────────────────────────────
  if (!m_current_node) {
    auto *lbl = Gtk::make_managed<Gtk::Label>("No document loaded.");
    lbl->add_css_class("dim-label");
    lbl->set_margin_top(24);
    m_ann_box.append(*lbl);
    return;
  }

  const auto &anns = m_current_node->annotations;
  if (anns.empty()) {
    auto *lbl = Gtk::make_managed<Gtk::Label>(
        "No annotations.\nSelect text and right-click\nto add one.");
    lbl->add_css_class("dim-label");
    lbl->set_justify(Gtk::Justification::CENTER);
    lbl->set_margin_top(24);
    m_ann_box.append(*lbl);
    return;
  }

  for (const auto &ann : anns) {
    // ── Card ────────────────────────────────────────────────────────────────
    auto *card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    card->add_css_class("annotation-card");
    card->set_margin_top(6);
    card->set_margin_bottom(0);
    card->set_margin_start(8);
    card->set_margin_end(8);

    // Header row: color swatch + kind badge + id + timestamp
    auto *hdr = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    hdr->set_margin_top(6);
    hdr->set_margin_start(8);
    hdr->set_margin_end(8);

    // Color swatch
    auto *swatch = Gtk::make_managed<Gtk::Label>(" ");
    swatch->set_size_request(12, 12);
    {
      auto css = Gtk::CssProvider::create();
      css->load_from_data(std::string("label { background: ") + ann.color_hex +
                          "; border-radius: 3px; }");
      swatch->get_style_context()->add_provider(
          css, GTK_STYLE_PROVIDER_PRIORITY_USER);
    }
    swatch->set_valign(Gtk::Align::CENTER);
    hdr->append(*swatch);

    // Kind label
    auto *kind_lbl = Gtk::make_managed<Gtk::Label>(ann.kind);
    kind_lbl->add_css_class("annotation-kind");
    kind_lbl->set_halign(Gtk::Align::START);
    kind_lbl->set_hexpand(true);
    hdr->append(*kind_lbl);

    // Date (just show date part of ISO timestamp)
    if (!ann.created_at.empty()) {
      auto *date_lbl =
          Gtk::make_managed<Gtk::Label>(ann.created_at.substr(0, 10));
      date_lbl->add_css_class("dim-label");
      date_lbl->set_halign(Gtk::Align::END);
      hdr->append(*date_lbl);
    }

    card->append(*hdr);

    // Annotation text (first 120 chars)
    std::string preview = ann.text;
    if (preview.size() > 120)
      preview = preview.substr(0, 120) + "…";
    auto *text_lbl = Gtk::make_managed<Gtk::Label>(preview);
    text_lbl->set_wrap(true);
    text_lbl->set_xalign(0.0f);
    text_lbl->set_halign(Gtk::Align::FILL);
    text_lbl->set_margin_start(8);
    text_lbl->set_margin_end(8);
    text_lbl->set_margin_bottom(4);
    card->append(*text_lbl);

    // Buttons row: Go | Edit | Delete
    auto *btn_row =
        Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    btn_row->set_halign(Gtk::Align::END);
    btn_row->set_margin_end(8);
    btn_row->set_margin_bottom(6);

    auto *go_btn = Gtk::make_managed<Gtk::Button>("Go");
    go_btn->add_css_class("flat");
    go_btn->set_tooltip_text("Scroll to annotation in editor");
    int aid = ann.id;
    go_btn->signal_clicked().connect([this, aid]() {
      if (on_scroll_to_annotation)
        on_scroll_to_annotation(aid);
    });

    auto *edit_btn = Gtk::make_managed<Gtk::Button>("Edit");
    edit_btn->add_css_class("flat");
    edit_btn->set_tooltip_text("Edit this annotation");

    auto *del_btn = Gtk::make_managed<Gtk::Button>();
    del_btn->set_icon_name("list-remove-symbolic");
    del_btn->add_css_class("flat");
    del_btn->set_tooltip_text("Delete annotation");
    del_btn->signal_clicked().connect([this, aid]() {
      if (on_delete_annotation)
        on_delete_annotation(aid);
      refresh_annotations();
    });

    btn_row->append(*go_btn);
    btn_row->append(*edit_btn);
    btn_row->append(*del_btn);
    card->append(*btn_row);

    // Inline edit revealer
    auto *edit_rev = Gtk::make_managed<Gtk::Revealer>();
    edit_rev->set_transition_type(Gtk::RevealerTransitionType::SLIDE_DOWN);
    edit_rev->set_transition_duration(150);
    edit_rev->set_reveal_child(false);

    auto *edit_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    edit_box->set_margin_start(8);
    edit_box->set_margin_end(8);
    edit_box->set_margin_bottom(3);

    auto *edit_tv = Gtk::make_managed<Gtk::TextView>();
    edit_tv->set_wrap_mode(Gtk::WrapMode::WORD_CHAR);
    edit_tv->add_css_class("annotation-entry");
    edit_tv->get_buffer()->set_text(ann.text);
    auto *edit_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    edit_scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    edit_scroll->set_min_content_height(60);
    edit_scroll->set_max_content_height(120);
    edit_scroll->set_child(*edit_tv);
    edit_box->append(*edit_scroll);

    auto *save_row =
        Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    save_row->set_halign(Gtk::Align::END);
    auto *cancel_btn = Gtk::make_managed<Gtk::Button>("Cancel");
    cancel_btn->add_css_class("flat");
    auto *save_btn = Gtk::make_managed<Gtk::Button>("Save");
    save_btn->add_css_class("suggested-action");
    save_row->append(*cancel_btn);
    save_row->append(*save_btn);
    edit_box->append(*save_row);
    edit_rev->set_child(*edit_box);
    card->append(*edit_rev);

    // Wire edit button
    edit_btn->signal_clicked().connect([edit_rev, edit_tv, aid, this]() {
      bool now = !edit_rev->get_reveal_child();
      edit_rev->set_reveal_child(now);
      if (now)
        edit_tv->grab_focus();
      if (on_scroll_to_annotation)
        on_scroll_to_annotation(aid);
    });
    cancel_btn->signal_clicked().connect(
        [edit_rev]() { edit_rev->set_reveal_child(false); });
    save_btn->signal_clicked().connect([this, edit_tv, aid, edit_rev]() {
      std::string new_text = edit_tv->get_buffer()->get_text();
      if (!new_text.empty()) {
        if (on_edit_annotation_text)
          on_edit_annotation_text(aid, new_text);
      }
      edit_rev->set_reveal_child(false);
      refresh_annotations();
    });

    // Add separator
    auto *sep = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
    sep->set_margin_top(6);

    m_ann_box.append(*card);
    m_ann_box.append(*sep);
  }
}

// ── Tools-menu entry points ───────────────────────────────────────────────────

void Inspector::open_annotation_report() {
  auto *root = dynamic_cast<Gtk::Window *>(get_root());
  if (!root) return;
  if (!m_ann_report)
    m_ann_report = std::make_unique<AnnotationReportDialog>(*root, m_model);
  else
    m_ann_report->refresh();
  m_ann_report->present();
}

void Inspector::open_barcode() {
  if (m_barcode_dialog) { m_barcode_dialog->present(); return; }
  auto *win = dynamic_cast<Gtk::Window *>(get_root());
  if (!win) return;
  m_barcode_dialog = std::make_unique<BarcodeDialog>(*win, m_model);
  m_barcode_dialog->set_svg_saved_callback([this]() {
    m_proj_barcode_btn.queue_draw();
  });
  m_barcode_dialog->set_hide_on_close(true);
  m_barcode_dialog->present();
}

void Inspector::open_project_goals() {
  auto *top = dynamic_cast<Gtk::Window *>(get_root());
  if (!top) return;
  if (!m_goal_dialog)
    m_goal_dialog = std::make_unique<ProjectGoalDialog>(*top, m_model, m_prefs);
  m_goal_dialog->refresh();
  m_goal_dialog->present();
}

} // namespace Folio
