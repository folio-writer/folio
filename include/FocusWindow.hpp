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
    void build_control_bar();   // hover-reveal width/zoom/font/size/spacing/color
    void build_switcher();      // type-to-filter scene overlay
    void repopulate_switcher(); // refill m_switch_list from m_scenes + filter text
    void activate_switch_row(Gtk::ListBoxRow* row);  // resolve row → scene → goto
    void wire_keys();           // Esc → close; switcher hotkey; next/prev hotkeys

    // ── Look (applies to m_view ONLY; never writes to the editor) ─────────────
    void apply_focus_look();        // view-level: line spacing, then geometry + padding
    void apply_typewriter_padding();// half-viewport top/bottom so the active line centres
    void apply_focus_geometry();    // page width % of the focus viewport (self-contained)

    // ── Navigation (delegates content to Editor::load_node) ───────────────────
    void rebuild_scene_list();          // ← model.manuscript_in_reading_order()
    void goto_node(BinderNode* node);   // Editor::load_node(node); update title/index
    void goto_relative(int delta);      // next/prev within m_scenes
    void open_switcher();               // populate + show the filter overlay

    // ── Refs (injected; not owned) ────────────────────────────────────────────
    DocumentModel& m_model;
    FolioPrefs&    m_prefs;
    Editor&        m_editor;

    // ── Own widgets / own state (the whole point — none of this is shared) ────
    Gtk::Overlay         m_overlay;
    Gtk::ScrolledWindow  m_scroll;
    Gtk::TextView        m_view;            // bound to the editor's shared buffer
    Glib::RefPtr<Gtk::Adjustment> m_size_adj;    // body-size control (refreshed on open)
    int    m_saved_size = 0;     // editor size snapshot taken on focus enter
    double m_saved_zoom = 1.0;   // editor zoom snapshot taken on focus enter

    Gtk::Box*            m_control_bar = nullptr; // hover-reveal, like the old bar
    Gtk::Box*            m_switcher    = nullptr;  // type-to-filter scene list
    Gtk::SearchEntry*    m_switch_entry = nullptr; // owned by m_switcher subtree
    Gtk::ListBox*        m_switch_list  = nullptr; // owned by m_switcher subtree

    // Navigation working set (rebuilt on open; transient, model is the truth)
    std::vector<BinderNode*> m_scenes;     // reading order, from the model
    BinderNode*              m_current = nullptr;

    ClosedCallback m_on_closed;
};

}  // namespace Folio
