#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — EditorTabBar.hpp
// The horizontal tab bar at the top of the editor.
// ─────────────────────────────────────────────────────────────────────────────
#include "DocumentModel.hpp"
#include "FolioPrefs.hpp"
#include <functional>
#include <gtkmm.h>
#include <set>
#include <string>
#include <vector>

namespace Folio {

// ── OpenTab ──────────────────────────────────────────────────────────────────
// Identifies an open tab by (section, path) — uniform across all binder sections.

struct OpenTab {
    Section          section = Section::Manuscript;
    std::vector<int> path;   // empty → sentinel "nothing open"

    bool operator==(const OpenTab& o) const {
        return section == o.section && path == o.path;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// EditorTabBar
// ─────────────────────────────────────────────────────────────────────────────

class EditorTabBar : public Gtk::Box {
public:
    explicit EditorTabBar(DocumentModel& model, FolioPrefs& prefs);

    // Open (or activate) a tab for any node in any section.
    void open_node(Section section, const std::vector<int>& path);

    // Refresh highlight to match model's current active_path without firing callback.
    void refresh_active();

    // Rebuild the chip label for the given path (called after title change).
    void refresh_tab_title(Section section, const std::vector<int>& path);

    // Update a tab's stored path after the node has been moved in the model.
    void notify_node_moved(Section section,
                           const std::vector<int>& old_path,
                           const std::vector<int>& new_path);

    // Remove the tab for a deleted node.
    void close_tab_for_path(Section section, const std::vector<int>& path);

    // Close all tabs (new/open project).
    void clear();

    // Close all tabs via hotkey or menu.
    void close_all_tabs() { m_selected_tabs.clear(); clear(); }

    // Called when a tab is clicked or closed. OpenTab.path empty → nothing open.
    using TabCallback = std::function<void(const OpenTab&)>;
    void set_tab_activated_callback(TabCallback cb) { m_on_tab_activated = std::move(cb); }
    void set_tab_closed_callback(TabCallback cb)    { m_on_tab_closed    = std::move(cb); }

    // Navigate prev/next tab (for hotkeys and arrow buttons)
    void navigate_prev();
    void navigate_next();

    // Activate a tab by index (public, for restore after load)
    void activate_tab_public(int idx) { activate_tab(idx); }

    // Access current tab list (used by MainWindow to reactivate on load).
    const std::vector<OpenTab>& tabs() const { return m_tabs; }
    int active_idx() const { return m_active_idx; }

private:
    DocumentModel& m_model;
    FolioPrefs&    m_prefs;

    std::vector<OpenTab> m_tabs;
    int                  m_active_idx = -1;

    Gtk::ScrolledWindow  m_scroll;
    Gtk::Box             m_tab_row;
    Gtk::Button          m_btn_prev;   // ◀ scroll left / prev tab
    Gtk::Button          m_btn_next;   // ▶ scroll right / next tab

    void update_arrow_buttons(); // show/hide/sensitise based on scroll position

    TabCallback m_on_tab_activated;
    TabCallback m_on_tab_closed;

    int          find_tab(const OpenTab& tab) const;
    void         activate_tab(int idx);
    std::string  tab_label(const OpenTab& tab) const;
    std::string  color_bar_suffix(const OpenTab& tab) const;
    Gtk::Widget* make_chip(int idx);
    void         rebuild_chips();
    void         setup_chip_dnd(Gtk::Widget* chip, int idx);

    // Tab selection (for multi-select DnD)
    std::set<int> m_selected_tabs;   // indices into m_tabs

    // Drag state
    int           m_drag_src_idx = -1;   // tab index drag started on
    bool          m_was_dragged  = false; // set in drag_end, cleared on release
    Gtk::Widget*  m_drop_chip    = nullptr;
    bool          m_drop_after   = false;
};

} // namespace Folio
