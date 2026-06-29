#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — Sidebar.hpp
// ─────────────────────────────────────────────────────────────────────────────

#include <gtkmm.h>
#include "DocumentModel.hpp"
#include "FolioPrefs.hpp"
#include <functional>
#include <set>
#include <vector>

namespace Folio {

// s20: the Sidebar's internal positional working set for selection. BoardItem
// (the cross-boundary identity) is iid-keyed; the sidebar is an intrinsically
// positional widget — rows live at indices, range-select walks row indices, DnD
// computes target paths — and it rebuilds its rows from the model on every
// structural change, so this set is transient-by-construction with no stale-path
// hazard. It mints iid-keyed BoardItems only at its callback edge. Declared at
// namespace scope so file-scope helpers in Sidebar.cpp can name it.
struct SelPath {
    Section          section = Section::Manuscript;
    std::vector<int> path;
    static SelPath make(Section s, std::vector<int> p) {
        return { s, std::move(p) };
    }
    bool operator<(const SelPath& o) const {
        if (section != o.section) return section < o.section;
        return path < o.path;
    }
    bool operator==(const SelPath& o) const {
        return section == o.section && path == o.path;
    }
};

class Sidebar : public Gtk::Box {
public:
    // All selection callbacks carry (section, path) — uniform across all three
    // binder sections. No more separate char_idx / place_idx integer callbacks.
    using NodeSelectedCallback   = std::function<void(Section, std::vector<int>)>;
    using NodeOpenedCallback     = std::function<void(Section, std::vector<int>)>;
    // Fired when an in-binder rename commits. The host refreshes any open
    // surface's displayed title WITHOUT a full reload (which would reset caret/
    // scroll). Selection moved to the board model, so the old reselect-to-sync
    // path is a no-op for the already-open node — this is the title-only channel.
    using NodeRenamedCallback    = std::function<void(Section, std::vector<int>)>;
    using BoardSelectionCallback = std::function<void(std::vector<BoardItem>)>;
    // Fired after any move (DnD or keyboard). Each pair is { old_path, new_path }.
    using NodesMovedCallback     = std::function<void(Section, std::vector<std::pair<std::vector<int>,std::vector<int>>>)>;
    // Fired when user requests split-on-separator from binder context menu.
    using SplitNodeCallback      = std::function<void(Section, std::vector<int>)>;
    // Fired when user requests combine from binder multi-select context menu.
    // Paths are in document order.
    using CombineNodesCallback   = std::function<void(Section, std::vector<std::vector<int>>)>;
    // s89 — request a Scene↔Group conversion for a Manuscript node (the host
    // flips the kind in place, rebuilds the sidebar, and refreshes the lens).
    using ConvertNodeCallback    = std::function<void(Section, std::vector<int>)>;
    // Fired when user escalates sidebar filter to global search dialog.
    using GlobalSearchCallback   = std::function<void(const std::string& query)>;
    // s38 — request the schema builder for a Template binder node (by iid).
    using EditTemplateCallback   = std::function<void(const std::string& tpl_iid)>;
    // s53 — fired just BEFORE node(s) are trashed/removed, so the host can drop
    // any editor/inspector still bound to a node in the deleted set (else its raw
    // m_current_node dangles into the erased vector → use-after-free). Paths are
    // in the section about to be mutated.
    using BeforeRemoveCallback   = std::function<void(Section, const std::vector<std::vector<int>>&)>;

    explicit Sidebar(DocumentModel& model, FolioPrefs& prefs);

    void set_node_selected_callback(NodeSelectedCallback cb)   { m_on_selected   = std::move(cb); }
    void set_node_renamed_callback(NodeRenamedCallback cb)     { m_on_renamed    = std::move(cb); }
    // s88 — fired when an inline rename starts/ends, so the host can stop the
    // editor stealing focus from the rename entry while it's open.
    void set_rename_begin_callback(std::function<void()> cb)   { m_on_rename_begin = std::move(cb); }
    void set_rename_end_callback(std::function<void()> cb)     { m_on_rename_end   = std::move(cb); }
    void set_node_opened_callback(NodeOpenedCallback cb)       { m_on_opened     = std::move(cb); }
    void set_edit_template_callback(EditTemplateCallback cb)   { m_on_edit_template = std::move(cb); }
    void set_before_remove_callback(BeforeRemoveCallback cb)   { m_on_before_remove = std::move(cb); }
    void set_board_selection_callback(BoardSelectionCallback cb){ m_on_board_sel = std::move(cb); }
    // s20: the internal set is positional (SelPath); convert to iid-keyed
    // BoardItems at the edge. A row whose node no longer resolves is dropped.
    std::vector<BoardItem> get_board_selection() const {
      std::vector<BoardItem> out;
      for (const auto& s : m_board_selection) {
        const BinderNode* n = m_model.node_at(s.section, s.path);
        if (n) out.push_back(BoardItem::make(s.section, n->iid));
      }
      return out;
    }
    // Returns true if the group node at section/path is expanded in the sidebar
    bool is_node_expanded(Section section, const std::vector<int>& path) const {
      for (const auto& ce : m_collapse_entries)
        if (ce.section == section && ce.path == path)
          return ce.expanded;
      return true; // not found → assume expanded
    }
    void set_allow_cross_category(bool allow) { m_allow_cross_category = allow; }
    void set_nodes_moved_callback(NodesMovedCallback cb)       { m_on_nodes_moved = std::move(cb); }
    void set_split_node_callback(SplitNodeCallback cb)         { m_on_split_node  = std::move(cb); }
    void set_combine_nodes_callback(CombineNodesCallback cb)   { m_on_combine     = std::move(cb); }
    void set_convert_node_callback(ConvertNodeCallback cb)     { m_on_convert_node = std::move(cb); }
    void set_global_search_callback(GlobalSearchCallback cb)   { m_on_global_search = std::move(cb); }

    void rebuild();
    // s20: external restores address the node by stable iid (the edge); the
    // sidebar resolves it to the current row internally.
    void set_active(Section section, const std::string& iid);
    void set_selection(const std::vector<BoardItem>& items); // restore full multi-selection (iid-keyed)
    void refresh_session();
    void expand_all_in_section(Section section);
    void collapse_all_in_section(Section section);
    // Expand/collapse a whole subtree rooted at `root` (an empty root == every
    // group in the section). Used by the disclosure triangle's Ctrl+click and by
    // the section-wide expand/collapse helpers above.
    void set_subtree_expanded(Section section, const std::vector<int>& root, bool expand);
    bool any_collapsed_in_subtree(Section section, const std::vector<int>& root) const;

    // ── Disclosure state: read back (for saving to prefs on close) ────────────
    bool sec_manuscript_expanded() const { return m_sec_manuscript.expanded; }
    bool sec_characters_expanded() const { return m_sec_characters.expanded; }
    bool sec_places_expanded()     const { return m_sec_places.expanded; }
    bool sec_references_expanded() const { return m_sec_references.expanded; }
    bool sec_templates_expanded()  const { return m_sec_templates.expanded; }
    bool sec_trash_expanded()      const { return m_sec_trash.expanded; }
    bool pomo_tile_expanded()      const { return m_pomo_tile_expanded; }
    bool session_tile_expanded()   const { return m_session_tile_expanded; }

    // ── Disclosure state: apply (called after prefs are loaded) ───────────────
    void apply_disclosure_state(bool manuscript, bool characters, bool places,
                                bool pomo_tile, bool session_tile,
                                bool references, bool templates, bool trash);

    // Called by MainWindow to push live timer state into the tile
    // progress: 0.0–1.0 elapsed fraction, remaining_sec: seconds left,
    // running: true while ticking, phase_label: "Focus" / "Short Break" / etc.
    void refresh_pomodoro_tile(double progress, int remaining_sec,
                               bool running, const std::string& phase_label,
                               int session_in_cycle, int sessions_before_long);

    // Callback fired whenever any disclosure state changes (for auto-save)
    using DisclosureChangedCallback = std::function<void()>;
    void set_disclosure_changed_callback(DisclosureChangedCallback cb) {
        m_on_disclosure_changed = std::move(cb);
    }
    using PomodoroTileClickedCallback = std::function<void()>;
    void set_pomodoro_tile_clicked_callback(PomodoroTileClickedCallback cb) {
        m_on_pomodoro_tile_clicked = std::move(cb);
    }

    // Callback fired when tile play/pause button is clicked
    using PomodoroTilePlayPauseCallback = std::function<void()>;
    void set_pomodoro_tile_play_pause_callback(PomodoroTilePlayPauseCallback cb) {
        m_on_pomodoro_tile_play_pause = std::move(cb);
    }

    // Callback fired when the tile settings button is clicked
    using PomodoroTileSettingsCallback = std::function<void()>;
    void set_pomodoro_tile_settings_callback(PomodoroTileSettingsCallback cb) {
        m_on_pomodoro_tile_settings = std::move(cb);
    }

    // Keyboard-triggered add: section-specific hotkeys
    void add_scene_to_active();
    void add_character_to_active();
    void add_place_to_active();
    void add_reference_to_active();
    void add_template_to_active();
    void add_group_to_manuscript();
    void add_group_to_characters();
    void add_group_to_places();

    // Template picker popover — anchored to `anchor`, calls on_chosen with selected template
    // s39/slice3 — `category` (character/place/reference) filters the picker to
    // that category's templates and, when set, to DOC-LOCAL templates only (the
    // instance path; globals are cross-project boilerplate, reconciled later).
    // Empty category = legacy behavior (all doc + global templates).
    void show_template_picker(Gtk::Widget* anchor,
                              std::function<void(const BinderNode&)> on_chosen,
                              const std::string& category = "");

private:
    // ── Build / rebuild ───────────────────────────────────────────────────────
    void build_ui();
    void build_section_header(Section kind);
    void build_pomodoro_tile();   // NEW: mini Pomodoro tile above session card
    void build_session_footer();

    void rebuild_section(Section kind);

    // ── Recursive node renderer ───────────────────────────────────────────────
    void add_node_recursive(Section section,
                            const std::vector<int>& path,
                            int depth,
                            Gtk::Box* parent_box);
    void add_global_template_row(int global_idx, Gtk::Box* parent_box);

    // ── Context menus ─────────────────────────────────────────────────────────
    void show_section_ctx_menu(Section kind, double x, double y, Gtk::Widget* anchor);
    void show_node_ctx_menu(Section section,
                            const std::vector<int>& path,
                            double x, double y,
                            Gtk::Widget* anchor);
    void popup_menu(Glib::RefPtr<Gio::Menu> menu,
                    Glib::RefPtr<Gio::SimpleActionGroup> ag,
                    Gtk::Widget* anchor, double x, double y);

    // ── Mutations ─────────────────────────────────────────────────────────────
    void on_add_group(Section section, const std::vector<int>& parent_path);
    void on_add_leaf(Section section, const std::vector<int>& parent_path);
    // s52 — inline rename: double-click a binder row opens a small entry popover
    // over it; commit writes node->title, rebuilds the section, reloads the editor.
    void begin_rename(Section section, const std::vector<int>& path);
    void on_remove_node(Section section, const std::vector<int>& path);
    void on_remove_selected(Section section);

    // ── Multi-select guard ────────────────────────────────────────────────────
    void show_cross_category_dialog();

    // ── Selection / highlight helpers ─────────────────────────────────────────
    void fire_board_selection();
    void refresh_all_highlights();
    // Returns true if item is selected directly or via a selected ancestor
    // Explodes the selected ancestor of item into all its children,
    // removing the ancestor from m_board_selection. Used before deselecting
    // an item that is only selected via ancestor inheritance.

    // ── Collapse tracking ─────────────────────────────────────────────────────
    struct CollapseEntry {
        Section          section;
        std::vector<int> path;
        Gtk::Revealer*   revealer = nullptr;
        Gtk::Label*      arrow    = nullptr;
        bool             expanded = true;
        std::string      iid;          // s89 — stable key for collapse persistence
    };
    std::vector<CollapseEntry> m_collapse_entries;
    void toggle_node(int idx);

    // ── Row tracking for highlight ─────────────────────────────────────────────
    struct RowEntry {
        Section          section;
        std::vector<int> path;
        Gtk::Widget*     row = nullptr;
        Gtk::Label*      title_lbl = nullptr;  // in-place rename: the title widget
        Gtk::Box*        title_box = nullptr;  // its parent box (where we swap an entry in)
    };
    std::vector<RowEntry> m_row_entries;

    // ── Board multi-selection ─────────────────────────────────────────────────
    // s20: the internal working set is the namespace-scope positional SelPath
    // (defined above the class so file-scope helpers in Sidebar.cpp can use it).
    // The sidebar mints iid-keyed BoardItems only at its callback edge
    // (fire_board_selection / get_board_selection).
    // Anchor for Shift+Click range selection — set on any non-Shift click.
    SelPath m_selection_anchor;
    // Anchor item for Shift-click range selection
    SelPath m_last_selected;
    std::set<SelPath> m_board_selection;

    // ── Context menu popover (single reused instance) ─────────────────────────
    Gtk::PopoverMenu* m_ctx_popover = nullptr;

    // ── Widgets ───────────────────────────────────────────────────────────────
    Gtk::SearchEntry    m_search_entry;
    Gtk::ScrolledWindow m_scroll;
    Gtk::Box            m_scroll_content;
    Gtk::Box*           m_manuscript_box = nullptr;
    Gtk::Box*           m_characters_box = nullptr;
    Gtk::Box*           m_places_box     = nullptr;
    Gtk::Box*           m_references_box = nullptr;
    Gtk::Box*           m_templates_box  = nullptr;
    Gtk::Box*           m_trash_box      = nullptr;
    std::string         m_search_filter;
    Gtk::LevelBar       m_session_bar;
    Gtk::Label*         m_session_words_lbl = nullptr;
    Gtk::Label*         m_session_target_lbl = nullptr;
    Gtk::Label*         m_session_pct_lbl   = nullptr;

    // ── Pomodoro disclosure tile ──────────────────────────────────────────────
    Gtk::DrawingArea*   m_pomo_ring             = nullptr;
    Gtk::Label*         m_pomo_time_lbl         = nullptr;
    Gtk::Label*         m_pomo_phase_lbl        = nullptr;
    Gtk::Box*           m_pomo_dot_row          = nullptr;
    Gtk::Revealer*      m_pomo_tile_revealer    = nullptr;
    Gtk::Label*         m_pomo_tile_arrow       = nullptr;
    bool                m_pomo_tile_expanded    = true;
    double              m_pomo_progress         = 0.0;
    bool                m_pomo_running          = false;
    std::string         m_pomo_phase_str        = "Focus";
    int                 m_pomo_session_in_cycle     = 0;
    int                 m_pomo_sessions_before_long = 4;
    PomodoroTileClickedCallback   m_on_pomodoro_tile_clicked;
    PomodoroTilePlayPauseCallback m_on_pomodoro_tile_play_pause;
    PomodoroTileSettingsCallback  m_on_pomodoro_tile_settings;
    Gtk::Button*                  m_pomo_tile_play_btn = nullptr;
    DisclosureChangedCallback     m_on_disclosure_changed;

    // ── Session tile disclosure ───────────────────────────────────────────────
    Gtk::Revealer*      m_session_tile_revealer = nullptr;
    Gtk::Label*         m_session_tile_arrow    = nullptr;
    bool                m_session_tile_expanded = true;

    // ── Section collapse state (survives rebuild) ─────────────────────────────
    struct SectionHeader {
        Gtk::Revealer* revealer = nullptr;
        Gtk::Label*    arrow    = nullptr;
        bool           expanded = true;
    };
    SectionHeader m_sec_manuscript;
    SectionHeader m_sec_characters;
    SectionHeader m_sec_places;
    SectionHeader m_sec_references;
    SectionHeader m_sec_templates;
    SectionHeader m_sec_trash;

    SectionHeader& section_header(Section s) {
        if (s == Section::Characters) return m_sec_characters;
        if (s == Section::Places)     return m_sec_places;
        if (s == Section::References) return m_sec_references;
        if (s == Section::Templates)  return m_sec_templates;
        if (s == Section::Trash)      return m_sec_trash;
        return m_sec_manuscript;
    }
    void toggle_section(Section s);

    // ── Model / prefs ─────────────────────────────────────────────────────────
    DocumentModel& m_model;
    FolioPrefs&    m_prefs;

    // ── Callbacks ─────────────────────────────────────────────────────────────
    NodeSelectedCallback   m_on_selected;
    NodeRenamedCallback    m_on_renamed;
    std::function<void()>  m_on_rename_begin;
    std::function<void()>  m_on_rename_end;
    NodeOpenedCallback     m_on_opened;
    BeforeRemoveCallback   m_on_before_remove;
    EditTemplateCallback   m_on_edit_template;   // s38
    BoardSelectionCallback m_on_board_sel;
    NodesMovedCallback     m_on_nodes_moved;
    SplitNodeCallback      m_on_split_node;
    CombineNodesCallback   m_on_combine;
    ConvertNodeCallback    m_on_convert_node;   // s89
    GlobalSearchCallback   m_on_global_search;
    bool                   m_allow_cross_category = false;

    // ── Drag-and-drop state ───────────────────────────────────────────────────
    enum class DropZone { None, Before, After, Inside };

    struct DragState {
        bool             active        = false;
        bool             was_dragged   = false;  // set on first motion, cleared after release
        Section          section       = Section::Manuscript;
        std::vector<int> path;
        std::vector<std::vector<int>> all_paths;
        Gtk::Widget*     source_widget = nullptr;
    } m_drag;

    struct DropTarget {
        Gtk::Widget*     widget  = nullptr;
        DropZone         zone    = DropZone::None;
    } m_drop_target;

    // Drag helpers
    void setup_drag_source(Gtk::Widget* widget, Section section, const std::vector<int>& path);
    void setup_drop_target(Gtk::Widget* widget, Section section, const std::vector<int>& path, bool is_group);
    void clear_drop_highlight();
    void apply_drop_highlight(Gtk::Widget* widget, DropZone zone);
    void execute_drop(Section section, const std::vector<int>& target_path, DropZone zone,
                      const std::vector<std::vector<int>>& src_paths);
    DropZone compute_drop_zone(Gtk::Widget* widget, double y, bool is_group) const;
};

} // namespace Folio
