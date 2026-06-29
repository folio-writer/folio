// ─────────────────────────────────────────────────────────────────────────────
// Folio — RulerManagerDialog.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "RulerManagerDialog.hpp"
#include <gtkmm/adjustment.h>
#include <gtkmm/stringlist.h>
#include <cmath>

namespace Folio {

const char* RulerManagerDialog::UNIT_STRINGS[] = {
    "mm", "cm", "inch", "pt", "pc", nullptr
};
const char* RulerManagerDialog::UNIT_LABELS[] = {
    "Millimetres (mm)", "Centimetres (cm)", "Inches (in)",
    "Points (pt)", "Picas (pc)", nullptr
};

RulerManagerDialog::RulerManagerDialog(Gtk::Window& parent, FolioPrefs& prefs)
    : m_prefs(prefs) {
    set_transient_for(parent);
    set_modal(false);
    set_title("Ruler Settings");
    set_default_size(380, 680);
    set_resizable(true);
    build();
}

void RulerManagerDialog::build() {
    auto* scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    scroll->set_vexpand(true);
    scroll->set_min_content_height(400);
    scroll->set_max_content_height(680);

    auto* outer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    outer->set_margin_top(16);
    outer->set_margin_bottom(16);
    outer->set_margin_start(20);
    outer->set_margin_end(20);
    scroll->set_child(*outer);

    auto make_sep = []() {
        auto* s = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
        s->set_margin_top(10); s->set_margin_bottom(10);
        return s;
    };
    auto make_heading = [](const char* t) {
        auto* l = Gtk::make_managed<Gtk::Label>(t);
        l->add_css_class("heading");
        l->set_halign(Gtk::Align::START);
        l->set_margin_bottom(6);
        return l;
    };
    auto make_lbl = [](const char* t) {
        auto* l = Gtk::make_managed<Gtk::Label>(t);
        l->add_css_class("stat-label");
        l->set_halign(Gtk::Align::START);
        l->set_hexpand(true);
        return l;
    };
    auto make_row = []() {
        auto* r = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        r->set_valign(Gtk::Align::CENTER);
        r->set_margin_bottom(4);
        return r;
    };

    // ── Units ─────────────────────────────────────────────────────────────────
    outer->append(*make_heading("Units"));
    auto unit_items = Gtk::StringList::create({
        "Millimetres (mm)", "Centimetres (cm)", "Inches (in)",
        "Points (pt)", "Picas (pc)"
    });
    m_unit_dd = Gtk::make_managed<Gtk::DropDown>(unit_items);
    m_unit_dd->set_hexpand(true);
    {
        guint sel = 1;
        for (int i = 0; UNIT_STRINGS[i]; ++i)
            if (m_prefs.ruler_unit == UNIT_STRINGS[i]) { sel = i; break; }
        m_unit_dd->set_selected(sel);
    }
    m_unit_dd->property_selected().signal_changed().connect([this]() {
        if (m_loading) return;
        guint sel = m_unit_dd->get_selected();
        if (UNIT_STRINGS[sel]) {
            m_prefs.ruler_unit = UNIT_STRINGS[sel];
            try { m_prefs.save(); } catch (...) {}
            refresh();
        }
    });
    outer->append(*m_unit_dd);

    outer->append(*make_sep());

    // ── Page width ────────────────────────────────────────────────────────────
    outer->append(*make_heading("Page Width"));
    {
        auto* note = Gtk::make_managed<Gtk::Label>(
            "Drag the blue bars at the page edges on the ruler, or set here:");
        note->add_css_class("dim-label");
        note->set_halign(Gtk::Align::START);
        note->set_wrap(true);
        note->set_margin_bottom(4);
        outer->append(*note);
    }
    auto* pw_row = make_row();
    pw_row->append(*make_lbl("Width"));
    m_page_width_spin = Gtk::make_managed<Gtk::SpinButton>();
    m_page_width_spin->set_adjustment(
        Gtk::Adjustment::create(m_prefs.editor_page_width_pct, 15.0, 100.0, 1.0, 5.0));
    m_page_width_spin->set_digits(0);
    m_page_width_spin->set_numeric(true);
    m_page_width_spin->set_width_chars(4);
    auto* pw_pct = Gtk::make_managed<Gtk::Label>("% of window");
    pw_pct->add_css_class("stat-label");
    pw_row->append(*m_page_width_spin);
    pw_row->append(*pw_pct);
    outer->append(*pw_row);

    outer->append(*make_sep());

    // ── Margins ───────────────────────────────────────────────────────────────
    outer->append(*make_heading("Page Margins"));
    {
        auto* note = Gtk::make_managed<Gtk::Label>(
            "Set independently or use the 🔗 lock to keep them equal.");
        note->add_css_class("dim-label");
        note->set_halign(Gtk::Align::START);
        note->set_wrap(true);
        note->set_margin_bottom(4);
        outer->append(*note);
    }

    // Left margin row
    auto* lmg_row = make_row();
    lmg_row->append(*make_lbl("Left"));
    m_left_margin_spin = Gtk::make_managed<Gtk::SpinButton>();
    m_left_margin_spin->set_digits(2);
    m_left_margin_spin->set_numeric(true);
    m_left_margin_spin->set_width_chars(6);
    m_left_margin_unit = Gtk::make_managed<Gtk::Label>();
    m_left_margin_unit->add_css_class("stat-label");

    // Lock button between the two margin spins
    m_margins_lock = Gtk::make_managed<Gtk::ToggleButton>();
    m_margins_lock->set_label(m_prefs.editor_margins_linked ? "🔗" : "🔓");
    m_margins_lock->set_active(m_prefs.editor_margins_linked);
    m_margins_lock->set_tooltip_text("Link left and right margins");
    m_margins_lock->add_css_class("flat");

    lmg_row->append(*m_left_margin_spin);
    lmg_row->append(*m_left_margin_unit);
    lmg_row->append(*m_margins_lock);
    outer->append(*lmg_row);

    // Right margin row
    auto* rmg_row = make_row();
    rmg_row->append(*make_lbl("Right"));
    m_right_margin_spin = Gtk::make_managed<Gtk::SpinButton>();
    m_right_margin_spin->set_digits(2);
    m_right_margin_spin->set_numeric(true);
    m_right_margin_spin->set_width_chars(6);
    m_right_margin_spin->set_sensitive(!m_prefs.editor_margins_linked);
    m_right_margin_unit = Gtk::make_managed<Gtk::Label>();
    m_right_margin_unit->add_css_class("stat-label");
    rmg_row->append(*m_right_margin_spin);
    rmg_row->append(*m_right_margin_unit);
    outer->append(*rmg_row);

    outer->append(*make_sep());

    // ── Indent ────────────────────────────────────────────────────────────────
    outer->append(*make_heading("First-line Indent"));
    {
        auto* note = Gtk::make_managed<Gtk::Label>(
            "▼ white triangle at top of ruler. "
            "▲ body indent and ▲ right indent set via the other top handles.");
        note->add_css_class("dim-label");
        note->set_halign(Gtk::Align::START);
        note->set_wrap(true);
        note->set_margin_bottom(4);
        outer->append(*note);
    }
    auto* ind_row = make_row();
    ind_row->append(*make_lbl("First-line"));
    m_indent_spin = Gtk::make_managed<Gtk::SpinButton>();
    m_indent_spin->set_digits(2);
    m_indent_spin->set_numeric(true);
    m_indent_spin->set_width_chars(6);
    m_indent_spin->set_sensitive(m_prefs.first_line_indent);
    m_indent_unit = Gtk::make_managed<Gtk::Label>();
    m_indent_unit->add_css_class("stat-label");
    ind_row->append(*m_indent_spin);
    ind_row->append(*m_indent_unit);
    outer->append(*ind_row);

    outer->append(*make_sep());

    // ── Paragraph spacing ─────────────────────────────────────────────────────
    outer->append(*make_heading("Paragraph Spacing"));
    {
        auto* note = Gtk::make_managed<Gtk::Label>(
            "Extra space added above each paragraph.");
        note->add_css_class("dim-label");
        note->set_halign(Gtk::Align::START);
        note->set_margin_bottom(4);
        outer->append(*note);
    }
    auto* ps_row = make_row();
    ps_row->append(*make_lbl("Space above ¶"));
    m_para_spacing_spin = Gtk::make_managed<Gtk::SpinButton>();
    m_para_spacing_spin->set_adjustment(
        Gtk::Adjustment::create(m_prefs.paragraph_spacing_px, 0.0, 120.0, 1.0, 8.0));
    m_para_spacing_spin->set_digits(0);
    m_para_spacing_spin->set_numeric(true);
    m_para_spacing_spin->set_width_chars(4);
    auto* ps_px = Gtk::make_managed<Gtk::Label>("px");
    ps_px->add_css_class("stat-label");
    ps_row->append(*m_para_spacing_spin);
    ps_row->append(*ps_px);
    outer->append(*ps_row);

    outer->append(*make_sep());
    outer->append(*make_heading("Tab Stops"));

    auto* tab_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    tab_scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    tab_scroll->set_min_content_height(80);
    tab_scroll->set_max_content_height(180);

    m_tab_list = Gtk::make_managed<Gtk::ListBox>();
    m_tab_list->set_selection_mode(Gtk::SelectionMode::NONE);
    m_tab_list->add_css_class("boxed-list");
    tab_scroll->set_child(*m_tab_list);
    outer->append(*tab_scroll);

    m_add_tab_btn = Gtk::make_managed<Gtk::Button>("+ Add Tab Stop");
    m_add_tab_btn->add_css_class("flat");
    m_add_tab_btn->set_halign(Gtk::Align::START);
    m_add_tab_btn->set_margin_top(6);
    m_add_tab_btn->signal_clicked().connect([this]() {
        double default_pt = 36.0;
        if (!m_prefs.tab_stops.empty()) {
            double px_per_unit = RulerUnits::unit_to_px(1.0, current_unit());
            default_pt = m_prefs.tab_stops.back().position_pt +
                         RulerUnits::px_to_pt(px_per_unit);
        }
        Folio::TabStop ts;
        ts.position_pt = default_pt;
        ts.type        = m_prefs.ruler_tab_type;
        m_prefs.tab_stops.push_back(ts);
        std::sort(m_prefs.tab_stops.begin(), m_prefs.tab_stops.end(),
                  [](const Folio::TabStop& a, const Folio::TabStop& b) {
                      return a.position_pt < b.position_pt; });
        rebuild_tab_list();
        if (on_tab_stops_changed) on_tab_stops_changed();
        try { m_prefs.save(); } catch (...) {}
    });
    outer->append(*m_add_tab_btn);
    set_child(*scroll);

    // ── Wire signals (after all widgets built) ────────────────────────────────

    // Initialise values
    refresh();

    m_page_width_spin->signal_value_changed().connect([this]() {
        if (m_loading) return;
        m_prefs.editor_page_width_pct = std::max(15, std::min(100,
            (int)m_page_width_spin->get_value()));
        try { m_prefs.save(); } catch (...) {}
        if (on_geometry_changed) on_geometry_changed();
    });

    m_left_margin_spin->signal_value_changed().connect([this]() {
        if (m_loading) return;
        int px = unit_to_px(m_left_margin_spin->get_value());
        m_prefs.editor_left_margin_px  = px;
        m_prefs.editor_page_margin_px  = px; // keep legacy in sync
        if (m_prefs.editor_margins_linked) {
            m_prefs.editor_right_margin_px = px;
            m_loading = true;
            if (m_right_margin_spin)
                m_right_margin_spin->set_value(margin_to_unit(px));
            m_loading = false;
        }
        try { m_prefs.save(); } catch (...) {}
        if (on_geometry_changed) on_geometry_changed();
    });

    m_right_margin_spin->signal_value_changed().connect([this]() {
        if (m_loading) return;
        int px = unit_to_px(m_right_margin_spin->get_value());
        m_prefs.editor_right_margin_px = px;
        try { m_prefs.save(); } catch (...) {}
        if (on_geometry_changed) on_geometry_changed();
    });

    m_margins_lock->signal_toggled().connect([this]() {
        m_prefs.editor_margins_linked = m_margins_lock->get_active();
        m_margins_lock->set_label(m_prefs.editor_margins_linked ? "🔗" : "🔓");
        if (m_right_margin_spin)
            m_right_margin_spin->set_sensitive(!m_prefs.editor_margins_linked);
        if (m_prefs.editor_margins_linked) {
            // Sync right to left immediately
            m_prefs.editor_right_margin_px = m_prefs.editor_left_margin_px;
            m_loading = true;
            if (m_right_margin_spin)
                m_right_margin_spin->set_value(
                    margin_to_unit(m_prefs.editor_left_margin_px));
            m_loading = false;
            if (on_geometry_changed) on_geometry_changed();
        }
        try { m_prefs.save(); } catch (...) {}
    });

    m_indent_spin->signal_value_changed().connect([this]() {
        if (m_loading) return;
        m_prefs.first_line_indent_px = unit_to_px(m_indent_spin->get_value());
        try { m_prefs.save(); } catch (...) {}
        if (on_indent_changed) on_indent_changed();
    });

    m_para_spacing_spin->signal_value_changed().connect([this]() {
        if (m_loading) return;
        m_prefs.paragraph_spacing_px = (int)m_para_spacing_spin->get_value();
        try { m_prefs.save(); } catch (...) {}
        if (on_spacing_changed) on_spacing_changed();
    });

    rebuild_tab_list();
}

// ─────────────────────────────────────────────────────────────────────────────
// refresh — update all spin values from prefs (call when external changes occur)
// ─────────────────────────────────────────────────────────────────────────────

void RulerManagerDialog::refresh() {
    m_loading = true;
    update_unit_labels();

    if (m_page_width_spin)
        m_page_width_spin->set_value(m_prefs.editor_page_width_pct);

    double max_margin = RulerUnits::px_to_unit(400.0, current_unit());
    if (m_left_margin_spin) {
        m_left_margin_spin->set_adjustment(
            Gtk::Adjustment::create(margin_to_unit(m_prefs.editor_left_margin_px),
                                    0.0, max_margin, 0.1, 1.0));
        m_left_margin_spin->set_value(margin_to_unit(m_prefs.editor_left_margin_px));
    }
    if (m_right_margin_spin) {
        m_right_margin_spin->set_adjustment(
            Gtk::Adjustment::create(margin_to_unit(m_prefs.editor_right_margin_px),
                                    0.0, max_margin, 0.1, 1.0));
        m_right_margin_spin->set_value(margin_to_unit(m_prefs.editor_right_margin_px));
    }
    if (m_margins_lock) {
        m_margins_lock->set_active(m_prefs.editor_margins_linked);
        m_margins_lock->set_label(m_prefs.editor_margins_linked ? "🔗" : "🔓");
    }
    if (m_right_margin_spin)
        m_right_margin_spin->set_sensitive(!m_prefs.editor_margins_linked);

    double max_indent = RulerUnits::px_to_unit(300.0, current_unit());
    if (m_indent_spin) {
        m_indent_spin->set_adjustment(
            Gtk::Adjustment::create(indent_to_unit(), 0.0, max_indent, 0.1, 1.0));
        m_indent_spin->set_value(indent_to_unit());
        m_indent_spin->set_sensitive(m_prefs.first_line_indent);
    }
    if (m_para_spacing_spin)
        m_para_spacing_spin->set_value(m_prefs.paragraph_spacing_px);

    m_loading = false;
    rebuild_tab_list();
}

// ─────────────────────────────────────────────────────────────────────────────
// update_unit_labels
// ─────────────────────────────────────────────────────────────────────────────

void RulerManagerDialog::update_unit_labels() {
    std::string lbl = RulerUnits::display_label(current_unit());
    if (m_left_margin_unit)  m_left_margin_unit->set_text(lbl);
    if (m_right_margin_unit) m_right_margin_unit->set_text(lbl);
    if (m_indent_unit)       m_indent_unit->set_text(lbl);
}

// ─────────────────────────────────────────────────────────────────────────────
// rebuild_tab_list
// ─────────────────────────────────────────────────────────────────────────────

void RulerManagerDialog::rebuild_tab_list() {
    if (!m_tab_list) return;
    while (auto* child = m_tab_list->get_row_at_index(0))
        m_tab_list->remove(*child);

    if (m_prefs.tab_stops.empty()) {
        auto* lbl = Gtk::make_managed<Gtk::Label>("No tab stops — click ruler to add");
        lbl->add_css_class("dim-label");
        lbl->set_margin_top(8); lbl->set_margin_bottom(8);
        m_tab_list->append(*lbl);
        return;
    }

    RulerUnit u = current_unit();
    std::string unit_lbl = RulerUnits::display_label(u);

    for (int i = 0; i < (int)m_prefs.tab_stops.size(); ++i) {
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        row->set_margin_top(4); row->set_margin_bottom(4);
        row->set_margin_start(8); row->set_margin_end(8);

        double pt  = m_prefs.tab_stops[i].position_pt;
        double val = RulerUnits::px_to_unit(RulerUnits::pt_to_px(pt), u);
        double max_val = RulerUnits::px_to_unit(RulerUnits::pt_to_px(1440.0), u);

        auto* spin = Gtk::make_managed<Gtk::SpinButton>();
        spin->set_adjustment(Gtk::Adjustment::create(val, 0.0, max_val, 0.1, 1.0));
        spin->set_digits(2); spin->set_numeric(true);
        spin->set_width_chars(5); spin->set_hexpand(true);

        auto* unit_l = Gtk::make_managed<Gtk::Label>(unit_lbl);
        unit_l->add_css_class("stat-label");

        auto type_items = Gtk::StringList::create({"L", "R", "C", "D"});
        auto* type_dd = Gtk::make_managed<Gtk::DropDown>(type_items);
        type_dd->set_tooltip_text("Left / Right / Center / Decimal");
        const std::string& ct = m_prefs.tab_stops[i].type;
        guint tsel = (ct=="right") ? 1 : (ct=="center") ? 2 : (ct=="decimal") ? 3 : 0;
        type_dd->set_selected(tsel);

        auto* del = Gtk::make_managed<Gtk::Button>();
        del->set_icon_name("list-remove-symbolic");
        del->set_tooltip_text("Remove this ruler");
        del->add_css_class("flat");

        spin->signal_value_changed().connect([this, spin, i]() {
            if (i >= (int)m_prefs.tab_stops.size()) return;
            double px = RulerUnits::unit_to_px(spin->get_value(), current_unit());
            m_prefs.tab_stops[i].position_pt = RulerUnits::px_to_pt(px);
            std::sort(m_prefs.tab_stops.begin(), m_prefs.tab_stops.end(),
                      [](const Folio::TabStop& a, const Folio::TabStop& b) {
                          return a.position_pt < b.position_pt; });
            if (on_tab_stops_changed) on_tab_stops_changed();
            try { m_prefs.save(); } catch (...) {}
        });
        type_dd->property_selected().signal_changed().connect([this, type_dd, i]() {
            if (i >= (int)m_prefs.tab_stops.size()) return;
            static const char* types[] = {"left","right","center","decimal"};
            guint sel = type_dd->get_selected();
            if (sel < 4) m_prefs.tab_stops[i].type = types[sel];
            if (on_tab_stops_changed) on_tab_stops_changed();
            try { m_prefs.save(); } catch (...) {}
        });
        del->signal_clicked().connect([this, i]() {
            if (i < (int)m_prefs.tab_stops.size()) {
                m_prefs.tab_stops.erase(m_prefs.tab_stops.begin() + i);
                rebuild_tab_list();
                if (on_tab_stops_changed) on_tab_stops_changed();
                try { m_prefs.save(); } catch (...) {}
            }
        });

        row->append(*spin);
        row->append(*unit_l);
        row->append(*type_dd);
        row->append(*del);
        m_tab_list->append(*row);
    }
}

void RulerManagerDialog::update_margin_link() {
    // Intentionally empty — logic is inline in the lock signal handler
}

} // namespace Folio
