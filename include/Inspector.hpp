#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — Inspector.hpp
// Context-sensitive metadata panel.
// Tabs: Metadata | Notes | History | Project
// ─────────────────────────────────────────────────────────────────────────────
#include "DocumentModel.hpp"
#include "ObjectForm.hpp"   // s31/s32 — template-driven object form (editable: name/image/buffer)
#include "BarcodeGenerator.hpp"
#include "FolioPrefs.hpp"
#include "AnnotationReportDialog.hpp"
#include "ProjectGoalDialog.hpp"
#include <functional>
#include <gtkmm.h>
#include <memory>
#include <string>
#include <vector>

namespace Folio {
class BarcodeDialog; // forward declaration — defined in BarcodeDialog.hpp
class TemplateBuilderDialog; // forward declaration — defined in TemplateBuilderDialog.hpp

using MetaChangedCallback    = std::function<void(BinderNode*)>;
using ContentChangedCallback = std::function<void(BinderNode*)>;
using ToastCallback          = std::function<void(const std::string&)>;

class Inspector : public Gtk::Box {
public:
    ~Inspector(); // defined in Inspector.cpp where BarcodeDialog is complete
    explicit Inspector(DocumentModel& model, FolioPrefs& prefs);

    void load_node(BinderNode* node);
    void load_empty();
    void load_project();
    void load_joined_nodes(const std::vector<BinderNode*>& nodes);
    void refresh_project_tab();
    void refresh_prefs_dropdowns();
    void focus_meta_tab();   // switch to Metadata tab without reloading the node

    // Navigate directly to a named inspector tab:
    // 0 = Project, 1 = Metadata, 2 = Notes, 3 = Snapshots
    void navigate_to_tab(int idx);
    void refresh_history();  // refresh the Snapshots tab (called by Editor after toolbar save)

    // Progress footer disclosure
    bool progress_expanded() const;
    void set_progress_expanded(bool expanded);

    void set_meta_changed_callback(MetaChangedCallback cb);
    void set_content_changed_callback(ContentChangedCallback cb);
    // Progress footer disclosure callback (bool = new expanded state)
    void set_progress_disclosure_callback(std::function<void(bool)> cb);
    void set_toast_callback(ToastCallback cb);

    // s38 — open the schema builder on a TEMPLATE BINDER NODE (the merge's authoring
    // entry; called from MainWindow's Sidebar "Edit Form…" wire). Seeds from the
    // node's form_schema (or the Character floor when empty/new); on commit writes
    // the edited schema back to the node and re-projects the store.
    void open_template_builder_for_template_node(const std::string& node_iid);

    // s41 — open the schema builder for the CURRENT object's template (the
    // instance "Edit fields…" door). Public so MainWindow can route the Editor
    // form's door here. On commit it fires the object-form dirty callback so the
    // Editor (which now hosts the form) re-renders.
    void open_template_builder_for_current();
    using ObjectFormDirtyCallback = std::function<void()>;
    void set_object_form_dirty_callback(ObjectFormDirtyCallback cb) {
      m_on_object_form_dirty = std::move(cb);
    }

    // Annotations — called by Editor when annotations change
    void notify_annotations_changed();

    // Tools-menu entry points — open dialogs from outside the inspector
    void open_annotation_report();
    void open_barcode();
    void open_project_goals();

    // Callbacks wired from MainWindow → Editor
    std::function<void(int)>  on_scroll_to_annotation;
    std::function<void(int)>  on_delete_annotation;
    std::function<void(int, const std::string&)> on_edit_annotation_text;
    // JV-aware variants — carry the owning node so Editor can route correctly
    std::function<void(BinderNode*, int)>  on_delete_annotation_from_node;
    std::function<void(BinderNode*, int, const std::string&, const std::string&, const std::string&)> on_edit_annotation_on_node;

private:
    DocumentModel& m_model;
    FolioPrefs&    m_prefs;

    enum class InspectorMode { Empty, Node, Character, Place, Reference, Template, Project };
    InspectorMode   m_mode             = InspectorMode::Empty;
    BinderNode* m_current_node     = nullptr;
    
    
    bool            m_loading          = false;

    // ── Joined View state ─────────────────────────────────────────────────────
    bool                      m_in_jv_mode = false;
    std::vector<BinderNode*>  m_jv_nodes;           // ordered list of JV segments

    MetaChangedCallback    m_on_meta_changed;
    ObjectFormDirtyCallback m_on_object_form_dirty;   // s41 — Editor re-renders its form
    ContentChangedCallback m_on_content_changed;
    ToastCallback          m_on_toast;
    std::function<void(bool)> m_on_progress_disclosure_changed;
    void notify_meta_changed();
    void notify_content_changed();
    void notify_toast(const std::string& msg);

    // ── Tab bar ───────────────────────────────────────────────────────────────
    Gtk::Box          m_tab_bar;
    Gtk::ToggleButton m_tab_meta;
    Gtk::ToggleButton m_tab_notes;
    Gtk::ToggleButton m_tab_history;
    Gtk::ToggleButton m_tab_project;
    Gtk::ToggleButton m_tab_annotations; // kept for compatibility but hidden
    Gtk::Stack        m_stack;

    void build_tab_bar();
    void show_tab(int idx);

    // ── Annotations tab ───────────────────────────────────────────────────────
    Gtk::ScrolledWindow  m_ann_scroll;
    Gtk::Box             m_ann_box;
    void build_annotations_tab();
    void refresh_annotations();
    std::unique_ptr<AnnotationReportDialog> m_ann_report;  // rebuild card list from current node

    // ── Metadata tab ──────────────────────────────────────────────────────────
    Gtk::Box             m_meta_wrapper;  // vertical: scroll + progress footer
    Gtk::ScrolledWindow  m_meta_scroll;
    Gtk::Box             m_meta_box;

    // Node fields
    Gtk::Entry        m_title_entry;
    Gtk::TextView     m_synopsis_view;
    Glib::RefPtr<Gtk::TextBuffer> m_synopsis_buffer;
    Gtk::DropDown*    m_status_dropdown = nullptr;
    Gtk::DropDown*    m_pov_dropdown    = nullptr;
    Gtk::DropDown*    m_color_dropdown  = nullptr;
    Gtk::Label        m_tag_value;        // s23: scene's KP tag, shown under Label
    Gtk::ToggleButton m_pin_toggle;       // s30: per-scene pin (hinge), beside Tag
    // s30 — per-scene energies are now editable sliders (Pacing=frenetic,
    // Tension=arc). The adjustments are held so node-selection can prime them.
    Glib::RefPtr<Gtk::Adjustment> m_pacing_adj;
    Glib::RefPtr<Gtk::Adjustment> m_tension_adj;
    Gtk::SpinButton   m_word_target_spin;
    Gtk::Switch       m_include_switch;
    Gtk::LevelBar     m_target_bar;
    Gtk::Label        m_target_progress_lbl;
    Gtk::Box          m_progress_footer;  // fixed bottom panel: progress label + bar
    Gtk::Revealer     m_progress_revealer;
    Gtk::Label        m_progress_arrow;
    bool              m_progress_expanded = true;

    // Metadata tab disclosure — node
    Gtk::Revealer  m_meta_node_identity_revealer;
    Gtk::Label     m_meta_node_identity_arrow;
    Gtk::Revealer  m_meta_node_synopsis_revealer;
    Gtk::Label     m_meta_node_synopsis_arrow;
    Gtk::Revealer  m_meta_node_status_revealer;
    Gtk::Label     m_meta_node_status_arrow;
    Gtk::Revealer  m_meta_node_label_revealer;
    Gtk::Label     m_meta_node_label_arrow;
    Gtk::Revealer  m_meta_node_scene_revealer;
    Gtk::Label     m_meta_node_scene_arrow;

    // Character fields
    // m_char_name_entry retired (s32) — name editing moved to the object form.
    Gtk::Entry        m_char_desc_entry;
    Gtk::DropDown*    m_char_role_dropdown  = nullptr;
    Gtk::DropDown*    m_char_color_dropdown = nullptr;
    Gtk::TextView     m_char_notes_view;
    Glib::RefPtr<Gtk::TextBuffer> m_char_notes_buffer;

    // s41 — the object form moved OUT of the Inspector into the Editor (the
    // inversion). The Inspector retreats to chrome: status / tagline / colour /
    // snapshots / notes. The template builder it owns is reached from the Editor
    // form's "Edit fields…" door via open_template_builder_for_current (public).

    // s33 — the template builder (schema editor), owned once (persistent-window
    // rule), opened from the object form's "Edit fields…" affordance for the
    // current object's template. Commits on idle (s24 modal rule).
    std::unique_ptr<TemplateBuilderDialog> m_template_builder;

    // Metadata tab disclosure — character
    Gtk::Revealer  m_meta_char_identity_revealer;
    Gtk::Label     m_meta_char_identity_arrow;
    Gtk::Revealer  m_meta_char_tagline_revealer;
    Gtk::Label     m_meta_char_tagline_arrow;
    Gtk::Revealer  m_meta_char_colour_revealer;
    Gtk::Label     m_meta_char_colour_arrow;

    // Place fields
    // m_place_name_entry retired (s32) — name editing moved to the object form.
    Gtk::Entry        m_place_desc_entry;
    Gtk::DropDown*    m_place_color_dropdown = nullptr;
    Gtk::TextView     m_place_notes_view;
    Glib::RefPtr<Gtk::TextBuffer> m_place_notes_buffer;

    // Metadata tab disclosure — place
    // (Identity disclosure retired s32 — Name moved to the object form.)
    Gtk::Revealer  m_meta_place_description_revealer;
    Gtk::Label     m_meta_place_description_arrow;
    Gtk::Revealer  m_meta_place_colour_revealer;
    Gtk::Label     m_meta_place_colour_arrow;

    // Reference meta
    Gtk::Entry        m_ref_url_entry;   // s42 — Title moved to the Editor form
    Gtk::TextView     m_ref_notes_view;
    Glib::RefPtr<Gtk::TextBuffer> m_ref_notes_buffer;
    void              build_reference_meta_section(Gtk::Box& s);

    // Metadata tab disclosure — reference
    Gtk::Revealer  m_meta_ref_reference_revealer;
    Gtk::Label     m_meta_ref_reference_arrow;
    Gtk::Revealer  m_meta_ref_notes_revealer;
    Gtk::Label     m_meta_ref_notes_arrow;

    void build_meta_tab();
    void build_node_meta_section(Gtk::Box& parent);
    void build_character_meta_section(Gtk::Box& parent);
    void build_place_meta_section(Gtk::Box& parent);
    void show_meta_section(const std::string& name);
    Gtk::DropDown* build_color_dropdown(std::function<void(int)> setter);
    static void    sync_color_dropdown(Gtk::DropDown* dd, int color_idx);
    void           rebuild_status_dropdown();
    static Gtk::Box*     make_pref_row(const std::string& label, Gtk::Widget& widget);
    static Gtk::ListBox* make_listbox();
    void refresh_pov_dropdown();
    void refresh_scene_progress();
    // Shared disclosure header builder — header row + revealer wired + pref saved
    Gtk::Box* make_disclosure_hdr(const std::string& title,
                                  Gtk::Revealer& revealer,
                                  Gtk::Label& arrow,
                                  bool& expanded_flag);

    // ── Notes tab ─────────────────────────────────────────────────────────────
    Gtk::Paned           m_notes_paned{Gtk::Orientation::VERTICAL};
    Gtk::Box             m_notes_outer;
    Gtk::Label           m_notes_ctx_label;
    Gtk::ScrolledWindow  m_notes_scroll;
    Gtk::Box             m_notes_list;
    Gtk::Box             m_notes_add_row;
    Gtk::ScrolledWindow  m_notes_entry_scroll;
    Gtk::TextView        m_notes_entry;
    Glib::RefPtr<Gtk::TextBuffer> m_notes_entry_buf;
    Gtk::Button          m_notes_add_btn;
    // Annotations section (bottom of notes pane)
    Gtk::Box             m_ann_section{Gtk::Orientation::VERTICAL, 0};

    // Notes drag-and-drop state
    int          m_note_drag_idx  = -1;   // index in notes[] being dragged
    int          m_note_drop_idx  = -1;   // insertion slot (0..n), -1 = none
    Gtk::Widget* m_note_drop_line = nullptr;

    void build_notes_tab();
    void refresh_notes();
    std::vector<Note>* active_notes();
    void setup_note_dnd(Gtk::Widget* card, int display_pos, int note_idx);
    void apply_note_drop_line(int display_pos);
    void clear_note_drop_line();

    // ── History tab ───────────────────────────────────────────────────────────
    Gtk::Box             m_history_outer;
    Gtk::ScrolledWindow  m_history_scroll;
    Gtk::Box             m_history_list;
    Gtk::Button          m_snap_btn;
    Gtk::Label           m_last_snap_lbl;   // "Last snapshot: <timestamp>"

    void build_history_tab();
    void show_snapshot_review(int snap_idx);   // preview text in a dialog
    void show_snapshot_diff(int snap_idx);     // word-level diff dialog
    void rename_snapshot(int snap_idx);        // inline rename dialog
    void delete_snapshot(int snap_idx);        // confirmation + remove

    // Diff helpers
    static std::vector<std::string> split_words(const std::string& text);
    static std::string html_to_plain(const std::string& html);
    struct DiffOp { enum class Kind { Equal, Insert, Delete } kind; std::string text; };
    static std::vector<DiffOp> compute_diff(
        const std::vector<std::string>& a,
        const std::vector<std::string>& b);

    // ── Project tab ───────────────────────────────────────────────────────────
    Gtk::ScrolledWindow  m_proj_scroll;
    Gtk::Box             m_proj_outer;
    Gtk::Box             m_proj_wrapper;   // scroll + pinned stats button
    Gtk::Entry           m_proj_title_entry;
    Gtk::Entry           m_proj_author_entry;
    Gtk::DropDown*       m_proj_genre_dropdown = nullptr;
    Gtk::Entry           m_proj_publisher_entry;
    Gtk::Entry           m_proj_isbn_entry;
    Gtk::DrawingArea     m_proj_barcode_btn;   // miniature barcode thumbnail
    std::unique_ptr<BarcodeDialog> m_barcode_dialog;
    Gtk::Entry           m_proj_year_entry;
    Gtk::TextView        m_proj_synopsis_view;
    Glib::RefPtr<Gtk::TextBuffer> m_proj_synopsis_buffer;
    Gtk::Entry           m_proj_daily_target_entry;

    // ── Project tab disclosure state ─────────────────────────────────────────
    Gtk::Revealer  m_proj_project_revealer;
    Gtk::Label     m_proj_project_arrow;
    Gtk::Revealer  m_proj_synopsis_revealer;
    Gtk::Label     m_proj_synopsis_arrow;
    Gtk::Revealer  m_proj_publication_revealer;
    Gtk::Label     m_proj_publication_arrow;
    Gtk::Revealer  m_proj_cover_revealer;
    Gtk::Label     m_proj_cover_arrow;
    Gtk::Revealer  m_proj_goals_revealer;
    Gtk::Label     m_proj_goals_arrow;

    // ── Cover image ───────────────────────────────────────────────────────────
    Gtk::DrawingArea     m_cover_thumbnail_area;   // clickable thumbnail preview
    Gtk::Entry           m_cover_path_entry;
    Glib::RefPtr<Gdk::Pixbuf> m_cover_pixbuf;      // decoded thumbnail for display
    void                 load_cover_from_path(const std::string& path);
    void                 refresh_cover_thumbnail();
    void                 show_cover_dialog();       // full-size view + replace/remove

    // ── Project goal widgets ──────────────────────────────────────────────────
    Gtk::SpinButton      m_proj_word_target_spin;
    Gtk::Entry           m_proj_due_date_entry;
    Gtk::Label           m_proj_pace_lbl;
    std::unique_ptr<ProjectGoalDialog> m_goal_dialog;

    void build_project_tab();
    void refresh_project_meta();
    void update_barcode_thumbnail(); // show/hide + redraw miniature barcode
    void refresh_goal_computed();
};

} // namespace Folio
