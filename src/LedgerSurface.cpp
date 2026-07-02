// ─────────────────────────────────────────────────────────────────────────────
// Folio — LedgerSurface.cpp  (s104)
//
// Read-only card list over Folio::InterchangeLedger, hosted as an editor view.
// See LedgerSurface.hpp. Pure gtkmm + the ledger; no DocumentModel, no engine.
// ─────────────────────────────────────────────────────────────────────────────
#include "LedgerSurface.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace Folio {

LedgerSurface::LedgerSurface() : Gtk::Box(Gtk::Orientation::VERTICAL, 0) {
    set_name("ledger-surface");
    build_header();

    m_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_scroll.set_vexpand(true);
    m_list.set_margin_top(8);
    m_list.set_margin_bottom(16);
    m_list.set_margin_start(16);
    m_list.set_margin_end(16);
    m_scroll.set_child(m_list);

    append(m_header);
    auto* sep = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
    append(*sep);
    append(m_scroll);
}

void LedgerSurface::build_header() {
    m_header.add_css_class("folio-viewbar");
    m_header.set_margin_start(8);
    m_header.set_margin_end(8);
    m_header.set_margin_top(6);
    m_header.set_margin_bottom(6);

    m_title.set_text("Editorial Ledger");
    m_title.add_css_class("heading");
    m_title.set_halign(Gtk::Align::START);
    m_header.append(m_title);

    auto* spacer = Gtk::make_managed<Gtk::Box>();
    spacer->set_hexpand(true);
    m_header.append(*spacer);

    auto* flabel = Gtk::make_managed<Gtk::Label>("Show:");
    flabel->add_css_class("stat-label");
    m_header.append(*flabel);

    auto items = Gtk::StringList::create({"All", "Sent", "Returned"});
    m_filter = Gtk::make_managed<Gtk::DropDown>(items);
    m_filter->set_selected(0);
    m_filter->set_tooltip_text("Filter passes by status");
    m_filter->property_selected().signal_changed().connect([this]() { rebuild(); });
    m_header.append(*m_filter);

    m_close_btn.set_label("Back to writing");
    m_close_btn.add_css_class("pill-btn");
    m_close_btn.signal_clicked().connect(
        [this]() { if (m_on_close) m_on_close(); });
    m_header.append(m_close_btn);
}

void LedgerSurface::refresh(const InterchangeLedger& ledger) {
    m_ledger = ledger;
    rebuild();
}

void LedgerSurface::rebuild() {
    while (auto* c = m_list.get_first_child()) m_list.remove(*c);

    const int fsel = m_filter ? static_cast<int>(m_filter->get_selected()) : 0;
    // 0 = All, 1 = Sent, 2 = Returned

    std::vector<const LedgerEntry*> rows;
    for (const auto& e : m_ledger.entries()) {
        if (fsel == 1 && e.status != PassStatus::Sent)     continue;
        if (fsel == 2 && e.status != PassStatus::Returned) continue;
        rows.push_back(&e);
    }
    std::stable_sort(rows.begin(), rows.end(),
        [](const LedgerEntry* a, const LedgerEntry* b) {
            return a->created_at > b->created_at;   // newest first
        });

    if (rows.empty()) {
        auto* empty = Gtk::make_managed<Gtk::Label>(
            m_ledger.empty()
                ? "No editorial passes yet — seal one from Export \u25b8 "
                  "Folio Interchange."
                : "No passes match this filter.");
        empty->add_css_class("dim-label");
        empty->set_margin_top(40);
        empty->set_justify(Gtk::Justification::CENTER);
        m_list.append(*empty);
        return;
    }

    for (const LedgerEntry* e : rows) {
        auto* card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
        card->add_css_class("annotation-card");   // reuse the report's card styling
        card->set_margin_bottom(10);

        // ── header row: recipient + status badge + sent date ──────────────────
        auto* hdr = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        hdr->set_margin_top(8);
        hdr->set_margin_start(10);
        hdr->set_margin_end(10);

        auto* who = Gtk::make_managed<Gtk::Label>(
            e->recipient.empty() ? "(unnamed editor)" : e->recipient);
        who->add_css_class("annotation-kind");
        who->set_halign(Gtk::Align::START);
        hdr->append(*who);

        const bool returned = (e->status == PassStatus::Returned);
        auto* badge = Gtk::make_managed<Gtk::Label>(returned ? "Returned" : "Sent");
        badge->add_css_class(returned ? "success" : "dim-label");
        hdr->append(*badge);

        auto* spacer = Gtk::make_managed<Gtk::Box>();
        spacer->set_hexpand(true);
        hdr->append(*spacer);

        if (!e->created_at.empty()) {
            auto* d = Gtk::make_managed<Gtk::Label>(
                "sent " + e->created_at.substr(0, 10));
            d->add_css_class("dim-label");
            hdr->append(*d);
        }
        card->append(*hdr);

        // ── returned line ─────────────────────────────────────────────────────
        if (returned) {
            std::string rl = "returned";
            if (!e->returned_at.empty()) rl += " " + e->returned_at.substr(0, 10);
            rl += "  \u00b7  " + std::to_string(e->annotation_count) +
                  " annotation" + (e->annotation_count == 1 ? "" : "s") + " filed";
            auto* rlbl = Gtk::make_managed<Gtk::Label>(rl);
            rlbl->add_css_class("dim-label");
            rlbl->set_halign(Gtk::Align::START);
            rlbl->set_margin_start(10);
            rlbl->set_margin_end(10);
            card->append(*rlbl);
        }

        // ── scene inventory ───────────────────────────────────────────────────
        {
            std::string titles;
            for (std::size_t i = 0; i < e->inventory.size(); ++i) {
                if (i) titles += ", ";
                titles += e->inventory[i].title.empty() ? e->inventory[i].iid
                                                        : e->inventory[i].title;
            }
            std::string inv = std::to_string(e->inventory.size()) + " scene" +
                              (e->inventory.size() == 1 ? "" : "s");
            if (!titles.empty()) inv += ": " + titles;
            auto* ilbl = Gtk::make_managed<Gtk::Label>(inv);
            ilbl->add_css_class("dim-label");
            ilbl->set_wrap(true);
            ilbl->set_xalign(0.0f);
            ilbl->set_margin_start(10);
            ilbl->set_margin_end(10);
            ilbl->set_margin_top(2);
            card->append(*ilbl);
        }

        // ── passphrase + copy (unsealed passes have none: show "—") ────────────
        {
            const bool sealed = !e->phrase.empty();
            auto* prow = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
            prow->set_margin_start(10);
            prow->set_margin_end(10);
            prow->set_margin_top(4);
            prow->set_margin_bottom(8);

            auto* plbl = Gtk::make_managed<Gtk::Label>("Passphrase:");
            plbl->add_css_class("stat-label");
            prow->append(*plbl);

            auto* phrase = Gtk::make_managed<Gtk::Label>(
                sealed ? e->phrase : std::string("\u2014  (unsealed \u2014 for a chat / AI)"));
            phrase->set_selectable(sealed);
            phrase->set_halign(Gtk::Align::START);
            phrase->set_hexpand(true);
            phrase->set_xalign(0.0f);
            phrase->add_css_class(sealed ? "monospace" : "dim-label");
            prow->append(*phrase);

            if (sealed) {
                auto* copy = Gtk::make_managed<Gtk::Button>("Copy");
                copy->add_css_class("flat");
                copy->set_tooltip_text("Copy the passphrase to the clipboard");
                const std::string ph = e->phrase;
                copy->signal_clicked().connect([this, ph]() {
                    if (auto cb = get_clipboard()) cb->set_text(ph);
                });
                prow->append(*copy);
            }

            card->append(*prow);
        }

        m_list.append(*card);
    }
}

} // namespace Folio
