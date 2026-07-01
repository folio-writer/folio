#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — DiffView.hpp
// The side-by-side snapshot diff, shown as an editor VIEW (a page in the editor's
// m_view_stack), not a modal. Two paragraph-aligned panes: SNAPSHOT (the past
// photo) and CURRENT (the live document we're saving toward). The diff direction
// is fixed — snapshot → current; you never edit back into a snapshot. Colour
// meaning is anchored to Current: words removed from the snapshot read red +
// strikethrough, words added to reach Current read green. The Swap button only
// rearranges which physical pane each side occupies; it never inverts the colours.
//
// Rendering: one Gtk::Grid inside one Gtk::ScrolledWindow, so the two panes share
// a single vertical scroll and stay row-aligned. A "line" is a paragraph; line
// numbers are paragraph numbers, per side (blank where a side has no line).
// ─────────────────────────────────────────────────────────────────────────────

#include <gtkmm.h>
#include <functional>
#include <string>
#include "DocumentModel.hpp"
#include "SnapshotDiff.hpp"

namespace Folio {

class DiffView : public Gtk::Box {
public:
    DiffView();

    // Point the view at a node and the snapshot index to compare against Current.
    // Rebuilds the panes. Safe to call repeatedly (e.g. when the picker changes).
    void set_target(const BinderNode* node, int snap_idx);

    // s98 — scale the diff body text (paragraphs + line numbers) to follow the
    // editor's zoom. factor is the editor's zoom_factor (1.0 = 100%); the 100%
    // base is 15px (matching the old diff dialog). Synced live from the editor.
    void set_text_scale(double factor);

    // Invoked by the close (×) affordance — the Editor restores the prior view.
    void set_close_callback(std::function<void()> cb) { m_on_close = std::move(cb); }

    // s98 — "pick old text → annotate Current". When the user selects text on the
    // Snapshot side (or focuses a whole snapshot paragraph) and hits "Annotate
    // Current", this fires with (the diffed node, the target Current paragraph
    // index [0-based, empty paragraphs excluded — matches SnapshotDiff::html_to_lines],
    // the picked text). The Editor owns the live buffer, so it does the actual
    // char-offset anchoring + annotation creation. Non-destructive to the prose.
    std::function<void(const BinderNode*, int, const std::string&)> on_add_annotation;

private:
    void build_header();
    void rebuild();                 // recompute rows + repaint the grid
    void clear_grid();
    void refresh_pane_headers();

    // s98 — harvest the current pick (a text selection inside a snapshot label, or
    // else the focused snapshot paragraph) and route it to on_add_annotation.
    void annotate_current_from_pick();
    // s98 — choose which Current paragraph the annotation lands on (a diff row
    // whose Current side has text). Highlights the chosen paragraph's number.
    void set_annotation_destination(int chosen_row);

    // Which logical side a physical pane currently shows (Swap flips this).
    enum class Side { Snapshot, Current };
    Side left_side()  const { return m_swapped ? Side::Current  : Side::Snapshot; }
    Side right_side() const { return m_swapped ? Side::Snapshot : Side::Current;  }

    // Build Pango markup for one side of a row (escaping + colour spans).
    static std::string side_markup(const DiffRow& r, Side side);
    static int         side_lineno(const DiffRow& r, Side side);

    // ── State ────────────────────────────────────────────────────────────────
    const BinderNode*     m_node     = nullptr;
    int                   m_snap_idx = -1;
    bool                  m_swapped  = false;
    std::vector<DiffRow>  m_rows;
    std::function<void()> m_on_close;
    bool                  m_building = false;   // guard the picker's change signal
    double                m_scale    = 1.0;     // editor zoom mirrored → markup font size
    Glib::RefPtr<Gtk::CssProvider> m_text_css;  // filler shading (font size is in markup)

    // ── Widgets ──────────────────────────────────────────────────────────────
    Gtk::Box            m_header       { Gtk::Orientation::HORIZONTAL, 8 };
    Gtk::Label          m_title;
    Gtk::ComboBoxText   m_snap_picker;
    Gtk::Button         m_swap_btn;
    Gtk::Button         m_annotate_btn;    // "pick old text → annotate Current"
    Gtk::Button         m_close_btn;

    Gtk::Box            m_pane_headers { Gtk::Orientation::HORIZONTAL, 0 };
    Gtk::Label          m_pane_left;
    Gtk::Label          m_pane_right;

    Gtk::ScrolledWindow m_scroll;
    Gtk::Grid           m_grid;

    Gtk::Box            m_footer       { Gtk::Orientation::HORIZONTAL, 8 };
    Gtk::Label          m_stats;
    Gtk::Label          m_pick_hint;       // transient feedback for the annotate action

    // Snapshot-side text labels by row, refilled each rebuild — the pick harvest
    // reads the active selection / focus from these.
    std::vector<std::pair<int, Gtk::Label*>> m_snap_labels;
    int m_last_clicked_row = -1;   // last plainly-clicked snapshot paragraph (whole-para pick)

    // Current-side line-number labels by row (only rows whose Current side has
    // text), so a chosen destination paragraph can be highlighted without a full
    // rebuild (which would clear a pending snapshot text selection).
    std::vector<std::pair<int, Gtk::Label*>> m_curr_labels;
    int m_dest_current_para = -1;  // chosen destination (0-based); -1 = auto-anchor
    std::string m_size_open;       // current markup font-size prefix, for re-marking numbers
};

} // namespace Folio
