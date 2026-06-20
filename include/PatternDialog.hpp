#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — PatternDialog.hpp   (s24 — Layer 3: the pattern input dialog)
//
// "New from Pattern…" used to scaffold straight from the built-in defaults.
// This dialog puts the author's hands on the inputs the engine (Layer 1/2)
// already takes — DESIGN §5.4. It is pure GTK glue over the proven pure
// ModulePlanner: it gathers a PlanInputs and hands it back via a callback;
// MainWindow does the discard / new-project / palette-install / plan /
// materialize. The dialog never touches the model.
//
//   ┌─ New from Pattern ─────────────────────────────────────────────────────┐
//   │  LENGTH                                                                 │
//   │   Target words      [ 90,000 ▲▼]   Words / scene  [ 1,130 ▲▼]          │
//   │  CHAPTERS                                                               │
//   │   Chapters          [    10 ▲▼]                                        │
//   │   ≈ 78 scenes in 10 chapters (about 7–8 per chapter) · + prologue/epi  │
//   │  PACING                                                                 │
//   │   Scene energy      [ Flat ▾ ]                                          │
//   │  STRUCTURE                                                              │
//   │   ☐ Organize into Parts        [ Part ▾ ]                              │
//   │      "On the Rebound"  [ 4 ▲▼]  ✕                                       │
//   │      …                            [ + Add Part ]                        │
//   │  BOOKENDS                                                               │
//   │   ☑ Prologue   ☑ Epilogue                                              │
//   │                                          [ Cancel ]  [ Create ]         │
//   └────────────────────────────────────────────────────────────────────────┘
//
// The live readout is produced by actually calling ModulePlanner::plan() on the
// current inputs — so the count the author sees is exactly what materialisation
// will lay down. No second copy of the apportionment math to drift.
// ─────────────────────────────────────────────────────────────────────────────

#include "Module.hpp"        // PlanInputs, Module, built_in_folio_keypoints()
#include "ModuleBoard.hpp"   // s28 — the read-only mixing-board render
#include "ModuleLibrary.hpp" // s30 — pattern library (seed / list / save / load)
#include <functional>
#include <gtkmm.h>
#include <string>
#include <vector>

namespace Folio {

class PatternDialog : public Gtk::Window {
public:
    // Fired on "Create" with the authored module + the gathered inputs. The
    // dialog closes itself afterwards; the receiver owns discard/new-project/
    // scaffold. (s29: the module now travels — the author edits the arc, so the
    // built arc must be the edited one, not the built-in default.)
    using ApplyCallback = std::function<void(const Module&, const PlanInputs&)>;

    explicit PatternDialog(Gtk::Window& parent);

    void set_apply_callback(ApplyCallback cb) { m_on_apply = std::move(cb); }

    // The inputs as currently configured by the controls.
    PlanInputs current_inputs() const;

private:
    ApplyCallback m_on_apply;

    // The arc the preview/scaffold is built from. Built-in 14-KP module; held as
    // the SHELL (id/name/top/pacing/deploy). The editable KP list lives in m_arc;
    // build_module() reassembles the two for the board, the preview, and Create.
    Module m_module;

    // s29 — the editable arc: a FLAT KP list (the board flattens acts anyway, and
    // the told-line is one flat arc). The Key Points editor is the dialog's big
    // chunk: count (add/remove), names (label), meanings (description), reorder.
    // Energies (frenetic/arc) are NOT edited here — they're Inspector metadata +
    // board faders (right-click "Set value…"). Weight is the board dividers.
    std::vector<KeyPoint> m_arc;
    Module build_module() const;   // shell + m_arc, renumbered (order/color = 1..n)

    // s28 — read-only render of the arc (the mixing board), at the top of the
    // dialog so the author sees the shape they're editing. Faders are s29-next.
    ModuleBoard m_board;

    // ── Pattern library (s30) ───────────────────────────────────────────────────
    // A dropdown of saved patterns (genres) seeded with the built-ins. Picking one
    // loads it into the editor; "Save as…" banks the current arc as a new pattern.
    Gtk::DropDown*                    m_pattern_dd = nullptr;
    std::vector<ModuleLibrary::Entry> m_entries;
    Gtk::Button                       m_btn_save_pattern;
    Gtk::Popover*                     m_save_pop  = nullptr;
    Gtk::Entry*                       m_save_name = nullptr;
    bool                              m_loading_pattern = false;  // guard DD echo

    // ── Layout ────────────────────────────────────────────────────────────────
    // Board + footer are persistent; the detail sections live in tabbed pages
    // (a Stack driven by a StackSwitcher) so the dialog stays short (s29).
    Gtk::Box  m_root  { Gtk::Orientation::VERTICAL, 0 };
    Gtk::Box  m_footer{ Gtk::Orientation::HORIZONTAL, 8 };
    Gtk::Box  m_board_host{ Gtk::Orientation::VERTICAL, 0 };
    Gtk::Stack         m_stack;
    Gtk::StackSwitcher m_switcher;
    Gtk::Box  m_page_kp       { Gtk::Orientation::VERTICAL, 0 };
    Gtk::Box  m_page_length   { Gtk::Orientation::VERTICAL, 0 };
    Gtk::Box  m_page_structure{ Gtk::Orientation::VERTICAL, 0 };
    Gtk::Box* m_dest = nullptr;   // current append target while build_*() runs

    // ── Length ────────────────────────────────────────────────────────────────
    Gtk::SpinButton m_spin_target;       // target words
    Gtk::SpinButton m_spin_scene_words;  // avg words / scene

    // ── Key Points (the big chunk — identity editor) ────────────────────────────
    Gtk::ScrolledWindow m_kp_scroller;
    Gtk::Box            m_kp_list{ Gtk::Orientation::VERTICAL, 6 };
    Gtk::Button         m_btn_load_kps;   // seed the built-in 14
    Gtk::Button         m_btn_add_kp;     // append a blank KP

    // ── Chapters ──────────────────────────────────────────────────────────────
    Gtk::SpinButton m_spin_chapters;
    Gtk::Label      m_preview_lbl;       // live scenes/chapter readout

    // ── Pacing ────────────────────────────────────────────────────────────────
    Gtk::DropDown*  m_pace_dd = nullptr; // Flat / Build to spike
    // s30 — the pacing pattern (the genre's scene-energy fingerprint) is now shown
    // and programmable: an editable "30, 30, 62, 100" field + a step-bar strip.
    Gtk::Entry*      m_pace_pattern_entry = nullptr;
    Gtk::Box*        m_pace_pattern_box   = nullptr;   // field + strip; hidden in Flat
    Gtk::DrawingArea m_pace_strip;
    bool             m_loading_pace = false;   // guard programmatic entry set

    // ── Structure (optional Parts) ──────────────────────────────────────────────
    Gtk::CheckButton m_chk_parts;
    Gtk::DropDown*   m_container_dd = nullptr;  // Part / Book
    Gtk::Box         m_parts_detail{ Gtk::Orientation::VERTICAL, 8 };
    Gtk::Box         m_parts_list  { Gtk::Orientation::VERTICAL, 6 };
    Gtk::Button      m_btn_add_part;

    // Data model for the parts (widgets are rebuilt from this — same discipline
    // as ImportDialog's file list: the model is authoritative, never the widget).
    struct PartRow { std::string title; int chapters = 1; };
    std::vector<PartRow> m_parts;

    // ── Bookends ────────────────────────────────────────────────────────────────
    Gtk::CheckButton m_chk_prologue;
    Gtk::CheckButton m_chk_epilogue;

    // ── Footer ────────────────────────────────────────────────────────────────
    Gtk::Button m_btn_cancel;
    Gtk::Button m_btn_create;

    // ── Build helpers ────────────────────────────────────────────────────────
    Gtk::Widget* make_section(const std::string& title);
    Gtk::Widget* make_row(const std::string& label, Gtk::Widget& w);
    void build_pattern_row();
    void build_board();
    void build_keypoints();
    void build_length();
    void build_chapters();
    void build_pacing();
    void build_structure();
    void build_bookends();
    void build_footer();

    // ── Key Points management (model-authoritative: m_arc drives the widgets) ───
    void rebuild_kp_list();           // rows from m_arc (structural changes)
    void renumber_kps();              // order/color_idx = 1..n after add/remove/move
    void refresh_arc_views();         // board + preview after a structural edit
    void add_keypoint();              // append a blank KP
    void load_builtin_kps();          // reseed m_arc from the built-in 14
    void move_kp(size_t idx, int dir);// reorder by ±1
    void remove_kp(size_t idx);

    // ── Parts management ───────────────────────────────────────────────────────
    void on_parts_toggled();
    void seed_parts_from_chapters();   // first enable: split current chapters into parts
    void add_part(const std::string& title, int chapters);
    void remove_part(size_t idx);
    void rebuild_parts_list();
    int  parts_chapter_total() const;

    // Update the live readout and the enabled/disabled state of dependent rows.
    void update_preview();
    bool m_updating = false;   // re-entrancy guard (preview writes the chapter spin)

    // ── Pattern library management (s30) ────────────────────────────────────────
    void refresh_pattern_dropdown(const std::string& select_id = "");
    void refresh_pace_pattern();   // s30 — sync pacing entry + strip from m_module
    void on_pattern_selected();
    void load_module_into_editor(const Module& m);
    void save_current_pattern(const std::string& name);

    void on_create();
};

} // namespace Folio
