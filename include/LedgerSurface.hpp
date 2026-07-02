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

private:
    void build_header();
    void rebuild();   // repaint the card list from m_ledger honouring the status filter

    InterchangeLedger     m_ledger;
    std::function<void()> m_on_close;

    Gtk::Box       m_header { Gtk::Orientation::HORIZONTAL, 8 };
    Gtk::Label     m_title;
    Gtk::DropDown* m_filter = nullptr;   // All / Sent / Returned
    Gtk::Button    m_close_btn;

    Gtk::ScrolledWindow m_scroll;
    Gtk::Box            m_list { Gtk::Orientation::VERTICAL, 0 };
};

} // namespace Folio
