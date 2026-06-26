#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — Editor.hpp
// ─────────────────────────────────────────────────────────────────────────────

#include <functional>
#include <gtkmm.h>
#include <memory>
#include <string>
#include <vector>

#include "DocumentModel.hpp"
#include "MindMapCanvas.hpp"   // s48 — the fourth lens, hosted in the view-stack as "map"
#include "CustomMindMapCanvas.hpp" // s51 — the OWNED mind-map document surface (a Reference form)
#include "JournalSurface.hpp"      // s54 — the journal's owned writing surface (its own buffer + serializer)
#include "GallerySurface.hpp"      // s61 — the gallery's owned surface (lens over the image pool)
#include "Gallery.hpp"             // s61 — gallery front-door sentinel (kGalleryTemplateId) + lens reads
#include "ObjectForm.hpp"   // s41 — the inversion: the object form is the Editor document
#include "SearchEngine.hpp"
#include "EditorHtmlSerializer.hpp"
#include "Journal.hpp"   // pure journal nav/index model (JEntry/JConcept + queries)
#include "EditorRuler.hpp"
#include "RulerManagerDialog.hpp"
#include "FolioPrefs.hpp"
#include "SpellCheckHighlighter.hpp"
#include "StyleManagerDialog.hpp"
#include "TextSubstitution.hpp"
#include "UnicodePickerPopover.hpp"
#include "ScreenplayHelpDialog.hpp"

namespace Folio {

// ─────────────────────────────────────────────────────────────────────────────
// Editor
// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
// Editor's implementation is split across five TUs (s13). The split is purely
// structural — same class, same public surface. Routing convention:
//
//   src/Editor.cpp          (CORE)   — lifecycle & glue: ctor/dtor, node
//                                      load/save, editor & view-mode switching,
//                                      word count, toast/scroll utils, status CSS
//   src/Editor_build.cpp    (BUILD)  — all build_* (toolbar, font controls,
//                                      editor area, footer, find bar), the format
//                                      popover, style / extra-menu rebuilders
//   src/Editor_format.cpp   (FORMAT) — format tags + toggle/apply_*, indent /
//                                      spacing / font / geometry / style, ruler
//                                      sync, tab stops, writing-mode, outline
//                                      levels & indicators, screenplay tags,
//                                      tool-menu dialog entry points
//   src/Editor_views.cpp    (VIEWS)  — board (cards), grid/outline, joined view,
//                                      focus mode
//   src/Editor_text.cpp     (TEXT)   — find/replace bar, internal hyperlinks +
//                                      anchors + backtrace + link picker,
//                                      annotations
//   include/Editor_internal.hpp      — slot for cross-TU helpers (currently a
//                                      stub; NOT part of the public surface)
//
// Inline member functions defined in this header live with their declarations —
// they are not in any .cpp TU.
// ─────────────────────────────────────────────────────────────────────────────
class Editor : public Gtk::Box {
public:
  explicit Editor(DocumentModel &model, FolioPrefs &prefs);
  ~Editor(); // unparents m_char_picker before widget tree is torn down

  // ── Load content ──────────────────────────────────────────────────────────
  // Load any BinderNode — flushes current content first.
  void load_node(BinderNode* node);
  // Refresh the open surface's displayed title from the current node (no reload).
  // Used after an in-binder rename so the journal's own header / the editor's
  // title label track the new name without resetting caret or scroll.
  void update_open_title();

  // ── Shared-buffer seam (FocusWindow) ──────────────────────────────────────
  // The buffer is created once and reused for every node (load_node swaps
  // content in place, never the object), so a second Gtk::TextView may bind to
  // it and observe the same text/tags/undo without any sync. FocusWindow uses
  // this to render a distraction-free view on the live document while owning its
  // own view-level geometry/typography. current_node() lets the focus navigator
  // anchor next/prev against what the editor is currently showing.
  Glib::RefPtr<Gtk::TextBuffer> shared_buffer() const { return m_buffer; }
  BinderNode*                   current_node()  const { return m_current_node; }

  // Body font size (pt) and zoom. Body size is carried by the base font tag on
  // the shared buffer (GtkTextView ignores CSS font-size for body text). There
  // is no per-view body size, so FocusWindow gives itself an independent size by
  // snapshotting the editor's size+zoom on enter, driving the tag while focus is
  // open (the editor view is occluded behind the focus window), and restoring
  // the snapshot on exit via a single synchronous re-stamp.
  int    body_font_size() const { return m_current_font_size; }
  double zoom_factor()    const { return m_zoom_factor; }
  void   set_body_display(int size_pt, double zoom, int reference_pt = 0);

  // Load a character or place by index.
  
  

  // Clear editor to empty state.
  void load_empty();

  // Flush current item's buffer back to the model (called before switching).
  void save_current();

  // s46 — flush the shared buffer into the current node's content, then save a
  // named snapshot of it and notify (Inspector history refresh). Lets a second
  // surface (FocusWindow) take a snapshot through the editor's own load/save
  // path instead of reaching into the raw node. No-op for form-kind nodes.
  void snapshot_current(const std::string& name);

  // s46 — apply a link tag over the selection (or insert display_text linked) at
  // the shared cursor, and update the backlink index. View-agnostic buffer op, so
  // a second surface on the shared buffer (FocusWindow) can drive it directly with
  // its own picker — bypassing open_link_picker(), whose popover parents to the
  // editor's window and points into the editor's view (wrong surface in focus).
  void insert_link(const std::string& target_iid,
                   const std::string& anchor_id,
                   const std::string& display_text);

  // ── Board view ────────────────────────────────────────────────────────────
  void show_board(const std::vector<BoardItem> &items);
  void show_grid(const std::vector<BoardItem> &items);  // populate grid from selection

  // ── Joined View ───────────────────────────────────────────────────────────
  void load_joined(std::vector<BinderNode*> nodes);
  void save_joined();
  void reload_joined(std::vector<BinderNode*> nodes); // save then rebuild
  void exit_joined();                                  // save + clear + restore Write mode
  std::string slice_segment_to_html(int seg_idx) const;
  int  segment_index_at_iter(const Gtk::TextBuffer::iterator& it) const;
  int  joined_segment_count() const { return (int)m_joined_segments.size(); }

  // ── View / content modes ──────────────────────────────────────────────────
  enum class ViewMode    { Write, Outline, Board, Joined, Map };
  enum class EditorMode  { Node, Character, Place, Reference, Empty };
  enum class WritingMode { Novel, Outline, Screenplay };

  static constexpr int SP_COUNT = 6; // number of screenplay element types

  void set_view_mode(ViewMode mode);
  ViewMode view_mode() const { return m_view_mode; }

  void set_writing_mode(WritingMode mode);
  WritingMode writing_mode() const { return m_writing_mode; }

  // ── Focus mode ────────────────────────────────────────────────────────────
  using FocusModeCallback    = std::function<void(bool entering)>;
  using SessionChangedCallback = std::function<void()>;
  using SnapshotSavedCallback  = std::function<void()>;
  void set_focus_mode_callback(FocusModeCallback cb) {
    m_on_focus_mode = std::move(cb);
  }
  void set_session_changed_callback(SessionChangedCallback cb) {
    m_on_session_changed = std::move(cb);
  }
  void set_snapshot_saved_callback(SnapshotSavedCallback cb) {
    m_on_snapshot_saved = std::move(cb);
  }

  // ── s41 — object form (the inversion) ─────────────────────────────────────
  // Fired when the form writes a floor field that the binder leaf owns (title,
  // image, …) — lets MainWindow refresh chrome (sidebar title, etc.).
  using MetaChangedCallback = std::function<void(BinderNode*)>;
  void set_meta_changed_callback(MetaChangedCallback cb) {
    m_on_meta_changed = std::move(cb);
  }
  // Re-render the form for the currently-loaded node (after the builder saves a
  // schema change). No-op when the current node is not a form-kind.
  void refresh_object_form();

  void enter_focus_mode();
  void exit_focus_mode();
  bool is_focus_mode() const { return m_in_focus; }

  // ── s48 — map lens open hook ──────────────────────────────────────────────
  // Wired by MainWindow to its app-wide navigate (switch dropdown→Write + select
  // the node). The canvas fires it on a node click; the Editor forwards it.
  using MapOpenCallback = std::function<void(const std::string& iid)>;
  void set_map_open_callback(MapOpenCallback cb) { m_on_map_open = std::move(cb); }

  // ── s48 slice 2 — map "create a Reference here" hook ──────────────────────
  // Wired by MainWindow to create a real Reference leaf and return its iid (the
  // canvas then pins it at the drop point). Returns "" on failure.
  using MapCreateCallback = std::function<std::string(double world_x, double world_y)>;
  void set_map_create_callback(MapCreateCallback cb) { m_on_map_create = std::move(cb); }

  // ── Font / geometry prefs ─────────────────────────────────────────────────
  void apply_font_prefs(const FolioPrefs &prefs);
  void apply_editor_font();   // re-applies zoom + font CSS
  void apply_page_geometry(); // pushes width/margin/zoom to paper widgets
  void apply_editing_prefs(); // re-reads substitution + spell settings from m_prefs

  // Spell check — forwarded to SpellCheckHighlighter
  void spell_ignore_word(const std::string& word);
  void spell_add_to_dict(const std::string& word);

  // ── Word count ────────────────────────────────────────────────────────────
  void update_word_count();
  void refresh_chapter_tag();  // re-applies label colour to the chapter-tag pill
  void show_toast(const std::string& message);  // temporary bottom notification
  void queue_scroll_to_center(); // cancellable deferred scroll_to_cursor_center
  void apply_base_font_tag();    // applies m_tag_base_font over entire buffer
  void apply_zoom_to_font_tags(); // rescales all font: tags and page width by zoom
  void apply_focus_style(bool entering); // apply/restore focus font/size/spacing/color

  // Apply a named style from prefs to the current selection / paragraph(s)
  void apply_style(const TextStyle &style);

  using TemplatePickerCallback = std::function<void(Gtk::Widget*)>;
  void set_template_picker_callback(TemplatePickerCallback cb) {
    m_on_template_picker = std::move(cb);
  }

  // ── Find / Replace (inline bar) ───────────────────────────────────────────
  void open_find(bool with_replace = false);
  void close_find();

  // Tools-menu entry points — open manager dialogs from outside the editor
  void open_style_manager();
  void open_ruler_manager();
  void open_screenplay_help();
  bool find_bar_visible() const;
  void add_annotation(int range_start, int range_end,
                      const std::string& text,
                      const std::string& kind,
                      const std::string& color_hex);
  void add_annotation_to_node(BinderNode* node,
                               int node_start, int node_end,
                               int buf_start, int buf_end,
                               const std::string& text,
                               const std::string& kind,
                               const std::string& color_hex);
  void remove_annotation(int id);
  void remove_annotation_from_node(BinderNode* node, int id); // JV-aware variant
  void edit_annotation(int id, const std::string& text,
                       const std::string& kind,
                       const std::string& color_hex);
  void edit_annotation_on_node(BinderNode* node, int id,      // JV-aware variant
                               const std::string& text,
                               const std::string& kind,
                               const std::string& color_hex);
  void scroll_to_annotation(int id);
  void rebuild_annotation_tags();
  void refresh_annotation_visibility();  // show/hide all ann: tag visuals
  void refresh_link_visibility();        // show/hide all link: tag visuals

  // Fired whenever annotations change — wired to Inspector in MainWindow
  std::function<void()> on_annotations_changed;

  // Fired when the user requests a document split.
  // original: the node being split (still holds its truncated content after
  //           split_at_cursor, or original content after split_on_separator).
  // new_chunks: HTML strings for each new sibling scene to insert after it.
  std::function<void(BinderNode* original,
                     std::vector<std::string> new_chunks)> on_split_requested;

  void sync_ruler(); // push geometry to m_ruler — also called by MainWindow on layout change

private:
  // ── Model ─────────────────────────────────────────────────────────────────
  DocumentModel &m_model;
  FolioPrefs    &m_prefs;

  // ── Joined View ───────────────────────────────────────────────────────────
  struct JoinedSegment {
    BinderNode*                        node            = nullptr;
    Glib::RefPtr<Gtk::TextMark>        start;          // left-gravity
    Glib::RefPtr<Gtk::TextMark>        end;            // right-gravity
    Glib::RefPtr<Gtk::TextChildAnchor> divider_anchor; // nullptr for first segment
    bool                               dirty           = false;
  };
  std::vector<JoinedSegment> m_joined_segments;
  bool                       m_joined_active  = false;
  bool                       m_exiting_joined = false; // blocks load_node during exit
  sigc::connection           m_joined_save_conn;

  // ── Current item ──────────────────────────────────────────────────────────
  EditorMode m_editor_mode = EditorMode::Empty;
  BinderNode* m_current_node = nullptr; // never cache across mutations
  
  

  // ── Loading guard ─────────────────────────────────────────────────────────
  bool m_loading        = false;
  bool m_loading_joined = false; // blocks on_text_changed debounce during load_joined
  // Tag-extension state — populated by signal_insert_text (before insert),
  // consumed on the next idle to re-apply format tags to the inserted chars.
  bool                                       m_extend_tags_pending = false;
  int                                        m_extend_from = -1;  // buffer offset before insert
  int                                        m_extend_len  = 0;   // length of inserted text
  std::vector<Glib::RefPtr<Gtk::TextTag>>    m_extend_tags;       // tags to re-apply

  // ── View / focus state ────────────────────────────────────────────────────
  ViewMode    m_view_mode    = ViewMode::Write;
  WritingMode m_writing_mode = WritingMode::Novel;
  bool m_in_focus       = false;
  bool m_typewriter_mode = false;   // centres cursor line vertically on every move
  int  m_typewriter_page_h = -1;    // s44 — last viewport height the rail padding used
  Gtk::Scale m_typewriter_pos_slider;  // s44 — alt-click float to set the rail fraction
  bool m_tw_alt_press   = false;       // s44 — last typewriter-button press had Alt held
  bool m_tw_toggle_guard = false;      // s44 — re-entrancy guard for the revert set_active
  double typewriter_pos() const;       // s44 — clamped rail fraction (0.30–0.55)
  void   toggle_typewriter_slider();   // s44 — show/hide the floating rail slider
  Glib::RefPtr<Gtk::EventControllerKey> m_focus_key_ctrl;

  // Writing mode dropdown (Novel / Outline / Screenplay)
  Gtk::DropDown* m_writing_mode_dd = nullptr;
  bool           m_updating_wm_dd  = false;

  // Heading outline navigator (WritingMode::Outline)
  Gtk::ScrolledWindow m_houtline_scroll;
  Gtk::Box            m_houtline_box{Gtk::Orientation::VERTICAL, 0};
  void rebuild_heading_outline();

  // Scroll the write-area so the cursor line sits at the vertical midpoint.
  void scroll_to_cursor_center();
  // Set top/bottom paper padding based on typewriter_mode state.
  void apply_typewriter_padding();

  FocusModeCallback      m_on_focus_mode;
  SessionChangedCallback m_on_session_changed;
  SnapshotSavedCallback  m_on_snapshot_saved;
  TemplatePickerCallback m_on_template_picker;

  // ── Toolbar ───────────────────────────────────────────────────────────────
  Gtk::Box m_toolbar;
  Gtk::Box m_grid_toolbar;    // shown instead of m_toolbar in outline/grid mode

  // Style dropdown + manager button (toolbar)
  Gtk::DropDown  *m_style_dropdown  = nullptr;
  Gtk::Button    *m_btn_style_mgr   = nullptr;
  std::unique_ptr<StyleManagerDialog> m_style_mgr_dialog;
  bool            m_inhibit_style_dd = false;
  void            rebuild_style_dropdown();

  // Format popover — opened by toolbar button or right-click
  Gtk::Popover     *m_format_popover = nullptr;
  Gtk::MenuButton  *m_btn_format = nullptr;
  void              build_format_popover();
  void              show_format_popover_at(double x, double y);

  // Layout popover — page geometry, indent, spacing, line numbers, zoom

  // Rich-text format buttons
  Gtk::Button m_btn_bold;
  Gtk::Button m_btn_italic;
  Gtk::Button m_btn_underline;
  Gtk::Button m_btn_strikethrough;
  Gtk::Button m_btn_clear_format;
  Gtk::Button m_btn_snapshot;
  Gtk::Button m_btn_template;  // Apply Template toolbar button
  Gtk::ToggleButton m_btn_typewriter;

  // Justification toggle group
  Gtk::Box m_justify_box;
  Gtk::ToggleButton m_btn_justify_left;
  Gtk::ToggleButton m_btn_justify_center;
  Gtk::ToggleButton m_btn_justify_right;
  Gtk::ToggleButton m_btn_justify_full;

  // Named justification tags
  Glib::RefPtr<Gtk::TextTag> m_tag_justify_left;
  Glib::RefPtr<Gtk::TextTag> m_tag_justify_center;
  Glib::RefPtr<Gtk::TextTag> m_tag_justify_right;
  Glib::RefPtr<Gtk::TextTag> m_tag_justify_full;

  // Color buttons
  Gtk::ColorDialogButton *m_btn_text_color = nullptr;
  Gtk::ColorDialogButton *m_btn_bg_color = nullptr;
  Gdk::RGBA m_current_text_color;
  Gdk::RGBA m_current_bg_color;

  // Font controls
  Gtk::Box m_font_box;
  Gtk::DropDown *m_font_dropdown = nullptr;
  Gtk::SpinButton *m_font_size_spin = nullptr;
  Gtk::SpinButton *m_line_spacing_spin = nullptr;
  std::vector<std::string> m_font_names;
  guint m_font_multiple_idx = 0;

  // Page geometry controls
  Gtk::Scale      *m_zoom_scale       = nullptr;
  Gtk::Entry      *m_zoom_pct_entry   = nullptr;
  int    m_page_width_pct       = 65;   // 15–100 % of scroll-window width
  int    m_focus_page_width_pct = 80;   // separate % used only in focus mode
  double m_focus_zoom_factor    = 1.0;  // separate zoom used only in focus mode
  bool   m_focus_typewriter     = false;// separate typewriter toggle for focus
  int    m_focus_page_margin_px = 64;   // separate margin used only in focus mode
  std::string m_focus_font;             // font family in focus ("" = use editor)
  int         m_focus_font_size    = 0; // font size in focus (0 = use editor)
  double      m_focus_line_spacing = 0.0; // line spacing in focus (0 = use editor)
  std::string m_focus_text_color;       // text color override ("" = none)

  // Snapshots of regular-editor state — saved on focus entry, restored on exit
  std::string m_saved_font;
  int         m_saved_font_size       = 0;
  double      m_saved_line_spacing    = 0.0;
  double      m_saved_zoom_factor     = 1.0;
  bool        m_saved_typewriter      = false;
  int         m_saved_page_margin_px  = 64;
  int    m_page_margin_px  = 64;       // legacy symmetric margin
  int    m_left_margin_px  = 64;       // left margin (px)
  int    m_right_margin_px = 64;       // right margin (px)
  double m_zoom_factor     = 1.0;
  // Proportional scale applied to per-run font: tags on top of zoom. 1.0 in
  // regular editing; set by set_body_display (FocusWindow's sizing primitive)
  // to focus_size / authored_body_size so styled runs grow WITH the focus
  // sizer instead of staying at their literal authored point size.
  double m_font_tag_scale  = 1.0;

  // Internal helper for the percentage width widget
  void set_page_width_pct(int pct);   // clamp + apply + update entry

  // ── Find / Replace bar ────────────────────────────────────────────────────
  Gtk::Revealer   m_find_revealer;
  Gtk::Box        m_find_bar{Gtk::Orientation::VERTICAL, 0};
  Gtk::Box        m_find_row{Gtk::Orientation::HORIZONTAL, 4};
  Gtk::Entry*     m_find_entry            = nullptr;
  Gtk::Button*    m_find_prev_btn         = nullptr;
  Gtk::Button*    m_find_next_btn         = nullptr;
  Gtk::Label*     m_find_count_lbl        = nullptr;
  Gtk::CheckButton* m_find_case_btn       = nullptr;
  Gtk::CheckButton* m_find_word_btn       = nullptr;
  Gtk::CheckButton* m_find_regex_btn      = nullptr;
  Gtk::Button*    m_find_close_btn        = nullptr;
  Gtk::ToggleButton* m_find_replace_toggle = nullptr;
  Gtk::Revealer   m_replace_revealer;
  Gtk::Box        m_replace_row{Gtk::Orientation::HORIZONTAL, 4};
  Gtk::Entry*     m_replace_entry         = nullptr;
  Gtk::Button*    m_replace_one_btn       = nullptr;
  Gtk::Button*    m_replace_all_btn       = nullptr;
  struct FindMatch { int start = 0; int end = 0; };
  std::vector<FindMatch> m_find_matches;
  int                    m_find_current = -1;
  Glib::RefPtr<Gtk::TextTag> m_tag_find_match;
  Glib::RefPtr<Gtk::TextTag> m_tag_find_current;
  void build_find_bar();
  void find_update();
  void find_navigate(int delta);
  void find_replace_current();
  void find_replace_all();
  void find_clear_tags();
  SearchOptions current_find_opts() const;

  // Named inline format tags
  Glib::RefPtr<Gtk::TextTag> m_tag_bold;
  Glib::RefPtr<Gtk::TextTag> m_tag_italic;
  Glib::RefPtr<Gtk::TextTag> m_tag_underline;
  Glib::RefPtr<Gtk::TextTag> m_tag_strikethrough;

  // ── Outline indent level tags ────────────────────────────────────────────
  std::array<Glib::RefPtr<Gtk::TextTag>, MAX_OUTLINE_LEVELS> m_tag_ol;

  int  current_outline_level() const;
  void apply_outline_level(int level);
  void update_writing_mode_dd();
  void replace_line_indicator(int new_level);
  std::string compute_indicator(int line, int level) const;
  void detect_writing_mode_from_buffer();

  // ── Screenplay element tags ───────────────────────────────────────────────
  // Order matches SP_ELEMENTS[]: scene, action, character, parenthetical,
  // dialogue, transition.
  std::array<Glib::RefPtr<Gtk::TextTag>, SP_COUNT> m_tag_sp;

  // Returns index into SP_ELEMENTS for the current line's sp- tag, or -1.
  int  current_sp_element() const;
  // Apply (or remove) the screenplay element tag for the current line.
  void apply_sp_element(int idx);
  // Remove all sp-* tags from a line range.
  void clear_sp_tags(Gtk::TextBuffer::iterator s, Gtk::TextBuffer::iterator e);
  // Advance to the next element in the tab-cycle and apply it.
  void sp_tab_next(bool reverse);
  // Auto-sense the element type for the current line and apply if confident.
  void sp_auto_sense();
  bool m_sp_sensing = false; // re-entry guard for sp_auto_sense
  std::string m_pre_sp_font;      // editor font saved when entering Screenplay mode
  int         m_pre_sp_font_size = 0;
  std::string m_screenplay_font  = "Courier New"; // best available Courier, detected once on realize


  // ── Write view ────────────────────────────────────────────────────────────
  Gtk::Overlay m_scroll_overlay;
  Gtk::Revealer m_toast_revealer;
  Gtk::Label    m_toast_label;
  sigc::connection m_toast_timer;
  sigc::connection m_pending_scroll; // cancellable idle for scroll_to_cursor_center
  Gtk::ScrolledWindow m_write_scroll;
  Gtk::Box m_paper_card;
  Gtk::Box m_paper_inner;
  Gtk::Label m_chapter_tag;
  Gtk::Label m_title_label;
  Glib::RefPtr<Gtk::CssProvider> m_chapter_tag_css;  // reused for label-colour tinting
  Gtk::Separator m_divider;
  Gtk::Revealer  m_header_revealer;
  Gtk::Button    m_header_toggle;
  void           update_header_toggle_icon();

  // Line-number gutter
  Gtk::Box      m_text_area_row;          // horizontal: [gutter | text_view]
  Gtk::DrawingArea m_line_number_gutter;
  bool          m_show_line_numbers = false;
  Gtk::ToggleButton m_btn_line_numbers;
  Gtk::ToggleButton m_btn_spell_check;  // toolbar spell check on/off toggle
  Gtk::ToggleButton m_btn_ruler;        // toolbar ruler on/off toggle
  Gtk::ToggleButton m_btn_show_annotations; // toolbar annotation highlight toggle
  Gtk::ToggleButton m_btn_show_links;       // toolbar hyperlink colour toggle
  Gtk::ToggleButton m_btn_show_invisibles;  // toolbar invisible chars toggle
  Gtk::Button       m_btn_sp_help;          // screenplay format reference (Screenplay mode only)
  Gtk::DrawingArea  m_invis_overlay;        // overlay that paints ¶ · → glyphs
  void          update_gutter_width();    // recalculate + queue_draw

  Gtk::TextView m_text_view;
  Glib::RefPtr<Gtk::TextBuffer> m_buffer;

  // Avatar strip (characters + places)
  Gtk::Box m_avatar_strip;
  Gtk::Image m_avatar_image;
  Gtk::Button m_avatar_btn;

  // ── s41 — Form view (the inversion) ───────────────────────────────────────
  // A Character/Place opens its template-driven form as the Editor document.
  // Persistent widget (own once; ObjectForm::populate rebuilds its rows). Hosted
  // in a paper card matching the write view, added to m_view_stack as "form".
  Gtk::ScrolledWindow m_form_scroll;
  Gtk::Box            m_form_card;
  Folio::ObjectForm   m_object_form;
  MetaChangedCallback  m_on_meta_changed;
  bool node_is_form_kind(const BinderNode* n) const {
    return n && (n->kind == BinderKind::Character ||
                 n->kind == BinderKind::Place ||
                 n->kind == BinderKind::Reference);   // s42
  }
  // s51 — a Reference whose form is "Mind Map": routed to the owned-MM canvas as
  // its editor surface (not the ObjectForm). The marker is the reserved sentinel
  // template_id; the body cell holds the serialised CMMDoc.
  bool node_is_mindmap_form(const BinderNode* n) const {
    return n && n->kind == BinderKind::Reference &&
           n->template_id == Folio::kMindMapTemplateId;
  }
  // A Reference whose form is "Journal": routed to the PLAIN PROSE editor (not
  // the ObjectForm, not a canvas). The sentinel template_id marks it; the body
  // cell holds the journal's prose (DT-delimited entries).
  bool node_is_journal_form(const BinderNode* n) const {
    return n && n->kind == BinderKind::Reference &&
           n->template_id == Folio::kJournalTemplateId;
  }
  // A Reference whose form is "Gallery": routed to the owned GallerySurface (a
  // lens over the shared image pool). The sentinel template_id marks it; the
  // body cell holds the lens-def (wall order), NOT the images.
  bool node_is_gallery_form(const BinderNode* n) const {
    return n && n->kind == BinderKind::Reference &&
           n->template_id == Folio::kGalleryTemplateId;
  }
  void populate_object_form();   // render m_object_form for m_current_node

  // Exit-focus overlay button
  Gtk::Button *m_exit_focus_btn  = nullptr;
  Gtk::Box    *m_focus_width_bar = nullptr;  // overlay widget in focus mode
  Gtk::Label  *m_focus_zoom_lbl  = nullptr;  // zoom % label inside focus bar
  Gtk::Label  *m_focus_font_lbl  = nullptr;  // font name label inside focus bar
  Gtk::Label  *m_focus_size_lbl  = nullptr;  // font size label inside focus bar
  Gtk::Label  *m_focus_ls_lbl    = nullptr;  // line spacing label inside focus bar
  Glib::RefPtr<Gtk::EventControllerMotion> m_focus_motion_ctrl;
  sigc::connection m_focus_hide_conn;

  // ── Outline / Grid view ───────────────────────────────────────────────────
  Gtk::ScrolledWindow m_outline_scroll;
  Gtk::Overlay        m_outline_overlay;   // grid + marquee overlay
  Gtk::Grid           m_outline_grid;
  Gtk::DrawingArea    m_marquee_layer;
  std::vector<BoardItem> m_grid_items;     // current binder selection for grid
  bool m_grid_show_manuscript  = false;
  bool m_grid_show_characters  = false;
  bool m_grid_show_places      = false;
  std::vector<BinderNode*> m_grid_rows;
  std::vector<bool>        m_grid_selected;
  int  m_grid_row_count   = 0;
  int  m_grid_sort_col  = -1;             // -1 = unsorted
  bool m_grid_sort_asc  = true;
  // Marquee drag state (in grid widget coords)
  bool   m_marquee_active = false;
  double m_marquee_x0 = 0, m_marquee_y0 = 0;
  double m_marquee_x1 = 0, m_marquee_y1 = 0;
  // Per-row y positions for hit testing (populated in rebuild_outline)
  std::vector<int> m_grid_row_y;          // top y of each data row in grid coords
  int              m_grid_row_h = 32;     // nominal row height
  void rebuild_outline();
  void grid_batch_set_status(NodeStatus s);
  void grid_batch_set_pov(const std::string& pov);
  void grid_batch_set_label(int color_idx);
  void grid_batch_set_include(bool v);

  // ── Board view ────────────────────────────────────────────────────────────
  Gtk::Overlay m_board_overlay;
  Gtk::ScrolledWindow m_board_scroll;
  Gtk::FlowBox *m_board_flow = nullptr;
  Gtk::Label m_board_placeholder;

  // Multi-selection placeholder (shown in Write stack while multi selected)
  Gtk::Box m_multi_placeholder_box;
  Gtk::Label m_multi_placeholder_label;

  // Empty-state hint overlays
  Gtk::Label m_write_placeholder;   // overlay on m_scroll_overlay — Write/Joined
  Gtk::Label m_grid_placeholder;    // overlay on m_outline_overlay — Grid

  // ── Footer ────────────────────────────────────────────────────────────────
  Gtk::Box m_footer;
  Gtk::Label *m_wc_label = nullptr;
  Gtk::Label *m_chars_label = nullptr;
  Gtk::Label *m_read_label = nullptr;


  // ── Stack ─────────────────────────────────────────────────────────────────
  Gtk::Stack m_view_stack;

  // ── s48 — Map view (the fourth lens) ──────────────────────────────────────
  // A thin painter over the pure MindMap layer, added to m_view_stack as "map".
  // Spans the whole graph (all nodes), not the current node — so it is a VIEW
  // mode (like Board), routed in set_view_mode, rebuilt on entry from the model.
  Folio::MindMapCanvas m_map_canvas;

  // s51 — the OWNED mind-map document surface (added to m_view_stack as "cmm").
  // Shown in Write/Joined when the current node is a Mind Map Reference form,
  // in place of the ObjectForm. Reads/writes a CMMDoc serialised in the node's
  // body cell. m_cmm_iid is the host node the canvas's persist callback writes to.
  Folio::CustomMindMapCanvas m_cmm_canvas;
  // s54 — the journal as an OWNED instrument (mirrors the MM canvas): it owns its
  // TextView + buffer + serializer and persists straight into the host node's
  // body via a callback keyed by m_journal_iid. A journal Reference always shows
  // this surface in Write mode — it never borrows the Scene editor's prose view.
  Folio::JournalSurface m_journal_surface;
  std::string m_journal_iid;
  // s61 — the Gallery as an owned instrument (a LENS over the shared image
  // pool). Owns its lens-def (wall order) in the host node body; reads the pool
  // handed in via set_context. Shown in Write mode for a gallery Reference.
  Folio::GallerySurface m_gallery_surface;
  std::string m_gallery_iid;
  std::string m_cmm_iid;
  // Fired when a node glyph is activated on the map. MainWindow wires it to the
  // app-wide navigate path (switch to Write + select), so map-open == sidebar-open.
  // (MapOpenCallback alias is declared in the public section, above its setter.)
  MapOpenCallback m_on_map_open;
  MapCreateCallback m_on_map_create;   // s48 slice 2 — double-click → new Reference

  // ── Font / CSS state ──────────────────────────────────────────────────────
  std::string m_current_font = "JansonText";
  int m_current_font_size = 12;
  double m_current_line_spacing = 1.0;
  Glib::RefPtr<Gtk::CssProvider> m_font_css_provider;
  Glib::RefPtr<Gtk::CssProvider> m_focus_color_css;  // text color override in focus
  bool m_updating_font_controls = false;
  bool m_font_update_pending    = false;  // defers mark_set font-control refresh to idle
  bool m_mouse_btn_held         = false;  // true while primary button is pressed in text view
  bool m_geometry_ready         = false;  // set after first real layout; gates notify::width
  bool m_first_click_done       = false;  // set after first primary click; gates notify::width during focus acquisition
  bool m_first_node_click       = true;   // reset on every load_node; guards focus-scroll snapshot

  // ── First-line indent ─────────────────────────────────────────────────────
  bool m_first_line_indent    = true;
  int  m_first_line_indent_px = 32;
  int  m_left_indent_px       = 0;   // left body indent (px)
  int  m_right_indent_px      = 0;   // right indent (px)
  Glib::RefPtr<Gtk::TextTag> m_tag_indent;       // first-line indent
  Glib::RefPtr<Gtk::TextTag> m_tag_joined_divider; // JV divider lines (display-only, non-editable)
  Glib::RefPtr<Gtk::TextTag> m_tag_base_font;    // document-wide font // document-default font/size tag, lowest priority
  void apply_indent();

  // ── Paragraph spacing ─────────────────────────────────────────────────────
  int  m_paragraph_spacing_px = 0;
  void apply_paragraph_spacing();  // pushes value to text_view

  // Saved selection offsets (preserved when focus leaves the text view)
  int m_saved_sel_start = -1;
  int m_saved_sel_end = -1;

  // ── Build helpers ─────────────────────────────────────────────────────────
  void build_toolbar();
  void build_font_controls();
  void build_editor_area();
  void build_footer();

  // ── Internal helpers ──────────────────────────────────────────────────────
  void set_editor_mode(EditorMode mode);
  void on_text_changed();
  void rebuild_extra_menu(double x, double y);  // rebuild TextView extra menu on right-click
  void split_at_cursor();                        // split scene at cursor position
  void split_on_separator(const std::string& sep); // split scene on separator lines
  void apply_tab_stops();                           // push tab stops to text view
  void apply_paragraph_left_indent(int px);         // set left margin on cursor para
  void apply_paragraph_right_indent(int px);        // set right margin on cursor para

  // ── Internal hyperlinks ───────────────────────────────────────────────────
  void open_link_picker();                           // show node-picker popover to insert a link
  // insert_link is PUBLIC (see above) — a clean buffer-level op a second surface
  // (FocusWindow) can drive with its own picker, without the editor-window popover.
  void remove_link_at_cursor();                      // strip link tag under cursor
  void set_anchor_at_cursor();                       // stamp data-anchor on current paragraph
  void remove_anchor_at_cursor();                    // remove anchor: tag from current paragraph
  void apply_link_tag_style(Glib::RefPtr<Gtk::TextTag> tag); // set visual properties on a link tag
  void apply_anchor_tag_style(Glib::RefPtr<Gtk::TextTag> tag); // set visual properties on an anchor tag

  // ── Journal (an owned instrument; see JournalSurface) ──
  // dt:/concept: runs may appear in a loaded body; html_to_buffer re-applies
  // their look (header for a stamp, a soft underline for a concept). All journal
  // editing — stamping, extraction, flair — now lives in JournalSurface over its
  // OWN buffer; the Editor only routes the node to that surface and persists it.
  void apply_dt_tag_style(Glib::RefPtr<Gtk::TextTag> tag);
  void apply_concept_tag_style(Glib::RefPtr<Gtk::TextTag> tag);
  bool detect_dark_mode() const; // true if the current theme is dark
  Gdk::RGBA blend_annotation_bg(const std::string& hex) const; // pre-blend against real page bg
  // Follow the link tag under iter; returns false if no link there.
  bool follow_link_at(Gtk::TextBuffer::iterator iter);
  // Return the link tag (if any) at iter — tag name "link:<node>:<anchor>"
  Glib::RefPtr<Gtk::TextTag> link_tag_at(Gtk::TextBuffer::iterator iter) const;
  // Generate a short random hex anchor ID (6 chars)
  static std::string generate_anchor_id();
  // Blink-highlight a range after navigation arrival
  void start_arrival_highlight(Gtk::TextBuffer::iterator start,
                                Gtk::TextBuffer::iterator end);

  // Link picker popover widgets (built once, reused)
  Gtk::Popover*    m_link_picker_popover  = nullptr;
  Gtk::SearchEntry* m_link_picker_search  = nullptr;
  Gtk::ListBox*    m_link_picker_list     = nullptr;

  // Cached node list for the link picker — rebuilt on each open()
  struct LinkPickerEntry { std::string iid; std::string title; std::string section; };
  std::vector<LinkPickerEntry> m_link_picker_entries;
  sigc::connection m_link_search_conn;
  sigc::connection m_link_row_conn;

  // Arrival highlight tag + timeout connection
  Glib::RefPtr<Gtk::TextTag> m_tag_link_highlight;
  sigc::connection            m_link_highlight_conn;

  // Navigation callback — set by MainWindow to handle cross-node link follows
  // Called with (target_iid, anchor_id)
  std::function<void(const std::string&, const std::string&)> m_on_follow_link;

public:
  void set_on_follow_link(std::function<void(const std::string&, const std::string&)> cb) {
      m_on_follow_link = std::move(cb);
  }
  // Scroll to paragraph with the given anchor id and blink-highlight it.
  // Called by MainWindow after navigate_to_link loads the node.
  void scroll_to_anchor(const std::string& anchor_id);

private:

  // Rich-text serialization (buffer <-> HTML stored in content fields)
  // Delegated to EditorHtmlSerializer — see EditorHtmlSerializer.hpp
  std::string buffer_to_html() const;
  void        html_to_buffer(const std::string& html);
  std::unique_ptr<EditorHtmlSerializer> m_serializer; // initialized after tags are created

  // Text substitution engine — smart quotes, em dash, ellipsis, autocorrect
  std::unique_ptr<TextSubstitution> m_substitution;

  // Spell check highlighter — enchant-2 backed squiggle rendering
  std::unique_ptr<SpellCheckHighlighter> m_highlighter;

  // Unicode character picker — owned here, parented to root window on first use
  std::unique_ptr<UnicodePickerPopover> m_char_picker;
  std::unique_ptr<ScreenplayHelpDialog> m_sp_help_dialog;

  // Ruler
  EditorRuler m_ruler;
  std::unique_ptr<RulerManagerDialog> m_ruler_manager;

private:
  void show_annotation_popover(double x, double y);
  void apply_annotation_tag(const Annotation& ann);
  Gtk::Popover*  m_ann_popover  = nullptr;
  double         m_last_rc_x        = 0;
  double         m_last_rc_y        = 0;
  int            m_rc_sel_start     = -1; // selection at right-click time
  int            m_rc_sel_end       = -1;
  double         m_last_lc_x        = 0; // left-click press coords (for link follow)
  double         m_last_lc_y        = 0;
  bool           m_last_lc_alt      = false; // was Alt held at press time

  // Back-trace popover — persistent, rebuilt on each open
  Gtk::Popover*  m_backtrace_popover      = nullptr;
  Gtk::ListBox*  m_backtrace_list         = nullptr;
  Gtk::Label*    m_backtrace_header_label = nullptr;
  void do_backtrace(); // navigate directly (1 link) or show picker (multiple)
  Gtk::DrawingArea m_backtrace_gutter; // left-gutter diamond indicator

  void toggle_format_tag(const Glib::RefPtr<Gtk::TextTag> &tag,
                         Gtk::TextBuffer::iterator start,
                         Gtk::TextBuffer::iterator end);

  void apply_font_to_selection();
  void apply_line_spacing_to_selection();
  void update_font_controls_from_selection();
  static void expand_to_paragraphs(Gtk::TextBuffer::iterator &start,
                                   Gtk::TextBuffer::iterator &end);

  // Board card factories
  Gtk::Widget *make_board_card_node(const std::vector<int> &path);
  Gtk::Widget *make_board_card(Section section, const std::vector<int> &path);

  // Status dot CSS class — returns string literal, never dangling
  static const char *status_css(NodeStatus s);
};

} // namespace Folio
