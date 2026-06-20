// ─────────────────────────────────────────────────────────────────────────────
// Folio — SnapshotDialog.cpp
// ─────────────────────────────────────────────────────────────────────────────
#include <SnapshotDialog.hpp>
#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <sstream>

namespace Folio {

// ─────────────────────────────────────────────────────────────────────────────
// SetInfo helpers (file-local)
// ─────────────────────────────────────────────────────────────────────────────
struct SetInfo {
  int node_count = 0;
  int snap_count = 0;
};

static void scan_tree(const std::vector<BinderNode> &nodes,
                      std::map<std::string, SetInfo> &out) {
  for (const auto &n : nodes) {
    std::map<std::string, int> seen;
    for (const auto &s : n.snapshots)
      seen[s.name]++;
    for (const auto &[nm, cnt] : seen) {
      out[nm].snap_count += cnt;
      out[nm].node_count += 1;
    }
    if (!n.children.empty())
      scan_tree(n.children, out);
  }
}

static std::vector<std::tuple<std::string, int, int>>
collect_set_info(const DocumentModel &model) {
  std::map<std::string, SetInfo> info;
  scan_tree(model.root(Section::Manuscript), info);
  scan_tree(model.root(Section::Characters), info);
  scan_tree(model.root(Section::Places), info);
  std::vector<std::tuple<std::string, int, int>> result;
  for (const auto &[nm, si] : info)
    result.emplace_back(nm, si.node_count, si.snap_count);
  std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
    return std::get<0>(a) < std::get<0>(b);
  });
  return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────
SnapshotDialog::SnapshotDialog(Gtk::Window &parent, DocumentModel &model)
    : Gtk::Window(), m_model(model) {
  set_transient_for(parent);
  set_modal(true);
  set_title("Batch Snapshot");
  set_default_size(820, 580);
  set_resizable(true);

  // Escape closes
  auto kc = Gtk::EventControllerKey::create();
  kc->signal_key_pressed().connect(
      [this](guint kv, guint, Gdk::ModifierType) -> bool {
        if (kv == GDK_KEY_Escape) {
          close();
          return true;
        }
        return false;
      },
      false);
  add_controller(kc);

  // ── CSS ──────────────────────────────────────────────────────────────────
  auto css = Gtk::CssProvider::create();
  css->load_from_data(R"(
        .sdlg-toolbar, .sdlg-sets-bar, .sdlg-filter-bar {
            padding: 7px 14px;
            border-bottom: 1px solid alpha(currentColor,0.10);
        }
        .sdlg-sets-bar { background-color: alpha(currentColor,0.02); }
        .sdlg-footer   { padding: 8px 14px;
                         border-top: 1px solid alpha(currentColor,0.10); }
        .sdlg-hdr {
            background-color: alpha(currentColor,0.04);
            border-bottom: 1px solid alpha(currentColor,0.14);
            min-height: 26px;
            padding: 0 14px;
        }
        .sdlg-hdr-lbl {
            font-size: 11px; font-weight: bold;
            color: alpha(currentColor,0.45);
            padding: 0 6px;
        }
        .sdlg-vsep {
            min-width: 1px;
            background-color: alpha(currentColor,0.10);
            margin-top: 3px; margin-bottom: 3px;
        }
        .sdlg-row { min-height: 32px; padding: 0 14px; }
        .sdlg-row:nth-child(odd) { background-color: alpha(currentColor,0.02); }
        .sdlg-cell  { padding: 0 6px; }
        .sdlg-dot   { color: #e64553; font-size: 13px; }
        .sdlg-kind  {
            font-size: 12px;
            color: alpha(currentColor,0.45);
        }
        .sdlg-dim   { font-size: 12px; color: alpha(currentColor,0.45); }
        .sdlg-name  { font-size: 13px; }
        .sdlg-no-content { color: alpha(currentColor,0.30); font-style: italic; }
        .sdlg-delete-btn { color: #e64553; }
        .sdlg-delete-btn:hover { background-color: alpha(#e64553,0.12); }
    )");
  get_style_context()->add_provider_for_display(
      get_display(), css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);

  build_toolbar();
  build_sets_bar();
  build_filter_bar();
  build_header_row();
  build_list();
  build_footer();

  m_root.append(m_toolbar);
  m_root.append(m_sets_bar);
  m_root.append(m_filter_bar);
  m_root.append(m_header_row);

  // Toast overlay wraps the scroll area
  m_toast_label.add_css_class("editor-toast");
  m_toast_label.set_halign(Gtk::Align::CENTER);
  m_toast_label.set_valign(Gtk::Align::END);
  m_toast_label.set_margin_bottom(20);
  m_toast_revealer.set_child(m_toast_label);
  m_toast_revealer.set_transition_type(Gtk::RevealerTransitionType::CROSSFADE);
  m_toast_revealer.set_transition_duration(180);
  m_toast_revealer.set_halign(Gtk::Align::CENTER);
  m_toast_revealer.set_valign(Gtk::Align::END);
  m_toast_revealer.set_can_target(false);
  m_overlay.set_child(m_scroll);
  m_overlay.add_overlay(m_toast_revealer);
  m_overlay.set_vexpand(true);

  m_root.append(m_overlay);
  m_root.append(m_footer);
  set_child(m_root);

  update_status();
}

// ─────────────────────────────────────────────────────────────────────────────
// build_toolbar
// ─────────────────────────────────────────────────────────────────────────────
void SnapshotDialog::build_toolbar() {
  m_toolbar.add_css_class("sdlg-toolbar");
  m_toolbar.set_hexpand(true);

  auto *lbl = Gtk::make_managed<Gtk::Label>("Select nodes to snapshot:");
  lbl->set_hexpand(true);
  lbl->set_halign(Gtk::Align::START);
  m_toolbar.append(*lbl);

  for (auto &[btn, label, tip] :
       std::initializer_list<std::tuple<std::reference_wrapper<Gtk::Button>,
                                        const char *, const char *>>{
           {m_btn_modified, "Modified",
            "Select nodes changed since last snapshot"},
           {m_btn_all, "All", "Select every visible node"},
           {m_btn_none, "None", "Deselect all visible nodes"},
       }) {
    btn.get().set_label(label);
    btn.get().set_tooltip_text(tip);
    btn.get().add_css_class("pill-btn");
    m_toolbar.append(btn.get());
  }
  m_btn_modified.signal_clicked().connect([this]() { select_modified(); });
  m_btn_all.signal_clicked().connect([this]() { select_all(); });
  m_btn_none.signal_clicked().connect([this]() { select_none(); });

  auto *sep = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL);
  sep->set_margin_start(4);
  sep->set_margin_end(4);
  m_toolbar.append(*sep);

  auto *btn_active = Gtk::make_managed<Gtk::Button>("Sidebar Selection");
  btn_active->add_css_class("pill-btn");
  btn_active->set_tooltip_text("Select only the currently active binder node");
  btn_active->signal_clicked().connect([this]() {
    for (auto &r : m_rows)
      r.sw->set_active(false);
    auto *active = m_model.active_node();
    if (active)
      for (auto &r : m_rows)
        if (r.ref.node == active) {
          r.sw->set_active(true);
          break;
        }
    update_status();
  });
  m_toolbar.append(*btn_active);
}

// ─────────────────────────────────────────────────────────────────────────────
// build_sets_bar
// ─────────────────────────────────────────────────────────────────────────────
void SnapshotDialog::build_sets_bar() {
  m_sets_bar.add_css_class("sdlg-sets-bar");
  m_sets_bar.set_hexpand(true);

  auto *lbl = Gtk::make_managed<Gtk::Label>("Delete snapshot set:");
  lbl->add_css_class("sdlg-dim");
  m_sets_bar.append(*lbl);

  m_set_combo = Gtk::make_managed<Gtk::ComboBoxText>();
  m_set_combo->set_size_request(320, -1);
  m_set_combo->set_tooltip_text(
      "Choose a snapshot set to delete from every node");
  m_sets_bar.append(*m_set_combo);

  m_del_btn = Gtk::make_managed<Gtk::Button>("Delete Set");
  m_del_btn->set_icon_name("edit-delete-symbolic");
  m_del_btn->add_css_class("pill-btn");
  m_del_btn->add_css_class("sdlg-delete-btn");
  m_del_btn->set_tooltip_text(
      "Delete all snapshots in the selected set across every node");
  m_del_btn->signal_clicked().connect([this]() {
    if (!m_set_combo)
      return;
    std::string id = m_set_combo->get_active_id();
    if (id.empty())
      return;

    std::string title, detail;
    if (id == "__ALL__") {
      title = "Delete All Snapshots?";
      detail = "Every snapshot across all nodes in the project will be "
               "permanently removed. This cannot be undone.";
    } else {
      auto sets = collect_set_info(m_model);
      int sc = 0, nc = 0;
      for (const auto &[nm, n_cnt, s_cnt] : sets)
        if (nm == id) {
          sc = s_cnt;
          nc = n_cnt;
          break;
        }
      title = "Delete snapshot set \"" + id + "\"?";
      std::ostringstream det;
      det << "This will remove " << sc << " snapshot" << (sc == 1 ? "" : "s")
          << " across " << nc << " node" << (nc == 1 ? "" : "s")
          << ". This cannot be undone.";
      detail = det.str();
    }

    auto dlg = Gtk::AlertDialog::create(title);
    dlg->set_detail(detail);
    dlg->set_modal(true);
    dlg->set_buttons({"Cancel", "Delete"});
    dlg->set_cancel_button(0);
    dlg->set_default_button(0);
    dlg->choose(*this,
                [this, dlg, id](Glib::RefPtr<Gio::AsyncResult> &res) mutable {
                  int r = 0;
                  try {
                    r = dlg->choose_finish(res);
                  } catch (...) {
                  }
                  if (r == 1)
                    delete_snapshot_set(id);
                });
  });
  m_sets_bar.append(*m_del_btn);

  // Update button label whenever selection changes
  m_set_combo->signal_changed().connect([this]() {
    if (!m_del_btn || !m_set_combo)
      return;
    std::string id = m_set_combo->get_active_id();
    m_del_btn->set_label(id == "__ALL__" ? "Remove All" : "Remove Set");
  });

  auto *spacer = Gtk::make_managed<Gtk::Box>();
  spacer->set_hexpand(true);
  m_sets_bar.append(*spacer);

  refresh_sets_bar();
}

// ─────────────────────────────────────────────────────────────────────────────
// build_filter_bar
// ─────────────────────────────────────────────────────────────────────────────
void SnapshotDialog::build_filter_bar() {
  m_filter_bar.add_css_class("sdlg-filter-bar");
  m_filter_bar.set_hexpand(true);

  m_search = Gtk::make_managed<Gtk::SearchEntry>();
  m_search->set_placeholder_text("Search by name…");
  m_search->set_size_request(200, -1);
  m_search->signal_search_changed().connect([this]() { invalidate(); });
  m_filter_bar.append(*m_search);

  auto *sep1 = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL);
  sep1->set_margin_start(6);
  sep1->set_margin_end(6);
  m_filter_bar.append(*sep1);

  auto *fl = Gtk::make_managed<Gtk::Label>("Show:");
  fl->add_css_class("sdlg-dim");
  m_filter_bar.append(*fl);

  m_filter_combo = Gtk::make_managed<Gtk::ComboBoxText>();
  m_filter_combo->append("all", "All nodes");
  m_filter_combo->append("modified", "Modified only");
  m_filter_combo->append("has_snaps", "Has snapshots");
  m_filter_combo->append("no_snaps", "No snapshots");
  m_filter_combo->set_active_id("all");
  m_filter_combo->signal_changed().connect([this]() { invalidate(); });
  m_filter_bar.append(*m_filter_combo);

  auto *sep2 = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL);
  sep2->set_margin_start(6);
  sep2->set_margin_end(6);
  m_filter_bar.append(*sep2);

  auto *sl = Gtk::make_managed<Gtk::Label>("Sort:");
  sl->add_css_class("sdlg-dim");
  m_filter_bar.append(*sl);

  m_sort_combo = Gtk::make_managed<Gtk::ComboBoxText>();
  m_sort_combo->append("name", "Name");
  m_sort_combo->append("kind", "Kind");
  m_sort_combo->append("section", "Section");
  m_sort_combo->append("words", "Word count");
  m_sort_combo->append("modified", "Modified");
  m_sort_combo->append("snaps", "Most snapshots");
  m_sort_combo->set_active_id("name");
  m_sort_combo->signal_changed().connect([this]() { invalidate(); });
  m_filter_bar.append(*m_sort_combo);

  auto *spacer = Gtk::make_managed<Gtk::Box>();
  spacer->set_hexpand(true);
  m_filter_bar.append(*spacer);
}

// ─────────────────────────────────────────────────────────────────────────────
// build_header_row  — fixed column labels above the ListBox
// ─────────────────────────────────────────────────────────────────────────────
void SnapshotDialog::build_header_row() {
  m_header_row.add_css_class("sdlg-hdr");
  m_header_row.set_hexpand(true);

  auto add_vsep = [&]() {
    auto *s = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL);
    s->add_css_class("sdlg-vsep");
    m_header_row.append(*s);
  };

  // add_col uses same padding class as data cells so widths stay in sync
  auto add_col = [&](const std::string &text, int w, bool expand = false,
                     Gtk::Align ha = Gtk::Align::START) {
    auto *l = Gtk::make_managed<Gtk::Label>(text);
    l->add_css_class("sdlg-hdr-lbl");
    l->add_css_class("sdlg-cell"); // same 6px side padding as data cells
    l->set_halign(ha);
    l->set_valign(Gtk::Align::CENTER);
    if (expand)
      l->set_hexpand(true);
    else
      l->set_size_request(w, -1);
    m_header_row.append(*l);
  };

  add_col("Modified", W_DOT, false, Gtk::Align::CENTER);
  add_vsep();
  add_col("Name", 0, true, Gtk::Align::START);
  add_vsep();
  add_col("Kind", W_KIND, false, Gtk::Align::START);
  add_vsep();
  add_col("Section", W_SECTION);
  add_vsep();
  add_col("Words", W_WORDS, false, Gtk::Align::END);
  add_vsep();
  add_col("Snaps", W_SNAPS, false, Gtk::Align::END);
  add_vsep();
  add_col("Last Snapshot", W_LAST);
  add_vsep();
  add_col("Include", W_SWITCH, false, Gtk::Align::CENTER);
}

// ─────────────────────────────────────────────────────────────────────────────
// build_list  — populate ListBox with one row per node
// ─────────────────────────────────────────────────────────────────────────────
void SnapshotDialog::build_list() {
  m_scroll.set_vexpand(true);
  m_scroll.set_hexpand(true);
  m_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);

  m_list.set_selection_mode(Gtk::SelectionMode::NONE);
  m_list.set_hexpand(true);
  m_list.add_css_class("sdlg-list");

  // Native ListBox filter function
  m_list.set_filter_func(
      [this](Gtk::ListBoxRow *row) -> bool { return row_filter(row); });

  // Native ListBox sort function
  m_list.set_sort_func([this](Gtk::ListBoxRow *a, Gtk::ListBoxRow *b) -> int {
    return row_sort(a, b);
  });

  auto refs = m_model.collect_all_nodes();
  m_rows.clear();
  m_rows.reserve(refs.size());
  for (auto &ref : refs)
    m_rows.push_back({ref, nullptr, nullptr, nullptr, nullptr, nullptr});

  // Compute true modified state: compare current content to last snapshot.
  // content_modified is a runtime flag that doesn't survive project reload,
  // so we derive it here from the actual data.
  for (auto &rd : m_rows) {
    BinderNode *n = rd.ref.node;
    if (n->snapshots.empty()) {
      // Never snapshotted — modified if there's any content
      n->content_modified = !n->content.empty();
    } else {
      n->content_modified = (n->content != n->snapshots.back().content);
    }
  }

  for (auto &rd : m_rows) {
    auto *lr = make_list_row(rd);
    m_list.append(*lr);
  }

  m_scroll.set_child(m_list);
}

// ─────────────────────────────────────────────────────────────────────────────
// make_list_row
// ─────────────────────────────────────────────────────────────────────────────
Gtk::ListBoxRow *SnapshotDialog::make_list_row(RowData &rd) {
  BinderNode *n = rd.ref.node;

  auto *lr = Gtk::make_managed<Gtk::ListBoxRow>();
  lr->set_selectable(false);
  lr->set_activatable(false);
  // Store pointer back to RowData via set_data
  lr->set_data("rd", &rd);

  auto *hbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
  hbox->add_css_class("sdlg-row");
  hbox->set_hexpand(true);
  lr->set_child(*hbox);
  rd.list_row = lr;

  auto add_vsep = [&]() {
    auto *s = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL);
    s->add_css_class("sdlg-vsep");
    hbox->append(*s);
  };

  // ── Modified dot ─────────────────────────────────────────────────────────
  auto *dot = Gtk::make_managed<Gtk::Label>(n->content_modified ? "●" : "");
  dot->add_css_class("sdlg-dot");
  dot->set_halign(Gtk::Align::CENTER);
  dot->set_valign(Gtk::Align::CENTER);
  dot->set_size_request(W_DOT, ROW_H);
  dot->set_tooltip_text("Content changed since last snapshot");
  rd.mod_lbl = dot;
  hbox->append(*dot);
  add_vsep();

  // ── Name ─────────────────────────────────────────────────────────────────
  auto *name_lbl =
      Gtk::make_managed<Gtk::Label>(n->title.empty() ? "(Untitled)" : n->title);
  name_lbl->add_css_class("sdlg-cell");
  name_lbl->add_css_class("sdlg-name");
  if (n->content.empty() && n->kind != BinderKind::Group)
    name_lbl->add_css_class("sdlg-no-content");
  name_lbl->set_halign(Gtk::Align::START);
  name_lbl->set_valign(Gtk::Align::CENTER);
  name_lbl->set_ellipsize(Pango::EllipsizeMode::END);
  name_lbl->set_hexpand(true);
  hbox->append(*name_lbl);
  add_vsep();

  // ── Kind ─────────────────────────────────────────────────────────────────
  auto *kind_lbl = Gtk::make_managed<Gtk::Label>(kind_label(n->kind));
  kind_lbl->add_css_class("sdlg-cell");
  kind_lbl->add_css_class("sdlg-kind");
  kind_lbl->set_halign(Gtk::Align::START);
  kind_lbl->set_valign(Gtk::Align::CENTER);
  kind_lbl->set_size_request(W_KIND, ROW_H);
  hbox->append(*kind_lbl);
  add_vsep();

  // ── Section ──────────────────────────────────────────────────────────────
  auto *sec_lbl = Gtk::make_managed<Gtk::Label>(section_label(rd.ref.section));
  sec_lbl->add_css_class("sdlg-cell");
  sec_lbl->add_css_class("sdlg-dim");
  sec_lbl->set_halign(Gtk::Align::START);
  sec_lbl->set_valign(Gtk::Align::CENTER);
  sec_lbl->set_size_request(W_SECTION, ROW_H);
  hbox->append(*sec_lbl);
  add_vsep();

  // ── Words ────────────────────────────────────────────────────────────────
  int wc = n->word_count();
  auto *words_lbl =
      Gtk::make_managed<Gtk::Label>(wc > 0 ? std::to_string(wc) : "—");
  words_lbl->add_css_class("sdlg-cell");
  words_lbl->add_css_class("sdlg-dim");
  words_lbl->set_halign(Gtk::Align::END);
  words_lbl->set_valign(Gtk::Align::CENTER);
  words_lbl->set_size_request(W_WORDS, ROW_H);
  hbox->append(*words_lbl);
  add_vsep();

  // ── Snapshot count ───────────────────────────────────────────────────────
  int sc = (int)n->snapshots.size();
  auto *snaps_lbl =
      Gtk::make_managed<Gtk::Label>(sc > 0 ? std::to_string(sc) : "—");
  snaps_lbl->add_css_class("sdlg-cell");
  snaps_lbl->add_css_class("sdlg-dim");
  snaps_lbl->set_halign(Gtk::Align::END);
  snaps_lbl->set_valign(Gtk::Align::CENTER);
  snaps_lbl->set_size_request(W_SNAPS, ROW_H);
  snaps_lbl->set_tooltip_text("Number of saved snapshots for this node");
  rd.snap_count_lbl = snaps_lbl;
  hbox->append(*snaps_lbl);
  add_vsep();

  // ── Last snapshot ────────────────────────────────────────────────────────
  auto *last_lbl = Gtk::make_managed<Gtk::Label>(last_snapshot_str(*n));
  last_lbl->add_css_class("sdlg-cell");
  last_lbl->add_css_class("sdlg-dim");
  last_lbl->set_halign(Gtk::Align::START);
  last_lbl->set_valign(Gtk::Align::CENTER);
  last_lbl->set_ellipsize(Pango::EllipsizeMode::END);
  last_lbl->set_size_request(W_LAST, ROW_H);
  rd.last_lbl = last_lbl;
  hbox->append(*last_lbl);
  add_vsep();

  // ── Switch ───────────────────────────────────────────────────────────────
  auto *sw = Gtk::make_managed<Gtk::Switch>();
  sw->set_halign(Gtk::Align::CENTER);
  sw->set_valign(Gtk::Align::CENTER);
  sw->set_size_request(W_SWITCH, ROW_H);
  sw->set_active(false);
  sw->property_active().signal_changed().connect([this]() { update_status(); });
  rd.sw = sw;
  hbox->append(*sw);

  return lr;
}

// ─────────────────────────────────────────────────────────────────────────────
// build_footer
// ─────────────────────────────────────────────────────────────────────────────
void SnapshotDialog::build_footer() {
  m_footer.add_css_class("sdlg-footer");
  m_footer.set_hexpand(true);

  m_status_lbl.set_halign(Gtk::Align::START);
  m_status_lbl.set_hexpand(true);
  m_status_lbl.add_css_class("sdlg-dim");
  m_footer.append(m_status_lbl);

  auto *nl = Gtk::make_managed<Gtk::Label>("Name:");
  m_footer.append(*nl);

  m_name_entry.set_placeholder_text("Manual snapshot");
  m_name_entry.set_text("Manual snapshot");
  m_name_entry.set_size_request(200, -1);
  m_name_entry.signal_activate().connect([this]() { save_snapshots(); });
  m_footer.append(m_name_entry);

  m_btn_save.set_label("Save Snapshots");
  m_btn_save.set_icon_name("org.gnome.Settings-camera-access-symbolic");
  m_btn_save.add_css_class("pill-btn");
  m_btn_save.add_css_class("pill-btn-primary");
  m_btn_save.signal_clicked().connect([this]() { save_snapshots(); });
  m_footer.append(m_btn_save);

  auto *btn_cancel = Gtk::make_managed<Gtk::Button>("Close");
  btn_cancel->add_css_class("pill-btn");
  btn_cancel->signal_clicked().connect([this]() { close(); });
  m_footer.append(*btn_cancel);
}

// ─────────────────────────────────────────────────────────────────────────────
// Filter / Sort
// ─────────────────────────────────────────────────────────────────────────────
SnapshotDialog::RowData *SnapshotDialog::row_data_of(Gtk::ListBoxRow *r) {
  if (!r)
    return nullptr;
  return static_cast<RowData *>(r->get_data("rd"));
}

bool SnapshotDialog::row_filter(Gtk::ListBoxRow *row) {
  auto *rd = row_data_of(row);
  if (!rd)
    return true;
  const BinderNode *n = rd->ref.node;

  std::string filter = m_filter_combo ? m_filter_combo->get_active_id() : "all";
  if (filter == "modified" && !n->content_modified)
    return false;
  if (filter == "has_snaps" && n->snapshots.empty())
    return false;
  if (filter == "no_snaps" && !n->snapshots.empty())
    return false;

  if (m_search) {
    std::string q = m_search->get_text();
    if (!q.empty()) {
      std::string title = n->title;
      auto to_lower = [](std::string s) {
        for (auto &c : s)
          c = (char)std::tolower((unsigned char)c);
        return s;
      };
      if (to_lower(title).find(to_lower(q)) == std::string::npos)
        return false;
    }
  }
  return true;
}

int SnapshotDialog::row_sort(Gtk::ListBoxRow *a, Gtk::ListBoxRow *b) {
  auto *rda = row_data_of(a);
  auto *rdb = row_data_of(b);
  if (!rda || !rdb)
    return 0;
  const BinderNode *na = rda->ref.node;
  const BinderNode *nb = rdb->ref.node;

  std::string sort_by = m_sort_combo ? m_sort_combo->get_active_id() : "name";

  if (sort_by == "kind") {
    int diff = (int)na->kind - (int)nb->kind;
    if (diff != 0)
      return diff;
  }
  if (sort_by == "section") {
    int diff = (int)rda->ref.section - (int)rdb->ref.section;
    if (diff != 0)
      return diff;
  }
  if (sort_by == "words") {
    int diff = nb->word_count() - na->word_count(); // descending
    if (diff != 0)
      return diff;
  }
  if (sort_by == "modified") {
    int diff = (int)nb->content_modified - (int)na->content_modified;
    if (diff != 0)
      return diff;
  }
  if (sort_by == "snaps") {
    int diff = (int)nb->snapshots.size() - (int)na->snapshots.size();
    if (diff != 0)
      return diff;
  }
  // Fallback / primary for "name": alphabetical
  return na->title.compare(nb->title);
}

void SnapshotDialog::invalidate() {
  m_list.invalidate_filter();
  m_list.invalidate_sort();
  update_status();
}

// ─────────────────────────────────────────────────────────────────────────────
// Actions
// ─────────────────────────────────────────────────────────────────────────────
void SnapshotDialog::select_modified() {
  for (auto &r : m_rows)
    if (r.list_row && r.list_row->get_visible())
      r.sw->set_active(r.ref.node->content_modified);
  update_status();
}

void SnapshotDialog::select_all() {
  for (auto &r : m_rows)
    if (r.list_row && r.list_row->get_visible())
      r.sw->set_active(true);
  update_status();
}

void SnapshotDialog::select_none() {
  for (auto &r : m_rows)
    if (r.list_row && r.list_row->get_visible())
      r.sw->set_active(false);
  update_status();
}

void SnapshotDialog::save_snapshots() {
  std::string name = m_name_entry.get_text();
  if (name.empty())
    name = "Manual snapshot";

  int count = 0;
  for (auto &r : m_rows) {
    if (!r.sw->get_active())
      continue;
    r.ref.node->save_snapshot(name);
    ++count;
    if (r.mod_lbl)
      r.mod_lbl->set_text("");
    if (r.last_lbl)
      r.last_lbl->set_text(last_snapshot_str(*r.ref.node));
    if (r.snap_count_lbl) {
      int sc = (int)r.ref.node->snapshots.size();
      r.snap_count_lbl->set_text(sc > 0 ? std::to_string(sc) : "—");
    }
  }

  if (count > 0) {
    m_model.mark_modified();
    if (m_on_saved)
      m_on_saved();
    refresh_sets_bar();
    std::ostringstream oss;
    oss << "📷  Saved " << count << " snapshot" << (count == 1 ? "" : "s");
    show_toast(oss.str());
  } else {
    m_status_lbl.set_text(
        "No nodes selected — toggle switches to include nodes.");
  }
}

void SnapshotDialog::update_status() {
  int selected = 0, modified = 0, visible = 0, total = (int)m_rows.size();
  for (const auto &r : m_rows) {
    // A ListBoxRow is visible after filtering only when its parent is shown
    bool vis = r.list_row && r.list_row->get_child_visible();
    if (vis) {
      ++visible;
      if (r.sw && r.sw->get_active())
        ++selected;
    }
    if (r.ref.node->content_modified)
      ++modified;
  }
  std::ostringstream oss;
  if (visible < total)
    oss << visible << " of " << total << " shown  ·  ";
  oss << selected << " selected";
  if (modified > 0)
    oss << "  ·  " << modified << " modified";
  m_status_lbl.set_text(oss.str());
}

// ─────────────────────────────────────────────────────────────────────────────
// refresh_sets_bar
// ─────────────────────────────────────────────────────────────────────────────
void SnapshotDialog::refresh_sets_bar() {
  if (!m_set_combo)
    return;
  std::string prev = m_set_combo->get_active_id();
  m_set_combo->remove_all();

  auto sets = collect_set_info(m_model);
  int total_snaps = 0, total_nodes = 0;
  for (const auto &[nm, nc, sc] : sets) {
    total_snaps += sc;
    total_nodes += nc;
  }

  { // "All" entry
    std::ostringstream oss;
    if (total_snaps > 0)
      oss << "All Snapshots  —  " << total_snaps << " snapshot"
          << (total_snaps == 1 ? "" : "s") << " across " << total_nodes
          << " node" << (total_nodes == 1 ? "" : "s");
    else
      oss << "All Snapshots  —  none saved yet";
    m_set_combo->append("__ALL__", oss.str());
  }
  for (const auto &[nm, nc, sc] : sets) {
    std::ostringstream oss;
    oss << nm << "  —  " << sc << " snapshot" << (sc == 1 ? "" : "s")
        << " across " << nc << " node" << (nc == 1 ? "" : "s");
    m_set_combo->append(nm, oss.str());
  }

  if (!prev.empty())
    m_set_combo->set_active_id(prev);
  if (m_set_combo->get_active_row_number() < 0)
    m_set_combo->set_active(0);
  m_set_combo->set_sensitive(true);

  // Sync button label with current selection
  if (m_del_btn) {
    std::string id = m_set_combo->get_active_id();
    m_del_btn->set_label(id == "__ALL__" ? "Remove All" : "Remove Set");
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// delete_snapshot_set
// ─────────────────────────────────────────────────────────────────────────────
void SnapshotDialog::delete_snapshot_set(const std::string &set_name) {
  bool delete_all = (set_name == "__ALL__");
  int removed = 0;

  std::function<void(std::vector<BinderNode> &)> purge =
      [&](std::vector<BinderNode> &nodes) {
        for (auto &n : nodes) {
          int before = (int)n.snapshots.size();
          if (delete_all) {
            n.snapshots.clear();
          } else {
            n.snapshots.erase(std::remove_if(n.snapshots.begin(),
                                             n.snapshots.end(),
                                             [&](const Snapshot &s) {
                                               return s.name == set_name;
                                             }),
                              n.snapshots.end());
          }
          removed += before - (int)n.snapshots.size();
          if (!n.children.empty())
            purge(n.children);
        }
      };
  purge(m_model.root(Section::Manuscript));
  purge(m_model.root(Section::Characters));
  purge(m_model.root(Section::Places));

  for (auto &r : m_rows) {
    if (r.last_lbl)
      r.last_lbl->set_text(last_snapshot_str(*r.ref.node));
    if (r.snap_count_lbl) {
      int sc = (int)r.ref.node->snapshots.size();
      r.snap_count_lbl->set_text(sc > 0 ? std::to_string(sc) : "—");
    }
  }

  if (removed > 0) {
    m_model.mark_modified();
    if (m_on_saved)
      m_on_saved();
  }
  refresh_sets_bar();

  std::ostringstream oss;
  if (removed > 0) {
    if (delete_all)
      oss << "🗑  Removed all snapshots — " << removed << " snapshot"
          << (removed == 1 ? "" : "s") << " deleted";
    else
      oss << "🗑  Deleted \"" << set_name << "\" — " << removed << " snapshot"
          << (removed == 1 ? "" : "s") << " removed";
    show_toast(oss.str());
  } else {
    m_status_lbl.set_text("No snapshots found to remove.");
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// show_toast
// ─────────────────────────────────────────────────────────────────────────────
void SnapshotDialog::show_toast(const std::string &message) {
  m_toast_label.set_text(message);
  m_toast_revealer.set_reveal_child(true);
  if (m_toast_timer.connected())
    m_toast_timer.disconnect();
  m_toast_timer = Glib::signal_timeout().connect(
      [this]() {
        m_toast_revealer.set_reveal_child(false);
        return false;
      },
      2200);
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
std::string SnapshotDialog::kind_label(BinderKind k) {
  switch (k) {
  case BinderKind::Scene:
    return "Scene";
  case BinderKind::Group:
    return "Group";
  case BinderKind::Character:
    return "Character";
  case BinderKind::Place:
    return "Place";
  default:
    return "Node";
  }
}
std::string SnapshotDialog::section_label(Section s) {
  switch (s) {
  case Section::Manuscript:
    return "Manuscript";
  case Section::Characters:
    return "Characters";
  case Section::Places:
    return "Places";
  case Section::References:
    return "References";
  case Section::Templates:
    return "Templates";
  case Section::Trash:
    return "Trash";
  default:
    return "";
  }
}
std::string SnapshotDialog::last_snapshot_str(const BinderNode &n) {
  return n.snapshots.empty() ? "—" : n.snapshots.back().timestamp;
}

} // namespace Folio
