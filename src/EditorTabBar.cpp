// ─────────────────────────────────────────────────────────────────────────────
// Folio — EditorTabBar.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "EditorTabBar.hpp"
#include "Iid.hpp"
#include <algorithm>
#include <cmath>
#include <gtk/gtk.h>
#include <graphene.h>
#include <gtkmm/dragsource.h>
#include <gtkmm/droptarget.h>
#include <gdkmm/contentprovider.h>
#include <memory>

namespace Folio {

EditorTabBar::EditorTabBar(DocumentModel& model, FolioPrefs& prefs)
    : Gtk::Box(Gtk::Orientation::HORIZONTAL)
    , m_model(model)
    , m_prefs(prefs)
    , m_tab_row(Gtk::Orientation::HORIZONTAL, 2)
{
    add_css_class("folio-timeline");
    m_tab_row.set_valign(Gtk::Align::CENTER);
    m_tab_row.set_margin_start(4);
    m_tab_row.set_margin_end(4);
    m_scroll.set_child(m_tab_row);
    m_scroll.set_hexpand(true);
    m_scroll.set_vexpand(false);
    m_scroll.set_policy(Gtk::PolicyType::EXTERNAL, Gtk::PolicyType::NEVER);
    m_scroll.set_propagate_natural_height(true);

    // ── Arrow buttons ─────────────────────────────────────────────────────────
    m_btn_prev.set_label("◀");
    m_btn_prev.add_css_class("timeline-arrow-btn");
    m_btn_prev.set_tooltip_text("Previous tab  (Alt+Left)");
    m_btn_prev.set_sensitive(false);
    m_btn_prev.set_visible(false);
    m_btn_prev.signal_clicked().connect([this]() { navigate_prev(); });

    m_btn_next.set_label("▶");
    m_btn_next.add_css_class("timeline-arrow-btn");
    m_btn_next.set_tooltip_text("Next tab  (Alt+Right)");
    m_btn_next.set_sensitive(false);
    m_btn_next.set_visible(false);
    m_btn_next.signal_clicked().connect([this]() { navigate_next(); });

    // Update arrow sensitivity when the scroll position changes
    auto hadj = m_scroll.get_hadjustment();
    if (hadj) {
        hadj->signal_value_changed().connect([this]() { update_arrow_buttons(); });
        hadj->signal_changed().connect([this]() { update_arrow_buttons(); });
    }

    auto* placeholder = Gtk::make_managed<Gtk::Label>(
        "Double-click a scene, character, or place in the binder to open it here");
    placeholder->add_css_class("row-subtitle");
    placeholder->set_valign(Gtk::Align::CENTER);
    placeholder->set_halign(Gtk::Align::START);
    placeholder->set_hexpand(true);
    placeholder->set_margin_start(12);
    placeholder->set_name("timeline-placeholder");

    append(m_btn_prev);
    append(m_scroll);
    append(m_btn_next);
    append(*placeholder);
    m_scroll.set_visible(false);
    placeholder->set_visible(true);

    // Right-click context menu on the tab row
    auto gc = Gtk::GestureClick::create();
    gc->set_button(3);
    gc->signal_pressed().connect([this](int, double x, double y) {
        if (m_tabs.empty()) return;

        auto gm = Gio::Menu::create();
        auto ag = Gio::SimpleActionGroup::create();

        auto sec1 = Gio::Menu::create();
        sec1->append_item(Gio::MenuItem::create("Close All Tabs",   "ctx.close-all"));
        ag->add_action("close-all", [this]() {
            m_selected_tabs.clear();
            clear();
        });

        if (m_active_idx >= 0 && (int)m_tabs.size() > 1) {
            sec1->append_item(Gio::MenuItem::create("Close Other Tabs", "ctx.close-others"));
            OpenTab keep = m_tabs[m_active_idx];
            ag->add_action("close-others", [this, keep]() {
                m_tabs = { keep };
                m_active_idx = 0;
                m_selected_tabs.clear();
                rebuild_chips();
                if (m_on_tab_activated) m_on_tab_activated(m_tabs[0]);
            });
        }
        gm->append_section({}, sec1);

        auto* pop = Gtk::make_managed<Gtk::PopoverMenu>(gm);
        pop->insert_action_group("ctx", ag);
        pop->set_parent(m_scroll);
        pop->set_has_arrow(false);
        Gdk::Rectangle r;
        r.set_x((int)x); r.set_y((int)y); r.set_width(1); r.set_height(1);
        pop->set_pointing_to(r);
        pop->signal_closed().connect([pop]() {
            Glib::signal_idle().connect_once([pop]() { pop->unparent(); });
        });
        pop->popup();
    }, false);
    m_scroll.add_controller(gc);
}

void EditorTabBar::navigate_prev() {
    if (m_tabs.empty()) return;
    int target = (m_active_idx > 0) ? m_active_idx - 1 : (int)m_tabs.size() - 1;
    activate_tab(target);
}

void EditorTabBar::navigate_next() {
    if (m_tabs.empty()) return;
    int target = (m_active_idx < (int)m_tabs.size() - 1) ? m_active_idx + 1 : 0;
    activate_tab(target);
}

void EditorTabBar::update_arrow_buttons() {
    bool has_tabs = !m_tabs.empty();
    m_btn_prev.set_visible(has_tabs);
    m_btn_next.set_visible(has_tabs);
    if (!has_tabs) return;

    auto hadj = m_scroll.get_hadjustment();
    if (hadj) {
        double val   = hadj->get_value();
        double upper = hadj->get_upper() - hadj->get_page_size();
        m_btn_prev.set_sensitive(val > 1.0);
        m_btn_next.set_sensitive(val < upper - 1.0);
    } else {
        m_btn_prev.set_sensitive(m_active_idx > 0);
        m_btn_next.set_sensitive(m_active_idx < (int)m_tabs.size() - 1);
    }
}

int EditorTabBar::find_tab(const OpenTab& tab) const {
    for (int i = 0; i < (int)m_tabs.size(); ++i)
        if (m_tabs[i] == tab) return i;
    return -1;
}

void EditorTabBar::open_node(Section section, const std::vector<int>& path) {
    OpenTab tab;
    tab.section = section;
    tab.path    = path;
    int existing = find_tab(tab);
    if (existing >= 0) { activate_tab(existing); return; }
    m_tabs.push_back(std::move(tab));
    rebuild_chips();
    activate_tab((int)m_tabs.size() - 1);
}

void EditorTabBar::activate_tab(int idx) {
    if (idx < 0 || idx >= (int)m_tabs.size()) return;
    m_active_idx = idx;
    rebuild_chips();
    if (m_on_tab_activated) m_on_tab_activated(m_tabs[idx]);
}

void EditorTabBar::refresh_active() {
    for (int i = 0; i < (int)m_tabs.size(); ++i) {
        const auto& t = m_tabs[i];
        if (t.section == m_model.active_section && t.path == m_model.active_path) {
            m_active_idx = i;
            rebuild_chips();
            return;
        }
    }
}

void EditorTabBar::refresh_tab_title(Section section, const std::vector<int>& path) {
    for (const auto& t : m_tabs) {
        if (t.section == section && t.path == path) {
            rebuild_chips();
            return;
        }
    }
}

void EditorTabBar::notify_node_moved(Section section,
                                       const std::vector<int>& old_path,
                                       const std::vector<int>& new_path)
{
    bool changed = false;
    for (auto& tab : m_tabs) {
        if (tab.section != section) continue;

        // Exact match — this tab IS the moved node
        if (tab.path == old_path) {
            tab.path = new_path;
            changed = true;
            continue;
        }

        // Child of the moved node — rebase the prefix
        if (tab.path.size() > old_path.size()) {
            bool is_child = true;
            for (int i = 0; i < (int)old_path.size(); ++i)
                if (tab.path[i] != old_path[i]) { is_child = false; break; }
            if (is_child) {
                std::vector<int> suffix(tab.path.begin() + old_path.size(), tab.path.end());
                tab.path = new_path;
                tab.path.insert(tab.path.end(), suffix.begin(), suffix.end());
                changed = true;
            }
        }
    }
    if (changed) rebuild_chips();
}

void EditorTabBar::close_tab_for_path(Section section, const std::vector<int>& path) {
    for (int i = 0; i < (int)m_tabs.size(); ++i) {
        if (m_tabs[i].section == section && m_tabs[i].path == path) {
            m_tabs.erase(m_tabs.begin() + i);
            if (m_active_idx == i)
                m_active_idx = std::min(i, (int)m_tabs.size() - 1);
            else if (m_active_idx > i)
                --m_active_idx;
            rebuild_chips();
            if (m_active_idx >= 0 && m_on_tab_activated)
                m_on_tab_activated(m_tabs[m_active_idx]);
            else if (m_tabs.empty() && m_on_tab_activated) {
                OpenTab empty; m_on_tab_activated(empty);
            }
            return;
        }
    }
}

void EditorTabBar::clear() {
    m_tabs.clear();
    m_active_idx = -1;
    m_selected_tabs.clear();
    rebuild_chips();
}

std::string EditorTabBar::tab_label(const OpenTab& tab) const {
    const BinderNode* node = m_model.node_at(tab.section, tab.path);
    if (!node) return "(deleted)";
    return node->title.empty() ? "Untitled" : node->title;
}

std::string EditorTabBar::color_bar_suffix(const OpenTab& tab) const {
    const BinderNode* node = m_model.node_at(tab.section, tab.path);
    int idx = node ? node->color_idx : 0;
    std::string hex = m_prefs.color_hex_for_idx(idx);
    return hex.empty() ? "#89dceb" : hex; // fallback to sky
}

void EditorTabBar::setup_chip_dnd(Gtk::Widget* chip, int idx)
{
    auto alive = std::make_shared<bool>(true);
    chip->signal_destroy().connect([alive]{ *alive = false; });

    // ── Drag source ──────────────────────────────────────────────────────────
    auto src = Gtk::DragSource::create();
    src->set_actions(Gdk::DragAction::MOVE);

    src->signal_prepare().connect([this, idx](double, double) -> Glib::RefPtr<Gdk::ContentProvider> {
        m_was_dragged  = false;
        m_drag_src_idx = idx;
        // Ensure the dragged chip is in the selection
        if (!m_selected_tabs.count(idx)) {
            m_selected_tabs = { idx };
            rebuild_chips();
        }
        Glib::Value<int> val;
        val.init(G_TYPE_INT);
        val.set(idx);
        return Gdk::ContentProvider::create(val);
    }, false);

    src->signal_drag_begin().connect([chip, alive](const Glib::RefPtr<Gdk::Drag>&) {
        if (!*alive) return;
        chip->add_css_class("chip-drag-source");
    }, false);

    src->signal_drag_end().connect([this, chip, alive](const Glib::RefPtr<Gdk::Drag>&, bool) {
        m_drag_src_idx = -1;
        m_was_dragged  = true;
        if (!*alive) return;
        chip->remove_css_class("chip-drag-source");
    }, false);

    chip->add_controller(src);

    // ── Drop target ──────────────────────────────────────────────────────────
    auto dst = Gtk::DropTarget::create(G_TYPE_INT, Gdk::DragAction::MOVE);

    auto clear_highlight = [chip, alive]() {
        if (!*alive) return;
        chip->remove_css_class("chip-drop-before");
        chip->remove_css_class("chip-drop-after");
    };

    dst->signal_motion().connect([this, idx, chip, alive, clear_highlight](double x, double) -> Gdk::DragAction {
        if (!*alive) return Gdk::DragAction{};
        if (m_selected_tabs.count(idx)) return Gdk::DragAction{}; // dragging this chip
        bool after = (x > chip->get_width() * 0.5);
        clear_highlight();
        chip->add_css_class(after ? "chip-drop-after" : "chip-drop-before");
        m_drop_chip  = chip;
        m_drop_after = after;
        return Gdk::DragAction::MOVE;
    }, false);

    dst->signal_leave().connect([this, chip, alive, clear_highlight]() {
        if (!*alive) return;
        clear_highlight();
        if (m_drop_chip == chip) m_drop_chip = nullptr;
    }, false);

    dst->signal_drop().connect([this, idx, chip, alive, clear_highlight]
            (const Glib::ValueBase&, double x, double) -> bool {
        if (!*alive) return false;
        clear_highlight();
        m_drop_chip = nullptr;

        if (m_drag_src_idx < 0) return false;
        if (m_selected_tabs.count(idx)) return false; // dropped onto a selected chip

        bool after = (x > chip->get_width() * 0.5);

        // Collect selected indices sorted ascending
        std::vector<int> srcs(m_selected_tabs.begin(), m_selected_tabs.end());
        std::sort(srcs.begin(), srcs.end());

        // Snapshot the moving tabs
        std::vector<OpenTab> moving;
        for (int s : srcs) moving.push_back(m_tabs[s]);

        // Remove in reverse order so indices stay valid
        for (int i = (int)srcs.size() - 1; i >= 0; --i)
            m_tabs.erase(m_tabs.begin() + srcs[i]);

        // Find where dest chip (idx) landed after removals
        int dest = idx;
        for (int s : srcs)
            if (s < idx) --dest;

        int insert_at = after ? dest + 1 : dest;
        insert_at = std::clamp(insert_at, 0, (int)m_tabs.size());

        // Insert all moving tabs consecutively
        for (int i = (int)moving.size() - 1; i >= 0; --i)
            m_tabs.insert(m_tabs.begin() + insert_at, moving[i]);

        // Update active index
        int old_active = m_active_idx;
        bool active_was_selected = m_selected_tabs.count(old_active) > 0;
        if (active_was_selected) {
            // Find which position in `moving` it was
            int pos_in_moving = 0;
            for (int i = 0; i < (int)srcs.size(); ++i)
                if (srcs[i] == old_active) { pos_in_moving = i; break; }
            m_active_idx = insert_at + pos_in_moving;
        } else {
            // Recompute: count how many selected were before old_active
            int shift = 0;
            for (int s : srcs) if (s < old_active) ++shift;
            int adj = old_active - shift;
            // Count how many inserted before adj
            int ins_shift = (insert_at <= adj) ? (int)moving.size() : 0;
            m_active_idx = adj + ins_shift;
        }
        m_active_idx = std::clamp(m_active_idx, 0, (int)m_tabs.size() - 1);

        m_selected_tabs.clear();
        rebuild_chips();
        return true;
    }, false);

    chip->add_controller(dst);
}

Gtk::Widget* EditorTabBar::make_chip(int idx) {
    const auto& tab     = m_tabs[idx];
    bool        active  = (idx == m_active_idx);
    bool        selected = m_selected_tabs.count(idx) > 0;

    auto* chip = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
    chip->add_css_class("timeline-chip");
    if (active)   chip->add_css_class("active");
    if (selected) chip->add_css_class("chip-selected");

    auto* bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
    bar->set_size_request(3, -1);
    bar->set_valign(Gtk::Align::FILL);
    bar->set_vexpand(true);
    {
        std::string hex = color_bar_suffix(tab);
        auto prov = Gtk::CssProvider::create();
        prov->load_from_data("box { background-color: " + hex + "; }");
        bar->get_style_context()->add_provider(prov, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }

    const BinderNode* node = m_model.node_at(tab.section, tab.path);
    const char* prefix = "";
    if (node) {
        switch (node->kind) {
            case BinderKind::Group:     prefix = "▸ "; break;
            case BinderKind::Character: prefix = "@ "; break;
            case BinderKind::Place:     prefix = "⌖ "; break;
            case BinderKind::Reference: prefix = "⇥ "; break;
            case BinderKind::Template:  prefix = "T "; break;
            default: break;
        }
    }

    std::string display = tab_label(tab);
    if (display.length() > 14) {
        auto dash = display.find("\xe2\x80\x94");
        if (dash != std::string::npos && dash > 0)
            display = display.substr(0, dash - 1);
        if (display.length() > 14)
            display = display.substr(0, 14) + "\xe2\x80\xa6";
    }

    std::string sub_label;
    if (node) {
        switch (node->kind) {
            case BinderKind::Scene:
                { int wc = node->word_count(); sub_label = (wc > 0) ? std::to_string(wc) + "w" : "\xe2\x80\x94"; break; }
            case BinderKind::Group:
                sub_label = std::to_string(node->total_words()) + "w"; break;
            case BinderKind::Character: sub_label = "Character"; break;
            case BinderKind::Place:     sub_label = "Place";     break;
            case BinderKind::Reference: sub_label = "Reference"; break;
            case BinderKind::Template:  sub_label = "Template";  break;
        }
    }

    auto* label_btn = Gtk::make_managed<Gtk::Button>();
    label_btn->add_css_class("context-menu-item");
    label_btn->set_has_frame(false);
    label_btn->set_hexpand(false);
    // s19: name the chip by the open node's iid (model-bound widget).
    if (node) label_btn->set_name(Folio::widget_name("editor-tab", node->iid));

    auto* text_col = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    text_col->set_valign(Gtk::Align::CENTER);
    text_col->set_margin_start(4); text_col->set_margin_end(2);

    auto* name_lbl = Gtk::make_managed<Gtk::Label>(prefix + display);
    name_lbl->add_css_class("chip-name"); name_lbl->set_halign(Gtk::Align::START);
    auto* sub_lbl = Gtk::make_managed<Gtk::Label>(sub_label);
    sub_lbl->add_css_class("chip-wc"); sub_lbl->set_halign(Gtk::Align::START);
    text_col->append(*name_lbl); text_col->append(*sub_lbl);
    label_btn->set_child(*text_col);
    // label_btn's clicked signal handles tab activation (single-select).
    label_btn->signal_clicked().connect([this, idx]() {
        if (m_was_dragged) { m_was_dragged = false; return; }
        m_selected_tabs = { idx };
        activate_tab(idx);
    });

    auto* close_btn = Gtk::make_managed<Gtk::Button>("\xe2\x9c\x95");
    close_btn->add_css_class("icon-btn"); close_btn->add_css_class("flat");
    close_btn->set_tooltip_text("Close tab (item stays in binder)");
    close_btn->set_valign(Gtk::Align::CENTER); close_btn->set_margin_end(2);

    OpenTab closing = tab;
    close_btn->signal_clicked().connect([this, closing]() {
        int i = find_tab(closing);
        if (i < 0) return;
        m_selected_tabs.erase(i);
        // Shift selection indices above i
        std::set<int> new_sel;
        for (int s : m_selected_tabs) new_sel.insert(s > i ? s - 1 : s);
        m_selected_tabs = new_sel;
        m_tabs.erase(m_tabs.begin() + i);
        if (m_active_idx == i)
            m_active_idx = std::min(i, (int)m_tabs.size() - 1);
        else if (m_active_idx > i)
            --m_active_idx;
        rebuild_chips();
        if (m_on_tab_closed) m_on_tab_closed(closing);
        if (m_active_idx >= 0 && m_on_tab_activated)
            m_on_tab_activated(m_tabs[m_active_idx]);
        else if (m_tabs.empty() && m_on_tab_activated) {
            OpenTab empty; m_on_tab_activated(empty);
        }
    });

    chip->append(*bar);
    chip->append(*label_btn);
    chip->append(*close_btn);

    // Ctrl+click on the chip box (captures before label_btn's button gesture)
    auto gc = Gtk::GestureClick::create();
    gc->set_button(1);
    gc->signal_pressed().connect([this, idx, gc](int, double, double) {
        bool ctrl = (gc->get_current_event_state() &
                     Gdk::ModifierType::CONTROL_MASK) != (Gdk::ModifierType)0;
        if (ctrl) {
            gc->set_state(Gtk::EventSequenceState::CLAIMED);
            if (m_selected_tabs.count(idx))
                m_selected_tabs.erase(idx);
            else
                m_selected_tabs.insert(idx);
            rebuild_chips();
        }
        // No ctrl: let the event fall through to label_btn
    }, false);
    chip->add_controller(gc);

    setup_chip_dnd(chip, idx);
    return chip;
}

void EditorTabBar::rebuild_chips() {
    while (auto* child = m_tab_row.get_first_child())
        m_tab_row.remove(*child);

    bool empty = m_tabs.empty();
    {
        auto* child = get_first_child();
        while (child) {
            auto* next = child->get_next_sibling();
            if (child->get_name() == "timeline-placeholder")
                child->set_visible(empty);
            child = next;
        }
    }
    m_scroll.set_visible(!empty);
    for (int i = 0; i < (int)m_tabs.size(); ++i)
        m_tab_row.append(*make_chip(i));

    // Scroll the active chip into view after layout settles
    if (m_active_idx >= 0 && m_active_idx < (int)m_tabs.size()) {
        Glib::signal_idle().connect_once([this]() {
            // Find the active chip widget by walking the tab_row children
            int i = 0;
            auto* child = m_tab_row.get_first_child();
            while (child && i < m_active_idx) {
                child = child->get_next_sibling();
                ++i;
            }
            if (child) {
                // Use compute_bounds to get the chip's x position relative to
                // the scrolled window viewport, then clamp hadjustment.
                auto hadj = m_scroll.get_hadjustment();
                if (hadj) {
                    graphene_rect_t bounds;
                    if (gtk_widget_compute_bounds(child->gobj(),
                                                  GTK_WIDGET(m_tab_row.gobj()),
                                                  &bounds)) {
                        double chip_x     = bounds.origin.x;
                        double chip_w     = bounds.size.width;
                        double page_size  = hadj->get_page_size();
                        double cur_val    = hadj->get_value();
                        double upper      = hadj->get_upper();

                        // If the chip is to the left of the viewport, scroll left
                        if (chip_x < cur_val) {
                            hadj->set_value(std::max(0.0, chip_x - 8.0));
                        }
                        // If the chip is to the right of the viewport, scroll right
                        else if (chip_x + chip_w > cur_val + page_size) {
                            double new_val = chip_x + chip_w - page_size + 8.0;
                            hadj->set_value(std::min(new_val, upper - page_size));
                        }
                    }
                }
            }
            update_arrow_buttons();
        });
    }
    update_arrow_buttons();
}

} // namespace Folio
