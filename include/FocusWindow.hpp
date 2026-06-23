#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// FocusWindow.hpp — distraction-free writing as a separate window on a SHARED
// buffer (focus-mode redesign).
//
// WHY THIS EXISTS (the bug this dissolves)
//   The previous focus mode was the *same* editor widget toggled into a parallel
//   state: it snapshotted the regular editor's geometry/typography, swapped in a
//   second set of "focus" values, and tried to hand the originals back on exit.
//   `apply_page_geometry()` branched on `m_in_focus` to read one of two margin
//   sets, and the editor's geometry was only restored by a single
//   `apply_page_geometry()` fired on a 150 ms timer after `unfullscreen()`. At
//   HiDPI that timer often beat the resize, so the call measured a stale width
//   or hit its `scroll_w < 1` early return — and because the widgets were never
//   unmapped, nothing re-fired it. The editor was left with margins that never
//   got restored until the user nudged the ruler (the *other* caller of
//   `apply_page_geometry()`). That is the "margins lost on return" report.
//
//   The root cause is not the timer — it is that two modes took turns owning ONE
//   mutable geometry/typography state and traded it across a boundary. This
//   class removes the boundary instead of patching the hand-off.
//
// THE ARCHITECTURE — one buffer, two views
//   GTK lets several Gtk::TextView widgets observe a single Gtk::TextBuffer. The
//   Editor's `m_buffer` is created ONCE (Editor.cpp) and reused for every node
//   via set_text; `load_node` swaps content in place, it never replaces the
//   buffer object. So a second view bound to that same buffer:
//     • shows the same text, tags (li:/ri:/indent/links), undo, and spell marks
//       for free — they all live on the buffer, nothing is copied or synced;
//     • owns its OWN geometry and typography (page width, margins, font, size,
//       line-spacing, zoom, color, typewriter padding) as properties of its own
//       widget + its own CssProvider — applied to nothing but itself.
//
//   The editor's margins are therefore a *different widget's* properties, sitting
//   untouched in the main window the entire time focus is open. There is no
//   snapshot, no restore, no `m_in_focus` branch in the geometry path, no timed
//   deferral. Closing the focus window restores nothing because nothing was
//   altered. The margins-lost bug becomes unrepresentable rather than fixed.
//
//   (Consequence: the Editor's `enter_focus_mode`/`exit_focus_mode`, the
//   `m_in_focus` branches in `apply_page_geometry`/`apply_typewriter_padding`,
//   the `m_saved_*` snapshot fields, and the in-place fullscreen hand-off in
//   MainWindow's focus callback all come OUT during implementation. The focus_*
//   PREFS stay — this window reads them — only the in-editor swap machinery goes.)
//
// CONTENT OWNERSHIP — focus owns no content; the Editor owns the one load/save path
//   FocusWindow holds NO BinderNode content and never serializes. To move to a
//   scene it calls the Editor's existing `load_node(node)` — the single
//   section-agnostic load/save path the sidebar already uses, which flushes the
//   current node before loading the next. Because the buffer is shared, after
//   `load_node` the focus view shows the new node with no extra work, and the
//   main editor behind it re-points to the same node. There is one current node
//   and one cursor (the insert mark lives on the shared buffer), so the two
//   surfaces cannot disagree about what is being edited. This is the deliberate
//   "they move together" decision: picking a scene in focus is the same event as
//   picking it in the binder.
//
// NAVIGATION — reading-order list + load_node, no globals
//   "Jump to an arbitrary scene" needs to ENUMERATE scenes, which the hidden
//   sidebar used to do. The model already owns that: `collect_compile_nodes`
//   walks the manuscript tree in reading/compile order (the order the exporter
//   uses). FocusWindow asks the model for that list through its injected
//   reference — it does NOT reach into the six raw vectors itself, and it does
//   NOT use a global/singleton accessor (Folio injects the model by reference
//   everywhere; a second access channel is exactly the kind of disagreement the
//   iid arc retired). Two affordances, both backed by that one list:
//     • quick-switcher: a hotkey pops a type-to-filter overlay of scene titles
//       in reading order; Enter calls Editor::load_node on the chosen node.
//     • next / prev: find the current node's index in the list, load index ± 1.
//   Rarer "open a character/place to glance at it" can reuse the model's
//   `collect_all_nodes` cross-section list later; the manuscript walk is the
//   writing path and ships first.
//
// REFERENCE INJECTION (consistent with the whole codebase)
//   Ctor takes `DocumentModel&`, `FolioPrefs&`, `Editor&` — the same shape every
//   Folio component uses. MainWindow owns all three and constructs the window;
//   no new ownership, no global.
//
// SMALL ADDITIVE SEAMS THIS REQUIRES (added during implementation, noted here so
// the surface is explicit and reviewable before any code moves):
//   • Editor:        Glib::RefPtr<Gtk::TextBuffer> shared_buffer() const;  // → m_buffer
//                    BinderNode* current_node() const;                     // → m_current_node
//   • DocumentModel: std::vector<BinderNode*> manuscript_in_reading_order();
//                    // public, non-const wrapper over the existing private
//                    // collect_compile_nodes walk (returns the nodes load_node needs)
//   These are pure accessors — they expose what already exists, they do not move
//   ownership or duplicate logic.
// ─────────────────────────────────────────────────────────────────────────────

#include <functional>
#include <gtkmm.h>
#include <string>
#include <utility>
#include <vector>

#include "DocumentModel.hpp"
#include "FolioPrefs.hpp"

namespace Folio {

class Editor;  // shared buffer + load_node + current_node (see additive seams above)

class FocusWindow : public Gtk::Window {
public:
    // model/prefs/editor are owned by MainWindow and outlive this window.
    FocusWindow(DocumentModel& model, FolioPrefs& prefs, Editor& editor);
    ~FocusWindow() override;

    // Open fullscreen on `start` (typically the editor's current node). If
    // `start` is null, opens on the editor's current node. Binds the focus view
    // to the shared buffer and applies the focus_* look to THIS view only.
    void present_focus(BinderNode* start = nullptr);

    // Optional: fired when the focus window closes, so MainWindow can re-assert
    // its own layout. Restores nothing about the editor — the editor was never
    // touched — it only lets the caller resync chrome it hid (if any).
    using ClosedCallback = std::function<void()>;
    void set_closed_callback(ClosedCallback cb) { m_on_closed = std::move(cb); }

private:
    // ── Construction ──────────────────────────────────────────────────────────
    void build_view();          // m_view + set_buffer(editor shared buffer)
    void build_view_chrome();   // s46 — line-number + invisibles overlays over m_view
    void apply_view_chrome();   // s46 — set overlay visibility from prefs + redraw
    void queue_chrome_draw();   // s46 — redraw the visible view-chrome overlays
    void build_toast();         // s46 — transient bottom-centre confirmation pill
    void flash_toast(const std::string& msg);  // s46 — show + auto-hide the toast
    void build_drawer();        // s45 — left settings drawer (replaces the hover pill)
    void build_switcher();      // type-to-filter scene overlay
    void build_link_picker();   // s46 — focus-owned node picker for link insert (mirrors switcher)
    void open_link_picker();    // s46 — rebuild entries + show the link picker overlay
    void repopulate_link_picker();              // s46 — refill m_link_list from entries + filter
    void activate_link_row(Gtk::ListBoxRow* row); // s46 — resolve row → node → Editor::insert_link
    void repopulate_switcher(); // refill m_switch_list from m_scenes + filter text
    void activate_switch_row(Gtk::ListBoxRow* row);  // resolve row → scene → goto
    void wire_keys();           // Esc → close; switcher hotkey; next/prev hotkeys

    // ── Settings drawer (s45) — left-edge slide-in, organized + labeled ────────
    void open_drawer();
    void close_drawer();
    void toggle_drawer();

    // ── Slider smoothness (s45) ───────────────────────────────────────────────
    // The cheap part of a slider change (the numeric readout + the pref value) runs
    // immediately; the expensive part is taken off the per-tick drag:
    //   • prefs.save() is debounced (one disk write after the drag settles), never
    //     per tick — that disk I/O was the main hitch.
    //   • the heavy line-spacing relayout is coalesced to one apply per idle.
    //   • the full-buffer font re-tag (size) is debounced — it applies when the drag
    //     settles, not every frame, since there is no cheap size preview.
    // Sliders also round to clean steps (set_round_digits) so a drag lands precisely.
    void schedule_save();        // debounced prefs.save()
    void queue_apply_look();     // idle-coalesced apply_focus_look (spacing)
    void schedule_size_apply();  // debounced set_body_display (size re-tag)

    // ── Backdrop (s45) — external image behind the text, two writer knobs ──────
    // Layer stack, back → front, all on m_overlay:
    //   background image → dim scrim → text-column backing (panel) → text → chrome.
    // The photo is an EXTERNAL path (projects link, never embed). Dim = darkness of
    // the scrim over the whole photo; Panel = alpha of the reading-column card.
    void open_backdrop_picker();        // FileChooserNative → set path + reload
    void set_backdrop(const std::string& path);  // load/clear + persist + reapply
    void apply_backdrop();              // push path/dim/panel onto the three layers
    void update_backdrop_controls();    // button label + slider-group visibility
    void apply_panel_color();           // s45 — pref hex → m_panel_color + redraw card
    void apply_text_color();            // s45 — pref hex → CssProvider on m_view

    // ── Look (applies to m_view ONLY; never writes to the editor) ─────────────
    void apply_focus_look();        // view-level: line spacing, then geometry + padding
    void apply_typewriter_padding();// rail-fraction top/bottom runway (s45 — pos-driven)
    void apply_focus_geometry();    // page width % of the focus viewport (self-contained)

    // ── Typewriter rail (s45) ─────────────────────────────────────────────────
    // Focus owns its OWN view, so the editor's scroll_to_cursor_center cannot move
    // it. These mirror that machinery for m_view, but read the SHARED rail fraction
    // (m_prefs.typewriter_position) so the platen position is one value across both
    // surfaces. View-level only — the buffer is never touched.
    double focus_typewriter_pos() const;  // shared rail fraction, clamped 0.15–0.85
    void   scroll_to_rail();              // pin the caret to the rail in m_view
    void   queue_scroll_to_rail();        // idle-deduped scroll_to_rail
    void   toggle_typewriter();           // flip focus_typewriter_mode + re-rail

    // ── Navigation (delegates content to Editor::load_node) ───────────────────
    void rebuild_scene_list();          // ← model.manuscript_in_reading_order()
    void goto_node(BinderNode* node);   // Editor::load_node(node); update title/index
    void goto_relative(int delta);      // next/prev within m_scenes
    void open_switcher();               // populate + show the filter overlay
    void update_navbar();               // s45 — refresh the visible scene breadcrumb

    // ── Refs (injected; not owned) ────────────────────────────────────────────
    DocumentModel& m_model;
    FolioPrefs&    m_prefs;
    Editor&        m_editor;

    // ── Own widgets / own state (the whole point — none of this is shared) ────
    Gtk::Overlay         m_overlay;
    Gtk::ScrolledWindow  m_scroll;
    Gtk::TextView        m_view;            // bound to the editor's shared buffer

    // s45 — backdrop layers (back → front; m_bg_pic is the overlay's child, the
    // rest are overlays added BEFORE the text so they sit behind it).
    Gtk::Picture*        m_bg_pic   = nullptr; // the photo (COVER), overlay child
    Gtk::Box*            m_bg_dim   = nullptr; // full-bleed black scrim (opacity = dim)
    Gtk::DrawingArea*    m_bg_panel = nullptr; // text-column card, Cairo-drawn (feathered)
    Gdk::RGBA            m_panel_color {"#1e1e2e"};  // card fill (panel-color picker later)
    Glib::RefPtr<Gtk::CssProvider> m_text_css;       // s45 — focus text colour override

    // s45 — backdrop controls in the hover bar (sliders gated on a live backdrop)
    Gtk::Button*         m_bg_btn        = nullptr; // Set/Change backdrop… (picker)
    Gtk::Button*         m_bg_clear      = nullptr; // remove the backdrop
    Gtk::Box*            m_bg_slider_grp = nullptr; // Dim + Panel sliders (show when set)
    Glib::RefPtr<Gtk::Adjustment> m_size_adj;    // body-size control (refreshed on open)
    int    m_saved_size = 0;     // editor size snapshot taken on focus enter
    double m_saved_zoom = 1.0;   // editor zoom snapshot taken on focus enter

    Gtk::Box*            m_switcher    = nullptr;  // type-to-filter scene list
    Gtk::SearchEntry*    m_switch_entry = nullptr; // owned by m_switcher subtree
    Gtk::ListBox*        m_switch_list  = nullptr; // owned by m_switcher subtree

    // s45 — visible scene navigation: a quiet top-centre breadcrumb that shows the
    // current scene (click → switcher) flanked by prev/next. The door for what was
    // a keyboard-only switcher. Scenes only — non-prose nodes need form rendering
    // focus doesn't have (see the cross-section fork in the slice notes).
    Gtk::Box*            m_navbar    = nullptr;
    Gtk::Button*         m_nav_title = nullptr;

    // s46 — top-left text-tool strip. Icon buttons that reach the editor's
    // existing model/buffer paths (never a focus-local reimplementation of the
    // underlying op). Spell ships first: its highlights are tags on the SHARED
    // buffer, so they already render in m_view; the button only flips the pref
    // and re-applies. Snapshot + link land in the next two slices.
    // s46 — focus-local transient confirmation (the editor's own toast sits on the
    // hidden surface behind focus). A bottom-centre pill that fades in then out.
    Gtk::Label*          m_toast = nullptr;
    sigc::connection     m_toast_conn;

    // s46 — link picker. Focus owns its own node picker (the switcher precedent)
    // and delegates the actual tag insertion to Editor::insert_link on the shared
    // buffer — never the editor's open_link_picker, whose popover parents to the
    // editor window. Lists non-group nodes across all four authored sections.
    Gtk::Box*            m_link_picker = nullptr;
    Gtk::SearchEntry*    m_link_entry  = nullptr;
    Gtk::ListBox*        m_link_list   = nullptr;
    struct LinkEntry { std::string iid; std::string title; std::string section; };
    std::vector<LinkEntry> m_link_entries;   // rebuilt on each open from the model

    // s46 — view-chrome parity. Focus draws its OWN line-number + invisibles
    // overlays over m_view (the editor's gutter/overlay are sibling widgets focus
    // doesn't have). These are per-view, so they use focus-specific prefs (default
    // off). Annotations + hyperlinks are SHARED buffer-tag visuals — a tag's look is
    // one value across both views, so they can't differ per view — toggled through
    // the editor's refresh_* on the shared prefs.
    Gtk::DrawingArea*   m_ln_overlay    = nullptr;
    Gtk::DrawingArea*   m_invis_overlay = nullptr;
    Gtk::Switch*        m_spell_sw      = nullptr;
    Gtk::Switch*        m_ln_switch     = nullptr;
    Gtk::Switch*        m_invis_switch  = nullptr;
    Gtk::Switch*        m_ann_switch    = nullptr;
    Gtk::Switch*        m_links_switch  = nullptr;
    bool                m_view_guard    = false;  // guard programmatic switch sync

    // s45 — left settings drawer. m_drawer is a Revealer (SLIDE_RIGHT) holding the
    // panel; m_drawer_tab is the always-visible pull on the left edge; m_drawer_scrim
    // is a transparent click-away catcher shown only while the drawer is open.
    Gtk::Revealer*       m_drawer       = nullptr;
    Gtk::Widget*         m_drawer_tab   = nullptr;
    Gtk::Box*            m_drawer_scrim = nullptr;

    // s45 — typewriter rail. The mode is a Switch in the drawer; the rail position is
    // a labeled slider row (m_rail_scale), sensitive only while the mode is on. Shares
    // m_prefs.typewriter_position with the editor.
    Gtk::Switch*         m_tw_switch  = nullptr;
    Gtk::Scale*          m_rail_scale = nullptr;
    bool m_tw_guard    = false;   // re-entrancy guard while syncing the switch in code
    bool m_rail_queued = false;   // an idle scroll_to_rail is already pending

    // s45 — slider smoothness: debounced save / size, coalesced spacing relayout.
    sigc::connection m_save_conn;     // pending debounced prefs.save()
    sigc::connection m_size_conn;     // pending debounced size re-tag
    bool             m_look_queued = false;  // an idle apply_focus_look is pending

    // Navigation working set (rebuilt on open; transient, model is the truth)
    std::vector<BinderNode*> m_scenes;     // reading order, from the model
    BinderNode*              m_current = nullptr;

    ClosedCallback m_on_closed;
};

}  // namespace Folio
