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

    InterchangeLedger     m_ledger;
    std::function<void()> m_on_close;
    std::function<void(const std::string&)>       m_on_remove;
    std::function<void(const std::string&, bool)> m_on_hide;
    std::function<void(const std::string&)>       m_on_return;   // s107 — send verdicts back
    std::function<void(double)>                   m_on_scale_changed;

    double m_scale = 1.0;

    Gtk::Box         m_header { Gtk::Orientation::HORIZONTAL, 8 };
    Gtk::Label       m_title;
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
