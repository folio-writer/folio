#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — LedgerSurface.hpp  (s104 — the editorial ledger, shown as an editor view)
//
// The author-side record of editorial passes, hosted as a PAGE in the editor's
// m_view_stack (the diff / gallery / journal family), NOT a dialog and NOT a lens
// in the Write/Board/Map switcher — those are manuscript projections; this is
// editorial metadata. open_ledger() swaps it in over the write chrome; a
// "Back to writing" affordance returns to the prior view (mirrors DiffView).
//
// Read-only: one card per pass — recipient, a Sent/Returned badge, sent/returned
// dates, scene inventory, and the passphrase with a Copy button. The passphrase
// is shown in the clear on purpose: it is already plaintext in interchange.json
// (threat model is an untrusted editor, not the author's own screen), and being
// the retrieval path is this surface's whole job. A status filter (All / Sent /
// Returned) sits in the header.
//
// Depends ONLY on the pure Folio::InterchangeLedger — no DocumentModel, no
// folioedit engine. It is handed a ledger snapshot via refresh() and renders it.
// ─────────────────────────────────────────────────────────────────────────────
#include <gtkmm.h>
#include <functional>

#include "InterchangeLedger.hpp"

namespace Folio {

// s111 §29 — one editor note shown in a Returned card's note list. The surface
// holds NO model reference: the host fills these via the notes-provider callback
// and the surface fires note actions back by (pass, scene, note id). `verdict` is
// the author's decision so far ("" / "proposed" both read as undecided); `receded`
// == author_resolved (the author set it aside -> dim, still reachable, §28.5).
struct LedgerNote {
    int         note_id = 0;      // annotation id (unique within its scene node)
    std::string scene_iid;        // for "Go to text"
    std::string scene_title;
    std::string kind;             // Proofreader / Editor / Writer
    std::string text;             // the comment
    std::string quote;            // the anchored prose ("" if floating / collapsed)
    std::string verdict;          // "" | proposed (undecided) | accepted | declined
    bool        receded = false;  // author_resolved -> dim + set aside
};

// The author's move on one note. The surface derives the intent from the row's
// current state (clicking the active verdict un-decides it); the host maps each to
// a model mutation. Accept/Dismiss sit at EQUAL weight (§28.2) — no good/bad.
enum class NoteAction { Accept, Dismiss, Undecide, Resolve, Reopen, GoTo };

class LedgerSurface : public Gtk::Box {
public:
    LedgerSurface();

    // Point the surface at a ledger snapshot and rebuild the cards. The ledger is
    // copied in, so the view renders independently of the model afterwards.
    void refresh(const InterchangeLedger& ledger);

    // Fired by the "Back to writing" affordance — the Editor restores the prior view.
    void set_close_callback(std::function<void()> cb) { m_on_close = std::move(cb); }

    // Fired when the user removes a pass from the ledger (after confirming). The
    // host drops it from the real DocumentModel ledger, marks the project modified,
    // and hands a fresh snapshot back via refresh(). The surface itself holds only
    // a copy, so it does not mutate the ledger directly.
    void set_remove_callback(std::function<void(const std::string& id)> cb) {
        m_on_remove = std::move(cb);
    }

    // Fired when the user hides / un-hides a pass. Same round-trip as remove: the
    // host flips the flag on the model ledger, marks modified, and re-refreshes.
    // Hiding is reversible and never destroys the entry — the register stays whole.
    void set_hide_callback(std::function<void(const std::string& id, bool hidden)> cb) {
        m_on_hide = std::move(cb);
    }

    // s107 — fired when the author asks to send their accept/decline verdicts on a
    // returned pass back to the editor. The surface only fires the id; the host
    // reads the stashed carrier, gathers the pass's verdicted annotations from the
    // model, and writes a sealed return (Interchange::write_return). Shown only on
    // Returned passes (a Sent pass has no author verdicts yet).
    void set_return_callback(std::function<void(const std::string& id)> cb) {
        m_on_return = std::move(cb);
    }

    // s111 §29 — the Ledger becomes the loop's driver's seat. Two new host hooks,
    // fired the same id-callback way as Send back (the surface stays view-only;
    // MainWindow owns the dialogs + model work):
    //   export      — the "Send to editor…" front door (opens interchange export).
    //   acknowledge — an action on a Sent card: absorb the return, hard-bound to
    //                 that pass by id (a different pass's return is refused).
    void set_export_callback(std::function<void()> cb) {
        m_on_export = std::move(cb);
    }
    void set_acknowledge_callback(std::function<void(const std::string& id)> cb) {
        m_on_acknowledge = std::move(cb);
    }

    // s111 §29 slice 2 — the per-pass note list. The surface asks the host for a
    // pass's notes (provider) and fires the author's per-note action back; the host
    // owns the model mutation + repaint. The surface still holds no model (§29.3).
    void set_notes_provider(
        std::function<std::vector<LedgerNote>(const std::string& pass_id)> cb) {
        m_notes_provider = std::move(cb);
    }
    void set_note_action_callback(
        std::function<void(const std::string& pass_id, const std::string& scene_iid,
                           int note_id, NoteAction action)> cb) {
        m_on_note_action = std::move(cb);
    }

    // s111 §29 — "Show report" on a Returned card: opens the read-only annotation
    // report scoped to that pass's editor (the complete record of the interaction;
    // the camera to the note list's driver's seat).
    void set_show_report_callback(std::function<void(const std::string& id)> cb) {
        m_on_show_report = std::move(cb);
    }

    // Text zoom for the card list (the "data"), driven by the header +/- control.
    // 1.0 = 100%. Clamped; multiplies on top of the CSS-resolved font sizes via a
    // Pango scale attribute (CSS font-size is pinned per class, so a scale attr is
    // the reliable lever — same lesson as the diff view). set_text_scale() is how
    // the host seeds the persisted zoom; it does NOT fire the changed callback.
    void set_text_scale(double factor);
    double text_scale() const { return m_scale; }

    // Fired when the user changes the zoom via the +/- control, so the host can
    // persist it (FolioPrefs::data_view_zoom_pct). Not fired by set_text_scale().
    void set_scale_changed_callback(std::function<void(double factor)> cb) {
        m_on_scale_changed = std::move(cb);
    }

private:
    void build_header();
    void rebuild();       // repaint the card list from m_ledger honouring the status filter
    void apply_scale();   // (re)attribute the card-list labels + refresh the % readout
    void nudge_scale(double delta);   // +/- handler: change, apply, fire callback
    Gtk::Widget* build_notes_section(const LedgerEntry& e);           // s111 §29 — "Work notes (N)"
    Gtk::Widget* build_note_row(const std::string& pass_id, const LedgerNote& n);

    InterchangeLedger     m_ledger;
    std::function<void()> m_on_close;
    std::function<void(const std::string&)>       m_on_remove;
    std::function<void(const std::string&, bool)> m_on_hide;
    std::function<void(const std::string&)>       m_on_return;   // s107 — send verdicts back
    std::function<void()>                         m_on_export;      // s111 §29 — front door
    std::function<void(const std::string&)>       m_on_acknowledge; // s111 §29 — ack a Sent card
    std::function<std::vector<LedgerNote>(const std::string&)>                 m_notes_provider;
    std::function<void(const std::string&, const std::string&, int, NoteAction)> m_on_note_action;
    std::function<void(const std::string&)>       m_on_show_report; // s111 §29 — read-only report
    std::function<void(double)>                   m_on_scale_changed;

    double m_scale = 1.0;

    Gtk::Box         m_header { Gtk::Orientation::HORIZONTAL, 8 };
    Gtk::Label       m_title;
    Gtk::Button      m_export_btn;               // s111 §29 — "Send to editor…" front door
    Gtk::DropDown*   m_filter = nullptr;        // All / Sent / Returned
    Gtk::ToggleButton m_show_hidden;            // reveal hidden passes (default off)
    Gtk::Button      m_zoom_out;                // −
    Gtk::Button      m_zoom_reset;              // "100%" — click resets to 100%
    Gtk::Button      m_zoom_in;                 // +
    Gtk::Button      m_close_btn;

    Gtk::ScrolledWindow m_scroll;
    Gtk::Box            m_list { Gtk::Orientation::VERTICAL, 0 };
};

} // namespace Folio
