// ─────────────────────────────────────────────────────────────────────────────
// Folio — Editor_views.cpp  (VIEWS TU — split from Editor.cpp in s13)
//
// Alternate views: board (cards), grid/outline, joined view, focus mode.
// See the manifest banner in Editor.hpp for the full routing map.
// ─────────────────────────────────────────────────────────────────────────────

#include <Editor.hpp>
#include <Editor_internal.hpp>
#include <EditorHtmlSerializer.hpp>
#include <FolioLog.hpp>
#include <Iid.hpp>
#include <algorithm>
#include <cmath>
#include <gtkmm/eventcontrollermotion.h>
#include <set>
#include <string>

namespace Folio {
void Editor::rebuild_outline() {
  // Preserve selection across rebuilds by matching node pointers
  std::set<BinderNode *> prev_sel;
  for (size_t i = 0; i < m_grid_rows.size() && i < m_grid_selected.size(); ++i)
    if (m_grid_selected[i] && m_grid_rows[i])
      prev_sel.insert(m_grid_rows[i]);

  while (auto *c = m_outline_grid.get_first_child())
    m_outline_grid.remove(*c);
  m_grid_rows.clear();
  m_grid_selected.clear();
  m_grid_row_y.clear();
  m_grid_row_count = 0;

  enum Col {
    C_SEL = 0,
    C_TITLE,
    C_STATUS,
    C_POV,
    C_LABEL,
    C_WORDS,
    C_TARGET,
    C_INCLUDE,
    C_SYNOPSIS,
    NUM_COLS
  };
  const char *col_labels[] = {"",      "Title",  "Status", "POV",     "Label",
                              "Words", "Target", "\u2713", "Synopsis"};
  const int col_min_w[] = {28, 220, 110, 130, 90, 56, 60, 32, 220};
  // Sortable columns
  const bool col_sortable[] = {false, true,  true,  true, true,
                               true,  false, false, false};

  // Collect nodes based on section filters and current binder selection.
  // Section toggles (Manuscript/Characters/Places) in the grid toolbar
  // control which sections are included. m_grid_items from the binder
  // selection further filters within those sections.
  std::vector<BinderNode *> flat;

  if (m_grid_items.empty()) {
    // No binder selection — show hint instead of dumping all manuscript items
    m_grid_placeholder.set_visible(true);
    m_outline_scroll.set_visible(false);
    return;
  }
  m_grid_placeholder.set_visible(false);
  m_outline_scroll.set_visible(true);

  {
    // Binder selection present — filter by active sections
    std::set<BinderNode *> seen;
    for (const auto &item : m_grid_items) {
      // Only include if section is active
      if (item.section == Section::Manuscript && !m_grid_show_manuscript)
        continue;
      if (item.section == Section::Characters && !m_grid_show_characters)
        continue;
      if (item.section == Section::Places && !m_grid_show_places)
        continue;
      BinderNode *n = m_model.find_node_by_iid(item.iid);
      if (!n || !seen.insert(n).second)
        continue;
      flat.push_back(n);
      if (binder_kind_is_group(n->kind)) {
        for (auto &child : n->children)
          if (seen.insert(&child).second)
            flat.push_back(&child);
      }
    }
  }

  // Sort all rows uniformly — groups and scenes treated equally
  if (m_grid_sort_col > 0) {
    std::stable_sort(
        flat.begin(), flat.end(), [this](BinderNode *a, BinderNode *b) {
          bool asc = m_grid_sort_asc;
          auto cs = [asc](const std::string &x, const std::string &y) {
            return asc ? x < y : x > y;
          };
          auto ci = [asc](int x, int y) { return asc ? x < y : x > y; };
          switch (m_grid_sort_col) {
          case 1:
            return cs(a->title, b->title);
          case 2:
            return ci((int)a->status, (int)b->status);
          case 3:
            return cs(a->pov_character_name, b->pov_character_name);
          case 4:
            return ci(a->color_idx, b->color_idx);
          case 5:
            return ci(a->word_count(), b->word_count());
          default:
            return false;
          }
        });
  }

  m_grid_rows.resize(flat.size());
  m_grid_selected.resize(flat.size(), false);
  for (size_t i = 0; i < flat.size(); ++i) {
    m_grid_rows[i] = flat[i];
    m_grid_selected[i] = prev_sel.count(flat[i]) > 0;
  }

  // ── Shared dropdown data (built once, reused per row) ──────────────────
  // POV: "—" + character names from model
  auto pov_names = Gtk::StringList::create({"—"});
  {
    std::function<void(const std::vector<BinderNode> &)> collect_chars =
        [&](const std::vector<BinderNode> &nodes) {
          for (const auto &n : nodes) {
            if (n.kind == BinderKind::Character)
              pov_names->append(n.title);
            else if (n.kind == BinderKind::Group)
              collect_chars(n.children);
          }
        };
    collect_chars(m_model.characters);
  }
  // Label: "—" + tag color names
  auto label_names = Gtk::StringList::create({"—"});
  for (const auto &tc : m_prefs.tag_colors)
    label_names->append(tc.name);

  auto get_selected_nodes = [this]() {
    std::vector<BinderNode *> sel;
    for (size_t i = 0; i < m_grid_rows.size(); ++i)
      if (i < m_grid_selected.size() && m_grid_selected[i] && m_grid_rows[i])
        sel.push_back(m_grid_rows[i]);
    return sel;
  };

  // Attach focus-leave rebuild to any Entry — refreshes grid once editing done
  auto attach_rebuild = [this](Gtk::Entry *e) {
    auto fc = Gtk::EventControllerFocus::create();
    fc->signal_leave().connect([this]() {
      Glib::signal_idle().connect_once([this]() { rebuild_outline(); });
    });
    e->add_controller(fc);
  };

  // ── Column headers row 0 ──────────────────────────────────────────────────
  for (int c = 0; c < NUM_COLS; ++c) {
    if (c == C_SEL) {
      auto *cb = Gtk::make_managed<Gtk::CheckButton>();
      cb->set_halign(Gtk::Align::CENTER);
      cb->set_valign(Gtk::Align::CENTER);
      cb->set_margin_start(4);
      cb->set_margin_end(4);
      cb->set_tooltip_text("Select all / Deselect all");
      bool any = false, all_sel = !m_grid_rows.empty();
      for (size_t i = 0; i < m_grid_selected.size() && i < m_grid_rows.size();
           ++i) {
        if (m_grid_rows[i]) {
          if (m_grid_selected[i])
            any = true;
          else
            all_sel = false;
        }
      }
      auto sa_init = std::make_shared<bool>(true);
      cb->signal_toggled().connect([this, cb, sa_init]() {
        if (*sa_init)
          return;
        bool v = cb->get_active();
        for (size_t si = 0; si < m_grid_rows.size(); ++si)
          if (m_grid_rows[si])
            m_grid_selected[si] = v;
        rebuild_outline();
      });
      cb->set_active(any && all_sel);
      *sa_init = false;
      m_outline_grid.attach(*cb, c, 0);
      continue;
    }

    // Build label with sort indicator
    std::string lbl_text = col_labels[c];
    if (m_grid_sort_col == c)
      lbl_text += (m_grid_sort_asc ? " \u25b2" : " \u25bc");

    auto *btn = Gtk::make_managed<Gtk::Button>(lbl_text);
    btn->add_css_class("outline-col-header");
    btn->set_size_request(col_min_w[c], -1);
    btn->set_hexpand(c == C_TITLE || c == C_SYNOPSIS);

    if (!col_sortable[c]) {
      // Non-sortable non-batch headers just look like labels
      btn->set_sensitive(false);
    } else {
      int col = c;
      bool is_batch =
          (c == C_STATUS || c == C_POV || c == C_LABEL || c == C_INCLUDE);
      btn->signal_clicked().connect([this, btn, col, is_batch,
                                     get_selected_nodes]() {
        auto sel = get_selected_nodes();
        if (is_batch && !sel.empty()) {
          // Batch-set popover
          auto *pop = Gtk::make_managed<Gtk::Popover>();
          pop->set_parent(*btn);
          pop->set_autohide(true);
          auto *box =
              Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
          box->set_margin_top(8);
          box->set_margin_bottom(8);
          box->set_margin_start(8);
          box->set_margin_end(8);

          auto make_hdr = [](const char *t) {
            auto *l = Gtk::make_managed<Gtk::Label>(t);
            l->add_css_class("heading");
            l->set_halign(Gtk::Align::START);
            l->set_margin_bottom(4);
            return l;
          };

          if (col == C_STATUS) {
            box->append(*make_hdr("Set Status for selected"));
            struct SItem {
              const char *lbl;
              NodeStatus v;
            };
            for (auto &s :
                 std::vector<SItem>{{"— Untitled", NodeStatus::Untitled},
                                    {"Rough Draft", NodeStatus::RoughDraft},
                                    {"In Progress", NodeStatus::InProgress},
                                    {"Polished", NodeStatus::Polished},
                                    {"Skip", NodeStatus::Skip}}) {
              auto *b = Gtk::make_managed<Gtk::Button>(s.lbl);
              b->add_css_class("flat");
              NodeStatus v = s.v;
              b->signal_clicked().connect([this, sel, v, pop]() {
                for (auto *n : sel)
                  n->status = v;
                m_model.mark_modified();
                pop->popdown();
                rebuild_outline();
              });
              box->append(*b);
            }
          } else if (col == C_POV) {
            box->append(*make_hdr("Set POV for selected"));
            auto *entry = Gtk::make_managed<Gtk::Entry>();
            entry->set_placeholder_text("Character name…");
            if (!sel.empty())
              entry->set_text(sel[0]->pov_character_name);
            auto *ok = Gtk::make_managed<Gtk::Button>("Apply");
            ok->add_css_class("suggested-action");
            ok->signal_clicked().connect([this, sel, entry, pop]() {
              std::string v = entry->get_text();
              for (auto *n : sel)
                n->pov_character_name = v;
              m_model.mark_modified();
              pop->popdown();
              rebuild_outline();
            });
            box->append(*entry);
            box->append(*ok);
          } else if (col == C_LABEL) {
            box->append(*make_hdr("Set Label for selected"));
            auto *nb = Gtk::make_managed<Gtk::Button>("None");
            nb->add_css_class("flat");
            nb->signal_clicked().connect([this, sel, pop]() {
              for (auto *n : sel)
                n->color_idx = 0;
              m_model.mark_modified();
              pop->popdown();
              rebuild_outline();
            });
            box->append(*nb);
            for (int ci = 0; ci < (int)m_prefs.tag_colors.size(); ++ci) {
              auto *b =
                  Gtk::make_managed<Gtk::Button>(m_prefs.tag_colors[ci].name);
              b->add_css_class("flat");
              int idx = ci + 1;
              b->signal_clicked().connect([this, sel, idx, pop]() {
                for (auto *n : sel)
                  n->color_idx = idx;
                m_model.mark_modified();
                pop->popdown();
                rebuild_outline();
              });
              box->append(*b);
            }
          }
          pop->set_child(*box);
          pop->popup();
        } else {
          // Sort by this column
          if (m_grid_sort_col == col)
            m_grid_sort_asc = !m_grid_sort_asc;
          else {
            m_grid_sort_col = col;
            m_grid_sort_asc = true;
          }
          rebuild_outline();
        }
      });
    }
    m_outline_grid.attach(*btn, c, 0);
  }

  // Header separator
  auto *hsep = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
  m_outline_grid.attach(*hsep, 0, 1, NUM_COLS, 1);

  // ── Data rows ────────────────────────────────────────────────────────────
  // Track Y positions for marquee hit testing
  // Row 0 = headers (~36px), row 1 = separator (~1px), then data rows
  int nominal_y = 37;
  m_grid_row_y.resize(flat.size(), 0);

  for (size_t ri = 0; ri < flat.size(); ++ri) {
    BinderNode *node = flat[ri];
    int grid_row = (int)ri + 2;
    bool is_group = binder_kind_is_group(node->kind);
    bool sel = (ri < m_grid_selected.size() && m_grid_selected[ri]);

    m_grid_row_y[ri] = nominal_y;
    nominal_y += m_grid_row_h;

    // Row background: selected takes priority over alternating tint
    if (sel) {
      auto *bg = Gtk::make_managed<Gtk::Box>();
      bg->add_css_class("outline-row-selected");
      m_outline_grid.attach(*bg, 0, grid_row, NUM_COLS, 1);
    } else if (!is_group && (ri % 2 == 1)) {
      auto *bg = Gtk::make_managed<Gtk::Box>();
      bg->add_css_class("outline-row-alt");
      m_outline_grid.attach(*bg, 0, grid_row, NUM_COLS, 1);
    }

    // ── C_SEL ────────────────────────────────────────────────────────────
    auto *cb = Gtk::make_managed<Gtk::CheckButton>();
    cb->set_halign(Gtk::Align::CENTER);
    cb->set_margin_start(4);
    cb->set_margin_end(4);
    {
      size_t row_idx = ri;
      auto cb_init = std::make_shared<bool>(true);
      cb->signal_toggled().connect([this, row_idx, cb, cb_init]() {
        if (*cb_init)
          return;
        if (row_idx < m_grid_selected.size())
          m_grid_selected[row_idx] = cb->get_active();
      });
      cb->set_active(sel);
      *cb_init = false;
    }
    m_outline_grid.attach(*cb, C_SEL, grid_row);

    if (is_group) {
      // Group row: editable title (bold via CSS), all metadata columns, no
      // indent
      auto *gte = Gtk::make_managed<Gtk::Entry>();
      gte->set_has_frame(false);
      gte->add_css_class("outline-cell");
      gte->add_css_class("outline-group-title");
      gte->set_hexpand(true);
      gte->set_name(Folio::widget_name("grid-title", node->iid)); // s19
      auto gte_init = std::make_shared<bool>(true);
      gte->signal_changed().connect([this, gte, node, ri, gte_init]() {
        if (*gte_init)
          return;
        std::string v = gte->get_text();
        node->title = v;
        if (ri < m_grid_selected.size() && m_grid_selected[ri])
          for (size_t i = 0; i < m_grid_rows.size(); ++i)
            if (i != ri && i < m_grid_selected.size() && m_grid_selected[i] &&
                m_grid_rows[i])
              m_grid_rows[i]->title = v;
        m_model.mark_modified();
      });
      gte->set_text(node->title);
      *gte_init = false;
      attach_rebuild(gte);
      m_outline_grid.attach(*gte, C_TITLE, grid_row);

      // Status dropdown
      auto gsi = Gtk::StringList::create(
          {"—", "Rough", "In Progress", "Polished", "Skip"});
      auto *gsdd = Gtk::make_managed<Gtk::DropDown>(gsi);
      gsdd->add_css_class("outline-cell");
      gsdd->set_size_request(col_min_w[C_STATUS], -1);
      guint gss = 0;
      switch (node->status) {
      case NodeStatus::RoughDraft:
        gss = 1;
        break;
      case NodeStatus::InProgress:
        gss = 2;
        break;
      case NodeStatus::Polished:
        gss = 3;
        break;
      case NodeStatus::Skip:
        gss = 4;
        break;
      default:
        break;
      }
      gsdd->set_selected(gss);
      gsdd->property_selected().signal_changed().connect(
          [this, gsdd, node, ri]() {
            static const NodeStatus sv[] = {
                NodeStatus::Untitled, NodeStatus::RoughDraft,
                NodeStatus::InProgress, NodeStatus::Polished, NodeStatus::Skip};
            guint s2 = gsdd->get_selected();
            if (s2 >= 5)
              return;
            node->status = sv[s2];
            if (ri < m_grid_selected.size() && m_grid_selected[ri])
              for (size_t i = 0; i < m_grid_rows.size(); ++i)
                if (i != ri && i < m_grid_selected.size() &&
                    m_grid_selected[i] && m_grid_rows[i])
                  m_grid_rows[i]->status = sv[s2];
            m_model.mark_modified();
            Glib::signal_idle().connect_once([this]() { rebuild_outline(); });
          });
      m_outline_grid.attach(*gsdd, C_STATUS, grid_row);

      // POV — groups don't have POV so show empty placeholder
      m_outline_grid.attach(*Gtk::make_managed<Gtk::Box>(), C_POV, grid_row);

      // Label dropdown
      {
        auto *gldd = Gtk::make_managed<Gtk::DropDown>(label_names);
        gldd->add_css_class("outline-cell");
        gldd->set_size_request(col_min_w[C_LABEL], -1);
        gldd->set_selected((guint)node->color_idx);
        gldd->property_selected().signal_changed().connect(
            [this, gldd, node, ri]() {
              int idx = (int)gldd->get_selected();
              node->color_idx = idx;
              if (ri < m_grid_selected.size() && m_grid_selected[ri])
                for (size_t i = 0; i < m_grid_rows.size(); ++i)
                  if (i != ri && i < m_grid_selected.size() &&
                      m_grid_selected[i] && m_grid_rows[i])
                    m_grid_rows[i]->color_idx = idx;
              m_model.mark_modified();
              Glib::signal_idle().connect_once([this]() { rebuild_outline(); });
            });
        m_outline_grid.attach(*gldd, C_LABEL, grid_row);
      }

      // Words
      auto *gwc =
          Gtk::make_managed<Gtk::Label>(std::to_string(node->total_words()));
      gwc->add_css_class("stat-label");
      gwc->add_css_class("outline-cell");
      gwc->set_halign(Gtk::Align::END);
      m_outline_grid.attach(*gwc, C_WORDS, grid_row);

      // Target
      auto *gts = Gtk::make_managed<Gtk::SpinButton>();
      gts->set_adjustment(Gtk::Adjustment::create(0, 0, 999999, 100, 1000));
      gts->set_digits(0);
      gts->set_numeric(true);
      gts->add_css_class("outline-cell");
      gts->set_size_request(col_min_w[C_TARGET], -1);
      auto gts_init = std::make_shared<bool>(true);
      gts->signal_value_changed().connect([this, gts, node, ri, gts_init]() {
        if (*gts_init)
          return;
        int v = (int)gts->get_value();
        node->word_target = v;
        if (ri < m_grid_selected.size() && m_grid_selected[ri])
          for (size_t i = 0; i < m_grid_rows.size(); ++i)
            if (i != ri && i < m_grid_selected.size() && m_grid_selected[i] &&
                m_grid_rows[i])
              m_grid_rows[i]->word_target = v;
        m_model.mark_modified();
      });
      gts->set_value(node->word_target);
      *gts_init = false;
      m_outline_grid.attach(*gts, C_TARGET, grid_row);

      // Include
      auto *ginc = Gtk::make_managed<Gtk::CheckButton>();
      ginc->set_halign(Gtk::Align::CENTER);
      auto ginc_init = std::make_shared<bool>(true);
      ginc->signal_toggled().connect([this, ginc, node, ri, ginc_init]() {
        if (*ginc_init)
          return;
        bool v = ginc->get_active();
        node->include_in_export = v;
        if (ri < m_grid_selected.size() && m_grid_selected[ri])
          for (size_t i = 0; i < m_grid_rows.size(); ++i)
            if (i != ri && i < m_grid_selected.size() && m_grid_selected[i] &&
                m_grid_rows[i])
              m_grid_rows[i]->include_in_export = v;
        m_model.mark_modified();
      });
      ginc->set_active(node->include_in_export);
      *ginc_init = false;
      m_outline_grid.attach(*ginc, C_INCLUDE, grid_row);

      // Synopsis
      auto *gsyn = Gtk::make_managed<Gtk::Entry>();
      gsyn->set_has_frame(false);
      gsyn->add_css_class("outline-cell");
      gsyn->set_hexpand(true);
      auto gsyn_init = std::make_shared<bool>(true);
      gsyn->signal_changed().connect([this, gsyn, node, ri, gsyn_init]() {
        if (*gsyn_init)
          return;
        std::string v = gsyn->get_text();
        node->synopsis = v;
        if (ri < m_grid_selected.size() && m_grid_selected[ri])
          for (size_t i = 0; i < m_grid_rows.size(); ++i)
            if (i != ri && i < m_grid_selected.size() && m_grid_selected[i] &&
                m_grid_rows[i])
              m_grid_rows[i]->synopsis = v;
        m_model.mark_modified();
      });
      gsyn->set_text(node->synopsis);
      *gsyn_init = false;
      attach_rebuild(gsyn);
      m_outline_grid.attach(*gsyn, C_SYNOPSIS, grid_row);
      continue;
    }

    // ── C_TITLE ──────────────────────────────────────────────────────────
    auto *te = Gtk::make_managed<Gtk::Entry>();
    te->set_has_frame(false);
    te->add_css_class("outline-cell");
    te->set_hexpand(true);
    te->set_name(Folio::widget_name("grid-title", node->iid)); // s19
    auto te_init = std::make_shared<bool>(true);
    te->signal_changed().connect([this, te, node, ri, te_init]() {
      if (*te_init)
        return;
      std::string v = te->get_text();
      node->title = v;
      if (ri < m_grid_selected.size() && m_grid_selected[ri])
        for (size_t i = 0; i < m_grid_rows.size(); ++i)
          if (i != ri && i < m_grid_selected.size() && m_grid_selected[i] &&
              m_grid_rows[i])
            m_grid_rows[i]->title = v;
      m_model.mark_modified();
    });
    te->set_text(node->title);
    *te_init = false;
    attach_rebuild(te);
    m_outline_grid.attach(*te, C_TITLE, grid_row);

    // ── C_STATUS ─────────────────────────────────────────────────────────
    auto si = Gtk::StringList::create(
        {"—", "Rough", "In Progress", "Polished", "Skip"});
    auto *sdd = Gtk::make_managed<Gtk::DropDown>(si);
    sdd->add_css_class("outline-cell");
    sdd->set_size_request(col_min_w[C_STATUS], -1);
    guint ss = 0;
    switch (node->status) {
    case NodeStatus::RoughDraft:
      ss = 1;
      break;
    case NodeStatus::InProgress:
      ss = 2;
      break;
    case NodeStatus::Polished:
      ss = 3;
      break;
    case NodeStatus::Skip:
      ss = 4;
      break;
    default:
      break;
    }
    sdd->set_selected(ss);
    sdd->property_selected().signal_changed().connect([this, sdd, node, ri]() {
      static const NodeStatus sv[] = {
          NodeStatus::Untitled, NodeStatus::RoughDraft, NodeStatus::InProgress,
          NodeStatus::Polished, NodeStatus::Skip};
      guint s2 = sdd->get_selected();
      if (s2 >= 5)
        return;
      node->status = sv[s2];
      if (ri < m_grid_selected.size() && m_grid_selected[ri]) {
        for (size_t i = 0; i < m_grid_rows.size(); ++i)
          if (i != ri && i < m_grid_selected.size() && m_grid_selected[i] &&
              m_grid_rows[i])
            m_grid_rows[i]->status = sv[s2];
      }
      m_model.mark_modified();
      Glib::signal_idle().connect_once([this]() { rebuild_outline(); });
    });
    m_outline_grid.attach(*sdd, C_STATUS, grid_row);

    // ── C_POV ────────────────────────────────────────────────────────────
    auto *pov_dd = Gtk::make_managed<Gtk::DropDown>(pov_names);
    pov_dd->add_css_class("outline-cell");
    pov_dd->set_size_request(col_min_w[C_POV], -1);
    guint pov_sel = 0;
    for (guint pi = 1; pi < pov_names->get_n_items(); ++pi)
      if (pov_names->get_string(pi) ==
          Glib::ustring(node->pov_character_name)) {
        pov_sel = pi;
        break;
      }
    pov_dd->set_selected(pov_sel); // set BEFORE connecting signal
    pov_dd->property_selected().signal_changed().connect(
        [this, pov_dd, pov_names, node, ri]() {
          guint s = pov_dd->get_selected();
          std::string v = (s == 0) ? "" : pov_names->get_string(s);
          node->pov_character_name = v;
          if (ri < m_grid_selected.size() && m_grid_selected[ri])
            for (size_t i = 0; i < m_grid_rows.size(); ++i)
              if (i != ri && i < m_grid_selected.size() && m_grid_selected[i] &&
                  m_grid_rows[i])
                m_grid_rows[i]->pov_character_name = v;
          m_model.mark_modified();
          Glib::signal_idle().connect_once([this]() { rebuild_outline(); });
        });
    m_outline_grid.attach(*pov_dd, C_POV, grid_row);

    // ── C_LABEL ──────────────────────────────────────────────────────────
    {
      auto *ldd = Gtk::make_managed<Gtk::DropDown>(label_names);
      ldd->add_css_class("outline-cell");
      ldd->set_size_request(col_min_w[C_LABEL], -1);
      ldd->set_selected((guint)node->color_idx); // set BEFORE connecting signal
      if (node->color_idx > 0 &&
          node->color_idx <= (int)m_prefs.tag_colors.size()) {
        const std::string &hex = m_prefs.tag_colors[node->color_idx - 1].hex;
        auto css = Gtk::CssProvider::create();
        css->load_from_data("dropdown{color:" + hex + ";font-weight:700;}");
        ldd->get_style_context()->add_provider(
            css, GTK_STYLE_PROVIDER_PRIORITY_USER);
      }
      ldd->property_selected().signal_changed().connect(
          [this, ldd, node, ri]() {
            int idx = (int)ldd->get_selected();
            node->color_idx = idx;
            if (ri < m_grid_selected.size() && m_grid_selected[ri])
              for (size_t i = 0; i < m_grid_rows.size(); ++i)
                if (i != ri && i < m_grid_selected.size() &&
                    m_grid_selected[i] && m_grid_rows[i])
                  m_grid_rows[i]->color_idx = idx;
            m_model.mark_modified();
            Glib::signal_idle().connect_once([this]() { rebuild_outline(); });
          });
      m_outline_grid.attach(*ldd, C_LABEL, grid_row);
    }

    // ── C_WORDS ──────────────────────────────────────────────────────────
    auto *wl =
        Gtk::make_managed<Gtk::Label>(std::to_string(node->word_count()));
    wl->add_css_class("stat-label");
    wl->add_css_class("outline-cell");
    wl->set_size_request(col_min_w[C_WORDS], -1);
    wl->set_halign(Gtk::Align::END);
    m_outline_grid.attach(*wl, C_WORDS, grid_row);

    // ── C_TARGET ─────────────────────────────────────────────────────────
    auto *ts = Gtk::make_managed<Gtk::SpinButton>();
    ts->set_adjustment(Gtk::Adjustment::create(0, 0, 999999, 100, 1000));
    ts->set_digits(0);
    ts->set_numeric(true);
    ts->add_css_class("outline-cell");
    ts->set_size_request(col_min_w[C_TARGET], -1);
    // Connect signal BEFORE set_value so initial set fires but is guarded
    auto ts_init =
        std::make_shared<bool>(true); // true = initialising, skip propagation
    ts->signal_value_changed().connect([this, ts, node, ri, ts_init]() {
      if (*ts_init)
        return;
      int v = (int)ts->get_value();
      node->word_target = v;
      if (ri < m_grid_selected.size() && m_grid_selected[ri])
        for (size_t i = 0; i < m_grid_rows.size(); ++i)
          if (i != ri && i < m_grid_selected.size() && m_grid_selected[i] &&
              m_grid_rows[i])
            m_grid_rows[i]->word_target = v;
      m_model.mark_modified();
      Glib::signal_idle().connect_once([this]() { rebuild_outline(); });
    });
    ts->set_value(node->word_target);
    *ts_init = false;
    m_outline_grid.attach(*ts, C_TARGET, grid_row);

    // ── C_INCLUDE ────────────────────────────────────────────────────────
    auto *ic = Gtk::make_managed<Gtk::CheckButton>();
    ic->set_halign(Gtk::Align::CENTER);
    auto ic_init = std::make_shared<bool>(true);
    ic->signal_toggled().connect([this, ic, node, ri, ic_init]() {
      if (*ic_init)
        return;
      bool v = ic->get_active();
      node->include_in_export = v;
      if (ri < m_grid_selected.size() && m_grid_selected[ri])
        for (size_t i = 0; i < m_grid_rows.size(); ++i)
          if (i != ri && i < m_grid_selected.size() && m_grid_selected[i] &&
              m_grid_rows[i])
            m_grid_rows[i]->include_in_export = v;
      m_model.mark_modified();
      Glib::signal_idle().connect_once([this]() { rebuild_outline(); });
    });
    ic->set_active(node->include_in_export);
    *ic_init = false;
    m_outline_grid.attach(*ic, C_INCLUDE, grid_row);

    // ── C_SYNOPSIS ───────────────────────────────────────────────────────
    auto *sye = Gtk::make_managed<Gtk::Entry>();
    sye->set_has_frame(false);
    sye->add_css_class("outline-cell");
    sye->set_hexpand(true);
    auto sye_init = std::make_shared<bool>(true);
    sye->signal_changed().connect([this, sye, node, ri, sye_init]() {
      if (*sye_init)
        return;
      std::string v = sye->get_text();
      node->synopsis = v;
      if (ri < m_grid_selected.size() && m_grid_selected[ri])
        for (size_t i = 0; i < m_grid_rows.size(); ++i)
          if (i != ri && i < m_grid_selected.size() && m_grid_selected[i] &&
              m_grid_rows[i])
            m_grid_rows[i]->synopsis = v;
      m_model.mark_modified();
    });
    sye->set_text(node->synopsis);
    *sye_init = false;
    attach_rebuild(sye);
    m_outline_grid.attach(*sye, C_SYNOPSIS, grid_row);
  }

  m_grid_row_count = (int)flat.size();
}

void Editor::grid_batch_set_status(NodeStatus s) {
  for (size_t i = 0; i < m_grid_rows.size(); ++i)
    if (i < m_grid_selected.size() && m_grid_selected[i] && m_grid_rows[i])
      m_grid_rows[i]->status = s;
  m_model.mark_modified();
  rebuild_outline();
}

void Editor::grid_batch_set_pov(const std::string &pov) {
  for (size_t i = 0; i < m_grid_rows.size(); ++i)
    if (i < m_grid_selected.size() && m_grid_selected[i] && m_grid_rows[i])
      m_grid_rows[i]->pov_character_name = pov;
  m_model.mark_modified();
  rebuild_outline();
}

void Editor::grid_batch_set_label(int idx) {
  for (size_t i = 0; i < m_grid_rows.size(); ++i)
    if (i < m_grid_selected.size() && m_grid_selected[i] && m_grid_rows[i])
      m_grid_rows[i]->color_idx = idx;
  m_model.mark_modified();
  rebuild_outline();
}

void Editor::grid_batch_set_include(bool v) {
  for (size_t i = 0; i < m_grid_rows.size(); ++i)
    if (i < m_grid_selected.size() && m_grid_selected[i] && m_grid_rows[i])
      m_grid_rows[i]->include_in_export = v;
  m_model.mark_modified();
  rebuild_outline();
}

// show_grid
// ─────────────────────────────────────────────────────────────────────────────

void Editor::show_grid(const std::vector<BoardItem> &items) {
  m_grid_items = items;
  // Auto-enable the section filter matching the incoming selection
  if (!items.empty()) {
    Section sec = items[0].section;
    // Don't reset — just ensure the incoming section is on
    if (sec == Section::Manuscript)
      m_grid_show_manuscript = true;
    if (sec == Section::Characters)
      m_grid_show_characters = true;
    if (sec == Section::Places)
      m_grid_show_places = true;
  }
  if (m_view_mode == ViewMode::Outline)
    rebuild_outline();
}

// ─────────────────────────────────────────────────────────────────────────────
// Joined View — load_joined / save_joined / reload_joined / exit_joined
// ─────────────────────────────────────────────────────────────────────────────

void Editor::load_joined(std::vector<BinderNode *> nodes) {
  if (nodes.empty()) {
    exit_joined();
    return;
  }

  m_loading        = true;
  m_loading_joined = true;
  m_joined_save_conn.disconnect();

  // Reset first-click guards — same as load_node — so the scroll-snapshot
  // fires on the first click after every JV load and suppresses the spurious
  // selection that GTK creates when its focus-scroll shifts the viewport
  // between button-press and button-release.
  m_first_click_done = false;
  m_first_node_click = true;

  m_buffer->set_text("");
  m_joined_segments.clear();
  m_joined_active = true;
  m_current_node = nullptr;

  EditorHtmlSerializer::Tags tags;
  tags.bold = m_tag_bold;
  tags.italic = m_tag_italic;
  tags.underline = m_tag_underline;
  tags.strikethrough = m_tag_strikethrough;
  tags.justify_left = m_tag_justify_left;
  tags.justify_center = m_tag_justify_center;
  tags.justify_right = m_tag_justify_right;
  tags.justify_full = m_tag_justify_full;
  for (int i = 0; i < (int)m_tag_ol.size(); ++i)
    tags.ol[i] = m_tag_ol[i];

  std::string dash_prefix;
  for (int d = 0; d < 6; ++d)
    dash_prefix += "\xe2\x80\x94";

  for (size_t i = 0; i < nodes.size(); ++i) {
    BinderNode *node = nodes[i];

    // Plain-text title divider — no child anchor, always renders
    // Record start offset before inserting (iterators are invalidated by insert)
    int div_start_off = m_buffer->end().get_offset();
    if (i > 0)
      m_buffer->insert(m_buffer->end(), "\n");
    m_buffer->insert(m_buffer->end(), dash_prefix + "  " + node->title + "\n");
    // Apply non-editable divider tag over the entire divider line
    {
      auto div_start = m_buffer->get_iter_at_offset(div_start_off);
      auto div_end   = m_buffer->end();
      m_buffer->apply_tag(m_tag_joined_divider, div_start, div_end);
    }

    // Start mark with LEFT gravity — stays at content start
    auto start_iter = m_buffer->end();
    auto start_mark = m_buffer->create_mark("js_start_" + std::to_string(i),
                                            start_iter, true /*left gravity*/);

    // Deserialise node content — insert blank paragraph if empty so segment is editable
    if (!node->content.empty()) {
      auto tmp_buf = Gtk::TextBuffer::create(m_buffer->get_tag_table());
      EditorHtmlSerializer tmp_ser(tmp_buf, tags);
      tmp_ser.from_html(node->content);

      int base_offset = m_buffer->end().get_offset();
      Glib::ustring text =
          tmp_buf->get_text(tmp_buf->begin(), tmp_buf->end(), false);
      m_buffer->insert(m_buffer->end(), text);

      auto it = tmp_buf->begin();
      auto tend = tmp_buf->end();
      while (it != tend) {
        auto next = it;
        next.forward_to_tag_toggle({});
        if (next > tend)
          next = tend;
        auto active = it.get_tags();
        int rel_s = it.get_offset();
        int rel_e = next.get_offset();
        if (rel_s < rel_e && !active.empty()) {
          auto ms = m_buffer->get_iter_at_offset(base_offset + rel_s);
          auto me = m_buffer->get_iter_at_offset(base_offset + rel_e);
          for (auto &t : active)
            m_buffer->apply_tag(t, ms, me);
        }
        if (next == tend)
          break;
        it = next;
      }
    } else {
      // Empty node — insert a blank paragraph so the segment is editable
      m_buffer->insert(m_buffer->end(), "\n");
    }

    JoinedSegment seg;
    seg.node = node;
    seg.start = start_mark;
    seg.end = {};
    seg.divider_anchor = {};
    seg.dirty = false;
    LOG_DEBUG("load_joined: seg {} = node='{}' start_off={}", i, node->title,
              m_buffer->get_iter_at_mark(start_mark).get_offset());
    m_joined_segments.push_back(std::move(seg));
  }

  m_chapter_tag.set_text("\xe2\x8a\x97  Joined View");
  m_title_label.set_text(std::to_string(nodes.size()) + " scenes");

  apply_indent();
  apply_base_font_tag();
  apply_zoom_to_font_tags();

  // Rebase and apply annotation tags for all segments
  for (const auto &seg : m_joined_segments) {
    if (!seg.node || seg.node->annotations.empty())
      continue;
    int base = m_buffer->get_iter_at_mark(seg.start).get_offset();
    for (const auto &ann : seg.node->annotations) {
      if (ann.range_start >= ann.range_end)
        continue;
      Annotation rebased = ann;
      rebased.range_start = base + ann.range_start;
      rebased.range_end   = base + ann.range_end;
      apply_annotation_tag(rebased);
    }
  }

  // All programmatic buffer changes done — re-enable text-change handling
  m_loading = false;

  m_buffer->place_cursor(m_buffer->begin());
  m_text_view.scroll_to(m_buffer->get_insert(), 0.0);

  m_loading_joined = false;
}

void Editor::save_joined() {
  if (!m_joined_active)
    return;
  m_loading = true;
  int saved_count = 0;
  for (int i = 0; i < (int)m_joined_segments.size(); ++i) {
    auto &seg = m_joined_segments[i];
    if (!seg.dirty || !seg.node)
      continue;
    std::string html = slice_segment_to_html(i);
    LOG_DEBUG("save_joined: seg {} node='{}' html_len={}", i, seg.node->title,
              html.size());
    seg.node->content = html;
    seg.node->content_modified = true;
    seg.dirty = false;
    ++saved_count;
  }
  LOG_DEBUG("save_joined: saved {} segments", saved_count);
  m_model.mark_modified();
  m_loading = false;
}

void Editor::reload_joined(std::vector<BinderNode *> nodes) {
  save_joined();
  load_joined(nodes);
}

void Editor::exit_joined() {
  if (!m_joined_active)
    return;
  m_exiting_joined = true; // block load_node until we're fully done
  save_joined();
  m_joined_save_conn.disconnect();
  m_joined_active = false;
  m_joined_segments.clear();
  m_loading = true;
  m_buffer->set_text("");
  m_loading = false;
  m_chapter_tag.set_text("");
  m_title_label.set_text("");
  m_exiting_joined = false;
}

std::string Editor::slice_segment_to_html(int seg_idx) const {
  if (seg_idx < 0 || seg_idx >= (int)m_joined_segments.size())
    return "";
  const auto &seg = m_joined_segments[seg_idx];
  int n = (int)m_joined_segments.size();

  // Start: the segment's start mark
  auto start = m_buffer->get_iter_at_mark(seg.start);

  // End: just before next segment's start mark, or buffer end
  Gtk::TextBuffer::iterator end;
  if (seg_idx + 1 < n) {
    end = m_buffer->get_iter_at_mark(m_joined_segments[seg_idx + 1].start);
    // Walk back past the entire divider line that precedes the next segment's
    // content start. The divider is: "\n——————  Title\n" where the leading \n
    // is the inter-segment separator. start_mark sits just after the trailing
    // \n, so we scan backward until we've consumed two newlines — that puts
    // `end` at the end of the previous segment's actual content.
    {
      int newlines_found = 0;
      while (end > start) {
        auto prev = end;
        prev.backward_char();
        if (prev.get_char() == '\n') {
          ++newlines_found;
          end = prev;
          if (newlines_found == 2)
            break;
        } else {
          end = prev;
        }
      }
    }
  } else {
    end = m_buffer->end();
  }

  // Trim leading/trailing newlines from the content range
  while (start < end && start.get_char() == '\n')
    start.forward_char();
  while (end > start) {
    auto prev = end;
    prev.backward_char();
    if (prev.get_char() == '\n')
      end = prev;
    else
      break;
  }

  if (start >= end)
    return "";

  // Build a scratch buffer that holds only the segment's text and tags,
  // then serialise it with a temporary EditorHtmlSerializer.
  Glib::RefPtr<Gtk::TextBuffer> scratch =
      Gtk::TextBuffer::create(m_buffer->get_tag_table());

  // Use get_text (skips embedded object chars) to get clean text
  Glib::ustring text = m_buffer->get_text(start, end, false);
  scratch->set_text(text);

  // Copy tag spans — walk source buffer and track a separate scratch offset
  // that skips U+FFFC anchor chars (which get_text omits).
  // We collect (scratch_start, scratch_end, tag) tuples then apply them.
  struct TagSpan {
    int s, e;
    Glib::RefPtr<Gtk::TextTag> tag;
  };
  std::vector<TagSpan> spans;

  int scratch_off = 0;
  auto it = start;
  while (it != end) {
    gunichar ch = it.get_char();
    bool is_anchor = (ch == 0xFFFC);

    auto next_toggle = it;
    next_toggle.forward_to_tag_toggle({});
    if (next_toggle > end)
      next_toggle = end;

    // Count non-anchor chars between it and next_toggle
    int seg_scratch_start = scratch_off;
    auto count_it = it;
    while (count_it != next_toggle) {
      if (count_it.get_char() != 0xFFFC)
        ++scratch_off;
      count_it.forward_char();
    }
    int seg_scratch_end = scratch_off;

    if (!is_anchor && seg_scratch_start < seg_scratch_end) {
      for (auto &tag : it.get_tags()) {
        std::string tn = tag->property_name().get_value();
        // Skip display-only tags that are never stored in node HTML
        if (tn == "base_font")
          continue;
        if (tn == "find_match")
          continue;
        if (tn == "find_current")
          continue;
        if (tn == "link-highlight")
          continue;
        if (tn.size() > 7 && tn.substr(0, 7) == "indent_")
          continue;
        if (tag == m_tag_indent)
          continue;
        if (tag == m_tag_joined_divider)
          continue;
        spans.push_back({seg_scratch_start, seg_scratch_end, tag});
      }
    }

    if (next_toggle == end)
      break;
    it = next_toggle;
  }

  for (auto &sp : spans) {
    if (sp.s >= sp.e)
      continue;
    auto ts = scratch->get_iter_at_offset(sp.s);
    auto te = scratch->get_iter_at_offset(sp.e);
    scratch->apply_tag(sp.tag, ts, te);
  }

  // Build Tags struct from the shared tag table (same pointers work)
  EditorHtmlSerializer::Tags t;
  t.bold = m_tag_bold;
  t.italic = m_tag_italic;
  t.underline = m_tag_underline;
  t.strikethrough = m_tag_strikethrough;
  t.justify_left = m_tag_justify_left;
  t.justify_center = m_tag_justify_center;
  t.justify_right = m_tag_justify_right;
  t.justify_full = m_tag_justify_full;
  for (int i = 0; i < (int)m_tag_ol.size(); ++i)
    t.ol[i] = m_tag_ol[i];

  EditorHtmlSerializer tmp_ser(scratch, t);
  return tmp_ser.to_html();
}

int Editor::segment_index_at_iter(const Gtk::TextBuffer::iterator &it) const {
  int off = it.get_offset();
  int n = (int)m_joined_segments.size();
  for (int i = 0; i < n; ++i) {
    int s = m_buffer->get_iter_at_mark(m_joined_segments[i].start).get_offset();
    // End = just before next segment's start mark; for last segment = buffer end
    int e = (i + 1 < n)
                ? m_buffer->get_iter_at_mark(m_joined_segments[i + 1].start)
                      .get_offset()
                : m_buffer->end().get_offset();
    if (off >= s && off < e)
      return i;
  }
  // Cursor is in a divider line (before first start mark, or between segments)
  // — not inside any editable content segment.
  return -1;
}

// show_board
// ─────────────────────────────────────────────────────────────────────────────

void Editor::show_board(const std::vector<BoardItem> &items) {
  if (!m_board_flow)
    return;

  // Count selected items for multi-selection detection
  int card_count = (int)items.size();

  // Multi-selection in Grid mode → show placeholder hint
  // Write mode handles multi via JV — don't clobber it with the placeholder
  if (m_view_mode != ViewMode::Board && m_view_mode != ViewMode::Write
      && m_view_mode != ViewMode::Joined && card_count > 1) {
    int groups = 0, scenes = 0, chars = 0, places = 0;
    for (auto &item : items) {
      const BinderNode *n = m_model.find_node_by_iid(item.iid);
      if (!n)
        continue;
      switch (n->kind) {
      case BinderKind::Group:
        ++groups;
        break;
      case BinderKind::Character:
        ++chars;
        break;
      case BinderKind::Place:
        ++places;
        break;
      default:
        ++scenes;
        break;
      }
    }
    std::string summary;
    auto add = [&](int n, const char *sg, const char *pl) {
      if (!n)
        return;
      if (!summary.empty())
        summary += ", ";
      summary += std::to_string(n) + " " + (n == 1 ? sg : pl);
    };
    add(groups, "group", "groups");
    add(scenes, "scene", "scenes");
    add(chars, "character", "characters");
    add(places, "place", "places");
    m_multi_placeholder_label.set_text(
        summary + " selected\n\nSwitch to  ⊞ Board  to view them as cards,\n"
                  "or double-click a single item to open it.");
    if (m_view_mode != ViewMode::Outline)
      m_view_stack.set_visible_child(m_multi_placeholder_box);
  }

  // Rebuild board cards
  while (auto *c = m_board_flow->get_first_child())
    m_board_flow->remove(*c);

  if (items.empty()) {
    m_board_flow->set_visible(false);
    m_board_placeholder.set_visible(true);
    return;
  }

  bool any = false;
  for (auto &item : items) {
    const BinderNode *node = m_model.find_node_by_iid(item.iid);
    if (!node) continue;
    // make_board_card places the card positionally — resolve the iid to its
    // current path at this render edge.
    auto path = m_model.path_for_iid(item.section, item.iid);
    if (path.empty()) continue;
    if (auto *card = make_board_card(item.section, path)) {
      m_board_flow->append(*card);
      any = true;
    }
  }

  m_board_placeholder.set_visible(!any);
  m_board_flow->set_visible(any);
}

// ─────────────────────────────────────────────────────────────────────────────
// Board card factories
// ─────────────────────────────────────────────────────────────────────────────

Gtk::Widget *Editor::make_board_card_node(const std::vector<int> &path) {
  const BinderNode *node = m_model.node_at(Section::Manuscript, path);
  if (!node)
    return nullptr;

  auto *fb = Gtk::make_managed<Gtk::FlowBoxChild>();
  auto *card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
  card->add_css_class("folio-paper");
  card->add_css_class("board-card");
  card->set_size_request(240, -1); // width only — height grows naturally
  card->set_name(Folio::widget_name("board-card", node->iid)); // s19

  auto *header = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
  header->add_css_class("board-card-header");
  header->set_margin_top(12);
  header->set_margin_start(14);
  header->set_margin_end(14);
  header->set_margin_bottom(8);

  auto *chip = Gtk::make_managed<Gtk::Label>(
      node->kind == BinderKind::Group ? "Group" : "Scene");
  chip->add_css_class("board-part-chip");
  chip->set_halign(Gtk::Align::START);
  {
    std::string hex = m_prefs.color_hex_for_idx(node->color_idx);
    if (!hex.empty()) {
      auto prov = Gtk::CssProvider::create();
      prov->load_from_data("label { color: " + hex + "; }");
      chip->get_style_context()->add_provider(
          prov, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
  }
  header->append(*chip);

  auto *title_lbl = Gtk::make_managed<Gtk::Label>(
      node->title.empty() ? "Untitled" : node->title);
  title_lbl->add_css_class("row-title");
  title_lbl->set_halign(Gtk::Align::START);
  title_lbl->set_wrap(true);
  title_lbl->set_xalign(0.0f);
  title_lbl->set_max_width_chars(24);
  header->append(*title_lbl);

  if (!node->synopsis.empty()) {
    auto *syn = Gtk::make_managed<Gtk::Label>(node->synopsis);
    syn->add_css_class("row-subtitle");
    syn->set_halign(Gtk::Align::START);
    syn->set_wrap(true);
    syn->set_xalign(0.0f);
    syn->set_max_width_chars(30);
    syn->set_ellipsize(Pango::EllipsizeMode::END);
    syn->set_lines(2);
    header->append(*syn);
  }
  card->append(*header);
  card->append(
      *Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL));

  // Scrollable text preview (plain-text, HTML stripped)
  auto *prev_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
  prev_scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  prev_scroll->set_vexpand(true);
  prev_scroll->set_size_request(-1,
                                120); // cap natural height so GTK doesn't warn
  prev_scroll->add_css_class("board-card-preview-scroll");
  auto buf = Gtk::TextBuffer::create();
  std::string preview = node->content;
  if (preview.find('<') != std::string::npos) {
    std::string stripped;
    bool in_tag = false;
    for (unsigned char c : preview) {
      if (c == '<')
        in_tag = true;
      else if (c == '>')
        in_tag = false;
      else if (!in_tag)
        stripped += (char)c;
    }
    preview = stripped.empty() ? "No content yet…" : stripped;
  } else if (preview.empty()) {
    preview = "No content yet…";
  }
  buf->set_text(preview);
  auto *tv = Gtk::make_managed<Gtk::TextView>(buf);
  tv->set_editable(false);
  tv->set_cursor_visible(false);
  tv->set_wrap_mode(Gtk::WrapMode::WORD_CHAR);
  tv->set_top_margin(8);
  tv->set_bottom_margin(8);
  tv->set_left_margin(14);
  tv->set_right_margin(14);
  tv->add_css_class("board-card-preview-text");
  prev_scroll->set_child(*tv);
  card->append(*prev_scroll);

  auto *footer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
  footer->add_css_class("board-card-footer");
  footer->set_margin_top(8);
  footer->set_margin_bottom(10);
  footer->set_margin_start(14);
  footer->set_margin_end(14);
  auto *wc_lbl = Gtk::make_managed<Gtk::Label>(
      std::to_string(node->word_count()) + " words");
  wc_lbl->add_css_class("stat-label");
  wc_lbl->set_hexpand(true);
  wc_lbl->set_halign(Gtk::Align::START);
  footer->append(*wc_lbl);
  auto *dot = Gtk::make_managed<Gtk::Label>("●");
  dot->add_css_class(status_css(node->status));
  dot->set_tooltip_text(node->status == NodeStatus::Polished     ? "Polished"
                        : node->status == NodeStatus::InProgress ? "In Progress"
                        : node->status == NodeStatus::RoughDraft ? "Rough Draft"
                                                                 : "Untitled");
  footer->append(*dot);
  card->append(*footer);

  auto gc = Gtk::GestureClick::create();
  gc->set_button(1);
  gc->signal_pressed().connect([this, path](int n_press, double, double) {
    if (n_press == 2) {
      m_model.set_active(Section::Manuscript, path);
      load_node(m_model.active_node());
    }
  });
  card->add_controller(gc);
  fb->set_child(*card);
  fb->set_focusable(true);
  return fb;
}

Gtk::Widget *Editor::make_board_card(Section section,
                                     const std::vector<int> &path) {
  const BinderNode *node = m_model.node_at(section, path);
  if (!node)
    return nullptr;

  if (node->kind == BinderKind::Scene || node->kind == BinderKind::Group) {
    // Reuse the existing scene card factory for manuscript nodes
    return make_board_card_node(path);
  }

  // Character or Place card
  auto *fb = Gtk::make_managed<Gtk::FlowBoxChild>();
  auto *card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
  card->add_css_class("folio-paper");
  card->add_css_class("board-card");
  card->set_size_request(240, -1); // width only — height grows naturally
  card->set_name(Folio::widget_name("board-card", node->iid)); // s19

  auto *header = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
  header->add_css_class("board-card-header");
  header->set_margin_top(12);
  header->set_margin_start(14);
  header->set_margin_end(14);
  header->set_margin_bottom(8);

  const char *chip_label =
      (node->kind == BinderKind::Character) ? "Character" : "Place";
  auto *chip = Gtk::make_managed<Gtk::Label>(chip_label);
  chip->add_css_class("board-part-chip");
  chip->set_halign(Gtk::Align::START);
  {
    std::string hex = m_prefs.color_hex_for_idx(node->color_idx);
    if (!hex.empty()) {
      auto prov = Gtk::CssProvider::create();
      prov->load_from_data("label { color: " + hex + "; }");
      chip->get_style_context()->add_provider(
          prov, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
  }
  header->append(*chip);

  auto *name_lbl = Gtk::make_managed<Gtk::Label>(
      node->title.empty() ? "Unnamed" : node->title);
  name_lbl->add_css_class("row-title");
  name_lbl->set_halign(Gtk::Align::START);
  name_lbl->set_wrap(true);
  name_lbl->set_xalign(0.0f);
  name_lbl->set_max_width_chars(24);
  header->append(*name_lbl);

  if (!node->description.empty()) {
    auto *desc = Gtk::make_managed<Gtk::Label>(node->description);
    desc->add_css_class("row-subtitle");
    desc->set_halign(Gtk::Align::START);
    desc->set_wrap(true);
    desc->set_xalign(0.0f);
    desc->set_max_width_chars(30);
    desc->set_ellipsize(Pango::EllipsizeMode::END);
    desc->set_lines(2);
    header->append(*desc);
  }
  card->append(*header);
  card->append(
      *Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL));

  auto *ps = Gtk::make_managed<Gtk::ScrolledWindow>();
  ps->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  ps->set_vexpand(true);
  ps->set_size_request(-1, 120); // cap natural height so GTK doesn't warn
  auto buf = Gtk::TextBuffer::create();
  buf->set_text(
      node->content.empty()
          ? (node->synopsis.empty() ? "No notes yet…" : node->synopsis)
          : node->content);
  auto *tv = Gtk::make_managed<Gtk::TextView>(buf);
  tv->set_editable(false);
  tv->set_cursor_visible(false);
  tv->set_wrap_mode(Gtk::WrapMode::WORD_CHAR);
  tv->set_top_margin(8);
  tv->set_bottom_margin(8);
  tv->set_left_margin(14);
  tv->set_right_margin(14);
  tv->add_css_class("board-card-preview-text");
  ps->set_child(*tv);
  card->append(*ps);

  auto *footer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
  footer->add_css_class("board-card-footer");
  footer->set_margin_top(8);
  footer->set_margin_bottom(10);
  footer->set_margin_start(14);
  footer->set_margin_end(14);
  std::string footer_text =
      (node->kind == BinderKind::Character)
          ? (!node->role.empty() ? node->role : "Character")
          : (node->synopsis.empty() ? "Setting" : "⌖ Location");
  auto *foot_lbl = Gtk::make_managed<Gtk::Label>(footer_text);
  foot_lbl->add_css_class("stat-label");
  foot_lbl->set_hexpand(true);
  footer->append(*foot_lbl);
  card->append(*footer);

  auto gc = Gtk::GestureClick::create();
  gc->set_button(1);
  gc->signal_pressed().connect(
      [this, section, path](int n_press, double, double) {
        if (n_press == 2) {
          BinderNode *n = m_model.node_at(section, path);
          load_node(n);
        }
      });
  card->add_controller(gc);
  fb->set_child(*card);
  fb->set_focusable(true);
  return fb;
}

// ─────────────────────────────────────────────────────────────────────────────
// Focus mode
// ─────────────────────────────────────────────────────────────────────────────

// Apply or remove focus-specific font/size/line-spacing/color.
// Call with entering=true on focus entry, entering=false on exit.
void Editor::apply_focus_style(bool entering) {
  if (entering) {
    // Switch to focus font/size
    if (!m_focus_font.empty())
      m_current_font = m_focus_font;
    if (m_focus_font_size > 0)
      m_current_font_size = m_focus_font_size;

    // Line spacing
    if (m_focus_line_spacing > 0.0) {
      m_current_line_spacing = m_focus_line_spacing;
      if (m_line_spacing_spin)
        m_line_spacing_spin->set_value(m_focus_line_spacing);
    }

    // Text color override via CSS
    if (!m_focus_text_color.empty()) {
      if (!m_focus_color_css) {
        m_focus_color_css = Gtk::CssProvider::create();
        m_text_view.get_style_context()->add_provider(
            m_focus_color_css, GTK_STYLE_PROVIDER_PRIORITY_USER + 2);
      }
      std::string css = "textview text { color: " + m_focus_text_color + "; }";
      m_focus_color_css->load_from_data(css);
    }
  } else {
    // Restore regular font/size from snapshot (not prefs — focus bar
    // changes must not leak into the regular editor)
    m_current_font = m_saved_font.empty() ? m_prefs.editor_font : m_saved_font;
    m_current_font_size =
        m_saved_font_size > 0 ? m_saved_font_size : m_prefs.editor_font_size;

    // Restore line spacing from snapshot
    m_current_line_spacing = m_saved_line_spacing > 0.0 ? m_saved_line_spacing
                                                        : m_prefs.line_spacing;
    if (m_line_spacing_spin)
      m_line_spacing_spin->set_value(m_current_line_spacing);

    // Remove text color override
    if (m_focus_color_css)
      m_focus_color_css->load_from_data("textview text {}");
  }
}

void Editor::enter_focus_mode() {
  if (m_in_focus)
    return;
  m_in_focus = true;
  if (m_on_focus_mode)
    m_on_focus_mode(true);
  m_toolbar.set_visible(false);
  m_footer.set_visible(false);

  // Snapshot regular-editor state before switching to focus values
  m_saved_font = m_current_font;
  m_saved_font_size = m_current_font_size;
  m_saved_line_spacing = m_current_line_spacing;
  m_saved_zoom_factor = m_zoom_factor;
  m_saved_typewriter =
      m_prefs.typewriter_mode; // regular (non-focus) typewriter
  m_saved_page_margin_px = m_page_margin_px;

  // Switch to focus-independent state
  m_zoom_factor = m_focus_zoom_factor;
  if (m_zoom_scale)
    m_zoom_scale->set_value(m_focus_zoom_factor * 100.0);
  m_typewriter_mode = m_focus_typewriter;
  if (m_btn_typewriter.get_active() != m_focus_typewriter)
    m_btn_typewriter.set_active(m_focus_typewriter);
  apply_focus_style(true);

  if (!m_exit_focus_btn) {
    m_exit_focus_btn = Gtk::make_managed<Gtk::Button>("✕  Exit Focus");
    m_exit_focus_btn->add_css_class("focus-exit-btn");
    m_exit_focus_btn->set_halign(Gtk::Align::END);
    m_exit_focus_btn->set_valign(Gtk::Align::START);
    m_exit_focus_btn->set_margin_top(12);
    m_exit_focus_btn->set_margin_end(16);
    m_exit_focus_btn->signal_clicked().connect([this]() { exit_focus_mode(); });
    m_scroll_overlay.add_overlay(*m_exit_focus_btn);
  }
  m_exit_focus_btn->set_visible(true);

  m_focus_key_ctrl = Gtk::EventControllerKey::create();
  m_focus_key_ctrl->signal_key_pressed().connect(
      [this](guint keyval, guint, Gdk::ModifierType) -> bool {
        if (keyval == GDK_KEY_Escape) {
          exit_focus_mode();
          return true;
        }
        return false;
      },
      false);
  m_scroll_overlay.add_controller(m_focus_key_ctrl);
  m_scroll_overlay.grab_focus();

  m_paper_card.set_margin_start(0);
  m_paper_card.set_margin_end(0);
  m_paper_card.set_size_request(-1,
                                -1); // let apply_page_geometry measure freshly

  // Build focus-mode control bar (created once, reused)
  // Contains: page-width slider | separator | zoom slider
  // Hover-reveal: hidden by default, shown when mouse near bottom of viewport
  if (!m_focus_width_bar) {
    auto *bar_box =
        Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    bar_box->add_css_class("focus-width-bar");
    bar_box->set_halign(Gtk::Align::CENTER);
    bar_box->set_valign(Gtk::Align::END);
    bar_box->set_margin_bottom(16);

    // ── Page width ──────────────────────────────────────────────────────
    auto *w_icon = Gtk::make_managed<Gtk::Label>("â¿");
    w_icon->add_css_class("stat-label");
    w_icon->set_tooltip_text("Focus mode page width");

    auto focus_adj = Gtk::Adjustment::create(m_focus_page_width_pct, 15.0,
                                             100.0, 1.0, 5.0, 0.0);
    auto *w_scale =
        Gtk::make_managed<Gtk::Scale>(focus_adj, Gtk::Orientation::HORIZONTAL);
    w_scale->set_size_request(140, -1);
    w_scale->set_valign(Gtk::Align::CENTER);
    w_scale->set_draw_value(false);
    w_scale->add_css_class("focus-width-scale");
    w_scale->set_tooltip_text("Page width (15â100 %)");

    auto *w_lbl =
        Gtk::make_managed<Gtk::Label>(std::to_string(m_focus_page_width_pct));
    w_lbl->set_width_chars(3);
    w_lbl->set_xalign(1.0f);
    w_lbl->add_css_class("page-width-entry");

    auto *w_pct = Gtk::make_managed<Gtk::Label>("%");
    w_pct->add_css_class("stat-label");

    focus_adj->signal_value_changed().connect([this, focus_adj, w_lbl]() {
      int p =
          std::max(15, std::min(100, (int)std::round(focus_adj->get_value())));
      if (p == m_focus_page_width_pct)
        return;
      m_focus_page_width_pct = p;
      m_prefs.focus_page_width_pct = p;
      w_lbl->set_text(std::to_string(p));
      apply_page_geometry();
      try {
        m_prefs.save();
      } catch (...) {
      }
    });

    // ── Separator ───────────────────────────────────────────────────────
    auto *sep = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL);
    sep->set_margin_start(4);
    sep->set_margin_end(4);

    // ── Zoom ────────────────────────────────────────────────────────────
    auto *z_icon = Gtk::make_managed<Gtk::Label>("â");
    z_icon->add_css_class("stat-label");
    z_icon->set_tooltip_text("Focus mode zoom");

    int initial_zoom_pct = (int)std::round(m_focus_zoom_factor * 100.0);
    auto zoom_adj =
        Gtk::Adjustment::create(initial_zoom_pct, 50.0, 300.0, 5.0, 20.0, 0.0);
    auto *z_scale =
        Gtk::make_managed<Gtk::Scale>(zoom_adj, Gtk::Orientation::HORIZONTAL);
    z_scale->set_size_request(140, -1);
    z_scale->set_valign(Gtk::Align::CENTER);
    z_scale->set_draw_value(false);
    z_scale->add_css_class("focus-width-scale");
    z_scale->set_tooltip_text(
        "Zoom (50â300 %)  Â· Double-click to reset");

    m_focus_zoom_lbl =
        Gtk::make_managed<Gtk::Label>(std::to_string(initial_zoom_pct));
    m_focus_zoom_lbl->set_width_chars(3);
    m_focus_zoom_lbl->set_xalign(1.0f);
    m_focus_zoom_lbl->add_css_class("page-width-entry");

    auto *z_pct = Gtk::make_managed<Gtk::Label>("%");
    z_pct->add_css_class("stat-label");

    zoom_adj->signal_value_changed().connect([this, zoom_adj]() {
      int p =
          std::max(50, std::min(300, (int)std::round(zoom_adj->get_value())));
      m_focus_zoom_factor = p / 100.0;
      m_zoom_factor = m_focus_zoom_factor;
      m_prefs.focus_zoom_pct = p;
      if (m_focus_zoom_lbl)
        m_focus_zoom_lbl->set_text(std::to_string(p));
      if (m_zoom_scale)
        m_zoom_scale->set_value(p);
      apply_base_font_tag();
      apply_zoom_to_font_tags();
      try {
        m_prefs.save();
      } catch (...) {
      }
    });

    auto gc_dbl = Gtk::GestureClick::create();
    gc_dbl->set_button(1);
    gc_dbl->signal_pressed().connect([zoom_adj](int n_press, double, double) {
      if (n_press == 2)
        zoom_adj->set_value(100.0);
    });
    z_scale->add_controller(gc_dbl);

    bar_box->append(*w_icon);
    bar_box->append(*w_scale);
    bar_box->append(*w_lbl);
    bar_box->append(*w_pct);
    bar_box->append(*sep);
    bar_box->append(*z_icon);
    bar_box->append(*z_scale);
    bar_box->append(*m_focus_zoom_lbl);
    bar_box->append(*z_pct);

    // ── Separator 2 ────────────────────────────────────────────────────
    auto *sep2 = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL);
    sep2->set_margin_start(4);
    sep2->set_margin_end(4);

    // ── Font family (compact button that pops a font chooser) ──────────────
    auto *f_icon = Gtk::make_managed<Gtk::Label>("ð");
    f_icon->add_css_class("stat-label");
    f_icon->set_tooltip_text("Focus font");

    std::string init_font =
        m_focus_font.empty() ? m_prefs.editor_font : m_focus_font;
    m_focus_font_lbl = Gtk::make_managed<Gtk::Label>(init_font);
    m_focus_font_lbl->add_css_class("page-width-entry");
    m_focus_font_lbl->set_max_width_chars(14);
    m_focus_font_lbl->set_ellipsize(Pango::EllipsizeMode::END);

    auto *f_btn = Gtk::make_managed<Gtk::Button>();
    f_btn->set_child(*m_focus_font_lbl);
    f_btn->add_css_class("flat");
    f_btn->set_tooltip_text("Click to change focus font");
    f_btn->signal_clicked().connect([this]() {
      auto *win = dynamic_cast<Gtk::Window *>(get_root());
      if (!win)
        return;
      std::string init_name =
          (m_focus_font.empty() ? m_prefs.editor_font : m_focus_font) + " " +
          std::to_string(m_focus_font_size > 0 ? m_focus_font_size
                                               : m_prefs.editor_font_size);
      auto *dlg =
          Gtk::make_managed<Gtk::FontChooserDialog>("Choose Focus Font", *win);
      dlg->set_font(init_name);
      dlg->set_modal(true);
      dlg->signal_response().connect([this, dlg](int response) {
        if (response == Gtk::ResponseType::OK) {
          std::string chosen = dlg->get_font();
          // Parse "FamilyName Size" — last token is size
          auto sp = chosen.rfind(' ');
          if (sp != std::string::npos) {
            m_focus_font = chosen.substr(0, sp);
            try {
              m_focus_font_size = std::stoi(chosen.substr(sp + 1));
            } catch (...) {
            }
          } else {
            m_focus_font = chosen;
          }
          if (m_focus_font_size < 6)
            m_focus_font_size = m_prefs.editor_font_size;
          m_prefs.focus_font = m_focus_font;
          m_prefs.focus_font_size = m_focus_font_size;
          if (m_focus_font_lbl)
            m_focus_font_lbl->set_text(m_focus_font);
          if (m_focus_size_lbl)
            m_focus_size_lbl->set_text(std::to_string(m_focus_font_size));
          m_current_font = m_focus_font;
          m_current_font_size = m_focus_font_size;
          apply_base_font_tag();
          apply_zoom_to_font_tags();
          try {
            m_prefs.save();
          } catch (...) {
          }
        }
        dlg->hide();
      });
      dlg->show();
    });

    // ── Font size spin ───────────────────────────────────────────────────
    int init_size =
        m_focus_font_size > 0 ? m_focus_font_size : m_prefs.editor_font_size;
    m_focus_size_lbl = Gtk::make_managed<Gtk::Label>(std::to_string(init_size));
    m_focus_size_lbl->add_css_class("page-width-entry");
    m_focus_size_lbl->set_width_chars(2);

    auto *sz_up = Gtk::make_managed<Gtk::Button>();
    sz_up->set_icon_name("list-add-symbolic");
    sz_up->add_css_class("flat");
    sz_up->set_tooltip_text("Increase font size");
    sz_up->signal_clicked().connect([this]() {
      int sz = (m_focus_font_size > 0 ? m_focus_font_size
                                      : m_prefs.editor_font_size) +
               1;
      sz = std::min(72, sz);
      m_focus_font_size = sz;
      m_current_font_size = sz;
      m_prefs.focus_font_size = sz;
      if (m_focus_size_lbl)
        m_focus_size_lbl->set_text(std::to_string(sz));
      apply_base_font_tag();
      apply_zoom_to_font_tags();
      try {
        m_prefs.save();
      } catch (...) {
      }
    });

    auto *sz_dn = Gtk::make_managed<Gtk::Button>();
    sz_dn->set_icon_name("list-remove-symbolic");
    sz_dn->add_css_class("flat");
    sz_dn->set_tooltip_text("Decrease font size");
    sz_dn->signal_clicked().connect([this]() {
      int sz = (m_focus_font_size > 0 ? m_focus_font_size
                                      : m_prefs.editor_font_size) -
               1;
      sz = std::max(6, sz);
      m_focus_font_size = sz;
      m_current_font_size = sz;
      m_prefs.focus_font_size = sz;
      if (m_focus_size_lbl)
        m_focus_size_lbl->set_text(std::to_string(sz));
      apply_base_font_tag();
      apply_zoom_to_font_tags();
      try {
        m_prefs.save();
      } catch (...) {
      }
    });

    // ── Separator 3 ────────────────────────────────────────────────────
    auto *sep3 = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL);
    sep3->set_margin_start(4);
    sep3->set_margin_end(4);

    // ── Line spacing slider ───────────────────────────────────────────────
    auto *ls_icon = Gtk::make_managed<Gtk::Label>("â");
    ls_icon->add_css_class("stat-label");
    ls_icon->set_tooltip_text("Line spacing");

    double init_ls = m_focus_line_spacing > 0.0 ? m_focus_line_spacing
                                                : m_prefs.line_spacing;
    auto ls_adj = Gtk::Adjustment::create(init_ls, 0.5, 4.0, 0.1, 0.5, 0.0);
    auto *ls_scale =
        Gtk::make_managed<Gtk::Scale>(ls_adj, Gtk::Orientation::HORIZONTAL);
    ls_scale->set_size_request(100, -1);
    ls_scale->set_valign(Gtk::Align::CENTER);
    ls_scale->set_draw_value(false);
    ls_scale->add_css_class("focus-width-scale");
    ls_scale->set_tooltip_text("Line spacing (0.5â4.0)");

    char ls_buf[8];
    std::snprintf(ls_buf, sizeof(ls_buf), "%.1f", init_ls);
    m_focus_ls_lbl = Gtk::make_managed<Gtk::Label>(ls_buf);
    m_focus_ls_lbl->set_width_chars(3);
    m_focus_ls_lbl->add_css_class("page-width-entry");

    ls_adj->signal_value_changed().connect([this, ls_adj]() {
      double v = std::round(ls_adj->get_value() * 10.0) / 10.0;
      m_focus_line_spacing = v;
      m_current_line_spacing = v;
      m_prefs.focus_line_spacing = v;
      if (m_focus_ls_lbl) {
        char b[8];
        std::snprintf(b, sizeof(b), "%.1f", v);
        m_focus_ls_lbl->set_text(b);
      }
      if (m_line_spacing_spin)
        m_line_spacing_spin->set_value(v);
      apply_line_spacing_to_selection();
      try {
        m_prefs.save();
      } catch (...) {
      }
    });

    // ── Separator 4 ────────────────────────────────────────────────────
    auto *sep4 = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL);
    sep4->set_margin_start(4);
    sep4->set_margin_end(4);

    // ── Text color button ────────────────────────────────────────────────
    auto *col_btn = Gtk::make_managed<Gtk::ColorDialogButton>();
    col_btn->add_css_class("flat");
    col_btn->set_tooltip_text(
        "Focus text color (âclick to change, right-click to reset)");
    col_btn->set_valign(Gtk::Align::CENTER);
    auto col_dlg = Gtk::ColorDialog::create();
    col_dlg->set_with_alpha(false);
    col_btn->set_dialog(col_dlg);

    // Set initial color from pref
    if (!m_focus_text_color.empty()) {
      Gdk::RGBA rgba;
      rgba.set(m_focus_text_color);
      col_btn->set_rgba(rgba);
    }

    col_btn->property_rgba().signal_changed().connect([this, col_btn]() {
      auto c = col_btn->get_rgba();
      char buf[8];
      std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", (int)(c.get_red() * 255),
                    (int)(c.get_green() * 255), (int)(c.get_blue() * 255));
      m_focus_text_color = buf;
      m_prefs.focus_text_color = m_focus_text_color;
      if (m_focus_color_css) {
        std::string css =
            "textview text { color: " + m_focus_text_color + "; }";
        m_focus_color_css->load_from_data(css);
      }
      try {
        m_prefs.save();
      } catch (...) {
      }
    });

    // Right-click to reset color
    auto gc_col = Gtk::GestureClick::create();
    gc_col->set_button(3);
    gc_col->signal_pressed().connect([this, col_btn](int, double, double) {
      m_focus_text_color.clear();
      m_prefs.focus_text_color.clear();
      if (m_focus_color_css)
        m_focus_color_css->load_from_data("textview text {}");
      col_btn->set_rgba(Gdk::RGBA("white"));
      try {
        m_prefs.save();
      } catch (...) {
      }
    });
    col_btn->add_controller(gc_col);

    bar_box->append(*sep2);
    bar_box->append(*f_icon);
    bar_box->append(*f_btn);
    bar_box->append(*sz_dn);
    bar_box->append(*m_focus_size_lbl);
    bar_box->append(*sz_up);
    bar_box->append(*sep3);
    bar_box->append(*ls_icon);
    bar_box->append(*ls_scale);
    bar_box->append(*m_focus_ls_lbl);
    bar_box->append(*sep4);
    bar_box->append(*col_btn);

    m_scroll_overlay.add_overlay(*bar_box);
    m_focus_width_bar = bar_box;
  } else {
    // Sync all labels to current focus values when re-entering focus
    if (m_focus_zoom_lbl)
      m_focus_zoom_lbl->set_text(
          std::to_string((int)std::round(m_focus_zoom_factor * 100.0)));
    if (m_focus_font_lbl)
      m_focus_font_lbl->set_text(m_focus_font.empty() ? m_prefs.editor_font
                                                      : m_focus_font);
    if (m_focus_size_lbl) {
      int sz =
          m_focus_font_size > 0 ? m_focus_font_size : m_prefs.editor_font_size;
      m_focus_size_lbl->set_text(std::to_string(sz));
    }
    if (m_focus_ls_lbl) {
      double ls = m_focus_line_spacing > 0.0 ? m_focus_line_spacing
                                             : m_prefs.line_spacing;
      char b[8];
      std::snprintf(b, sizeof(b), "%.1f", ls);
      m_focus_ls_lbl->set_text(b);
    }
  }

  // Hover-reveal: start hidden, show when mouse is in bottom 15% of viewport
  m_focus_width_bar->set_visible(false);

  m_focus_motion_ctrl = Gtk::EventControllerMotion::create();
  m_focus_motion_ctrl->signal_motion().connect([this](double /*x*/, double y) {
    double h = m_scroll_overlay.get_height();
    bool near_bottom = (h > 0 && y >= h * 0.85);
    if (near_bottom) {
      if (m_focus_hide_conn.connected())
        m_focus_hide_conn.disconnect();
      if (m_focus_width_bar)
        m_focus_width_bar->set_visible(true);
    } else if (m_focus_width_bar && m_focus_width_bar->get_visible()) {
      if (!m_focus_hide_conn.connected()) {
        m_focus_hide_conn = Glib::signal_timeout().connect(
            [this]() -> bool {
              if (m_focus_width_bar)
                m_focus_width_bar->set_visible(false);
              return false;
            },
            2000);
      }
    }
  });
  m_scroll_overlay.add_controller(m_focus_motion_ctrl);
  // Apply typewriter-style padding for focus mode centring
  apply_typewriter_padding();
  apply_base_font_tag();
  apply_zoom_to_font_tags();
  // Defer geometry until fullscreen resize has completed
  Glib::signal_timeout().connect_once(
      [this]() {
        apply_page_geometry();
        scroll_to_cursor_center();
      },
      150);
}

void Editor::exit_focus_mode() {
  if (!m_in_focus)
    return;
  m_in_focus = false;
  if (m_focus_key_ctrl) {
    m_scroll_overlay.remove_controller(m_focus_key_ctrl);
    m_focus_key_ctrl.reset();
  }
  m_toolbar.set_visible(true);
  m_footer.set_visible(true);
  if (m_exit_focus_btn)
    m_exit_focus_btn->set_visible(false);
  if (m_focus_width_bar)
    m_focus_width_bar->set_visible(false);
  // Remove hover-reveal motion controller and cancel any pending hide timer
  if (m_focus_hide_conn.connected())
    m_focus_hide_conn.disconnect();
  if (m_focus_motion_ctrl) {
    m_scroll_overlay.remove_controller(m_focus_motion_ctrl);
    m_focus_motion_ctrl.reset();
  }
  m_paper_card.set_margin_start(40);
  m_paper_card.set_margin_end(40);

  // Save current focus state back to prefs before restoring regular values
  m_prefs.focus_zoom_pct = (int)std::round(m_zoom_factor * 100.0);
  m_prefs.focus_typewriter_mode = m_typewriter_mode;
  m_prefs.focus_page_margin_px = m_focus_page_margin_px;
  m_prefs.focus_font = m_focus_font;
  m_prefs.focus_font_size = m_focus_font_size;
  m_prefs.focus_line_spacing = m_focus_line_spacing;
  m_prefs.focus_text_color = m_focus_text_color;
  try {
    m_prefs.save();
  } catch (...) {
  }

  // Restore regular (non-focus) values
  m_focus_zoom_factor = m_zoom_factor; // keep what user set during focus
  m_zoom_factor = m_saved_zoom_factor;
  if (m_zoom_scale)
    m_zoom_scale->set_value(m_saved_zoom_factor * 100.0);
  m_focus_typewriter = m_typewriter_mode; // keep what user set during focus
  m_typewriter_mode = m_saved_typewriter;
  if (m_btn_typewriter.get_active() != m_typewriter_mode)
    m_btn_typewriter.set_active(m_typewriter_mode);
  apply_focus_style(false);

  // Restore typewriter padding or clear it
  apply_typewriter_padding();
  apply_base_font_tag();
  apply_zoom_to_font_tags();

  // Reapply page geometry AFTER the window has resized from unfullscreen().
  // The hadjustment page_size reflects the old fullscreen width until GTK
  // completes the resize — a short defer ensures we measure the correct size.
  m_paper_card.set_size_request(-1,
                                -1); // clear the fullscreen-computed width now
  Glib::signal_timeout().connect_once([this]() { apply_page_geometry(); }, 150);

  if (m_on_focus_mode)
    m_on_focus_mode(false);
}

// Add top/bottom padding equal to half the viewport so the cursor can
// reach vertical centre at document start/end.  Called when typewriter
// mode is toggled and when entering/exiting focus mode.
void Editor::apply_typewriter_padding() {
  if (m_typewriter_mode || m_in_focus) {
    auto vadj = m_write_scroll.get_vadjustment();
    int page_h = vadj ? std::max(400, (int)(vadj->get_page_size())) : 400;
    m_text_view.set_top_margin(4);
    // Focus mode uses half viewport; typewriter mode needs full viewport
    // because the surrounding card margins (paper_inner 52 + paper_card 28)
    // eat into the usable scroll range.
    m_text_view.set_bottom_margin(m_in_focus ? page_h / 2 : page_h);
    m_paper_inner.set_margin_top(52);
    m_paper_inner.set_margin_bottom(52);
  } else {
    m_text_view.set_top_margin(4);
    m_text_view.set_bottom_margin(0);
    m_paper_inner.set_margin_top(52);
    m_paper_inner.set_margin_bottom(52);
  }
}

}  // namespace Folio
