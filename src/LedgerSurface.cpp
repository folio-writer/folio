// ─────────────────────────────────────────────────────────────────────────────
// Folio — LedgerSurface.cpp  (s104)
//
// Read-only card list over Folio::InterchangeLedger, hosted as an editor view.
// See LedgerSurface.hpp. Pure gtkmm + the ledger; no DocumentModel, no engine.
// ─────────────────────────────────────────────────────────────────────────────
#include "LedgerSurface.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace {
// Multiply every label under `w` by `scale` using a Pango scale attribute. The
// app CSS pins an explicit font-size on these label classes, so a container-wide
// CSS font-size can't cascade past them; a scale attribute multiplies whatever
// size Pango resolves, which is the reliable lever (the diff-view lesson). An
// empty attribute list clears the scaling (back to 100%).
void scale_labels(Gtk::Widget& w, double scale) {
    for (Gtk::Widget* c = w.get_first_child(); c; c = c->get_next_sibling()) {
        if (auto* lbl = dynamic_cast<Gtk::Label*>(c)) {
            Pango::AttrList attrs;   // empty list clears scaling (back to 100%)
            if (scale != 1.0) {
                auto a = Pango::Attribute::create_attr_scale(scale);
                attrs.insert(a);
            }
            lbl->set_attributes(attrs);   // set_attributes takes a non-const lvalue
        }
        scale_labels(*c, scale);
    }
}
} // namespace

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

    m_show_hidden.set_label("Show hidden");
    m_show_hidden.add_css_class("flat");
    m_show_hidden.set_tooltip_text("Reveal passes you've hidden from the ledger");
    m_show_hidden.signal_toggled().connect([this]() { rebuild(); });
    m_header.append(m_show_hidden);

    // ── text zoom (webpage-style −/%/+) ───────────────────────────────────────
    auto* zoom = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
    zoom->add_css_class("linked");
    zoom->set_margin_start(4);

    m_zoom_out.set_label("\u2212");   // minus sign
    m_zoom_out.add_css_class("flat");
    m_zoom_out.set_tooltip_text("Smaller text");
    m_zoom_out.signal_clicked().connect([this]() { nudge_scale(-0.10); });
    zoom->append(m_zoom_out);

    m_zoom_reset.set_label("100%");
    m_zoom_reset.add_css_class("flat");
    m_zoom_reset.set_tooltip_text("Reset text size");
    m_zoom_reset.signal_clicked().connect(
        [this]() { nudge_scale(1.0 - m_scale); });   // jump back to 100%
    zoom->append(m_zoom_reset);

    m_zoom_in.set_label("+");
    m_zoom_in.add_css_class("flat");
    m_zoom_in.set_tooltip_text("Larger text");
    m_zoom_in.signal_clicked().connect([this]() { nudge_scale(+0.10); });
    zoom->append(m_zoom_in);

    m_header.append(*zoom);

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
    const bool show_hidden = m_show_hidden.get_active();

    // The toggle is only meaningful when something is actually hidden.
    std::size_t hidden_total = 0;
    for (const auto& e : m_ledger.entries()) if (e.hidden) ++hidden_total;
    m_show_hidden.set_sensitive(hidden_total > 0);

    std::vector<const LedgerEntry*> rows;
    for (const auto& e : m_ledger.entries()) {
        if (e.hidden && !show_hidden)                      continue;   // hidden shelf
        if (fsel == 1 && e.status != PassStatus::Sent)     continue;
        if (fsel == 2 && e.status != PassStatus::Returned) continue;
        rows.push_back(&e);
    }
    std::stable_sort(rows.begin(), rows.end(),
        [](const LedgerEntry* a, const LedgerEntry* b) {
            return a->created_at > b->created_at;   // newest first
        });

    if (rows.empty()) {
        std::string msg;
        if (m_ledger.empty())
            msg = "No editorial passes yet — seal one from Export \u25b8 "
                  "Folio Interchange.";
        else if (!show_hidden && hidden_total == m_ledger.size())
            msg = "Every pass is hidden. Turn on \u201cShow hidden\u201d to see them.";
        else
            msg = "No passes match this filter.";
        auto* empty = Gtk::make_managed<Gtk::Label>(msg);
        empty->add_css_class("dim-label");
        empty->set_margin_top(40);
        empty->set_justify(Gtk::Justification::CENTER);
        m_list.append(*empty);
        apply_scale();
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

        if (e->hidden) {
            auto* hb = Gtk::make_managed<Gtk::Label>("Hidden");
            hb->add_css_class("dim-label");
            hdr->append(*hb);
            card->set_opacity(0.6);   // archived look — still readable, clearly set aside
        }

        auto* spacer = Gtk::make_managed<Gtk::Box>();
        spacer->set_hexpand(true);
        hdr->append(*spacer);

        if (!e->created_at.empty()) {
            auto* d = Gtk::make_managed<Gtk::Label>(
                "sent " + e->created_at.substr(0, 10));
            d->add_css_class("dim-label");
            hdr->append(*d);
        }

        // ── per-card actions ──────────────────────────────────────────────────
        // Visible pass  → Hide (one click, reversible, non-destructive).
        // Hidden pass   → Unhide, plus a deliberate Delete (the only place the
        //                 hard remove lives, so nothing is destroyed by accident).
        {
            const std::string id    = e->id;
            const std::string recip = e->recipient.empty() ? std::string("this pass")
                                                            : e->recipient;
            if (!e->hidden) {
                auto* hide = Gtk::make_managed<Gtk::Button>("Hide");
                hide->add_css_class("flat");
                hide->set_valign(Gtk::Align::CENTER);
                hide->set_tooltip_text("Hide this pass from the ledger (keeps the record)");
                hide->signal_clicked().connect(
                    [this, id]() { if (m_on_hide) m_on_hide(id, true); });
                hdr->append(*hide);
            } else {
                auto* unhide = Gtk::make_managed<Gtk::Button>("Unhide");
                unhide->add_css_class("flat");
                unhide->set_valign(Gtk::Align::CENTER);
                unhide->set_tooltip_text("Return this pass to the ledger");
                unhide->signal_clicked().connect(
                    [this, id]() { if (m_on_hide) m_on_hide(id, false); });
                hdr->append(*unhide);

                auto* rm = Gtk::make_managed<Gtk::Button>();
                rm->set_icon_name("user-trash-symbolic");
                rm->add_css_class("flat");
                rm->set_valign(Gtk::Align::CENTER);
                rm->set_tooltip_text("Delete this pass permanently");
                rm->signal_clicked().connect([this, id, recip]() {
                    auto dlg = Gtk::AlertDialog::create();
                    dlg->set_message("Permanently delete the pass to " + recip + "?");
                    dlg->set_detail(
                        "This erases your private record of the pass for good. Any "
                        ".folioedit file you already sent is untouched — its sealed "
                        "custody trail travels with the file, not with this book. To "
                        "just set it aside instead, use Unhide \u2192 Hide.");
                    dlg->set_buttons({"Cancel", "Delete"});
                    dlg->set_cancel_button(0);
                    dlg->set_default_button(0);   // safe default is Cancel
                    auto* parent = dynamic_cast<Gtk::Window*>(get_root());
                    if (!parent) { return; }
                    dlg->choose(*parent,
                        [this, dlg, id](const Glib::RefPtr<Gio::AsyncResult>& res) {
                            int choice = 0;
                            try { choice = dlg->choose_finish(res); }
                            catch (...) { return; }   // dismissed → treat as Cancel
                            if (choice == 1 && m_on_remove) m_on_remove(id);
                        });
                });
                hdr->append(*rm);
            }
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

            // s107 — on a returned pass, offer to seal the author's verdicts and
            // send them back to the editor (the host does the work via m_on_return).
            if (e->status == PassStatus::Returned) {
                auto* ret = Gtk::make_managed<Gtk::Button>("Send back to editor\u2026");
                ret->add_css_class("flat");
                ret->set_tooltip_text(
                    "Send your revised prose + accept/decline + resolves back to the editor, "
                    "sealed onto the pass\u2019s custody chain");
                const std::string rid = e->id;
                ret->signal_clicked().connect(
                    [this, rid]() { if (m_on_return) m_on_return(rid); });
                prow->append(*ret);
            }

            card->append(*prow);
        }

        m_list.append(*card);
    }

    apply_scale();
}

// ── text zoom ────────────────────────────────────────────────────────────────
void LedgerSurface::apply_scale() {
    scale_labels(m_list, m_scale);
    m_zoom_reset.set_label(std::to_string(static_cast<int>(std::lround(m_scale * 100.0))) + "%");
}

void LedgerSurface::set_text_scale(double factor) {
    m_scale = std::clamp(factor, 0.5, 3.0);
    apply_scale();   // seed only — no changed callback (that's for user nudges)
}

void LedgerSurface::nudge_scale(double delta) {
    const double next = std::clamp(m_scale + delta, 0.5, 3.0);
    if (next == m_scale) return;
    m_scale = next;
    apply_scale();
    if (m_on_scale_changed) m_on_scale_changed(m_scale);
}

} // namespace Folio
