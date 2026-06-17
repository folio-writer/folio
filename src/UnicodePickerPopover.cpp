// ─────────────────────────────────────────────────────────────────────────────
// Folio — UnicodePickerPopover.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "UnicodePickerPopover.hpp"
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/flowbox.h>
#include <gtkmm/separator.h>
#include <glibmm/ustring.h>
#include <cstdio>

namespace Folio {

// ─────────────────────────────────────────────────────────────────────────────
// Character data
// ─────────────────────────────────────────────────────────────────────────────

std::vector<UnicodePickerPopover::Group> UnicodePickerPopover::make_groups() {
    return {
        {
            "Typographic",
            {
                "\u2014", "\u2013", "\u2026", "\u00A0",
                "\u201C", "\u201D", "\u2018", "\u2019",
                "\u00AB", "\u00BB", "\u2039", "\u203A",
                "\u2022", "\u00B7", "\u00B6", "\u00A7",
                "\u2020", "\u2021", "\u2030",
                "\u2032", "\u2033",
            }
        },
        {
            "Accents — lowercase",
            {
                "\u00E0","\u00E1","\u00E2","\u00E3","\u00E4","\u00E5","\u00E6",
                "\u00E7",
                "\u00E8","\u00E9","\u00EA","\u00EB",
                "\u00EC","\u00ED","\u00EE","\u00EF",
                "\u00F0","\u00F1",
                "\u00F2","\u00F3","\u00F4","\u00F5","\u00F6","\u00F8",
                "\u00F9","\u00FA","\u00FB","\u00FC",
                "\u00FD","\u00FE","\u00FF",
                "\u0101","\u0113","\u012B","\u014D","\u016B",
                "\u0103","\u0115","\u012D","\u014F","\u016D",
                "\u015F","\u0163","\u017E","\u0161","\u010D",
            }
        },
        {
            "Accents — uppercase",
            {
                "\u00C0","\u00C1","\u00C2","\u00C3","\u00C4","\u00C5","\u00C6",
                "\u00C7",
                "\u00C8","\u00C9","\u00CA","\u00CB",
                "\u00CC","\u00CD","\u00CE","\u00CF",
                "\u00D0","\u00D1",
                "\u00D2","\u00D3","\u00D4","\u00D5","\u00D6","\u00D8",
                "\u00D9","\u00DA","\u00DB","\u00DC",
                "\u00DD","\u00DE",
                "\u0100","\u0112","\u012A","\u014C","\u016A",
                "\u0160","\u010C","\u017D",
            }
        },
        {
            "Symbols",
            {
                "\u00A9","\u00AE","\u2122",
                "\u00B0","\u00B1","\u00D7","\u00F7",
                "\u00BD","\u00BC","\u00BE",
                "\u00B9","\u00B2","\u00B3",
                "\u00B5","\u00BF","\u00A1",
                "\u00A3","\u20AC","\u00A5","\u00A2","\u00A4",
                "\u221E","\u2260","\u2248","\u2264","\u2265",
                "\u203C","\u2049",
                "\u2605","\u2606","\u2713","\u2717","\u2746",
                "\u2660","\u2663","\u2665","\u2666",
            }
        },
        {
            "Arrows",
            {
                "\u2192","\u2190","\u2191","\u2193",
                "\u2194","\u2195",
                "\u21D2","\u21D0","\u21D4",
                "\u21D1","\u21D3",
                "\u279C","\u27A1","\u27A4",
                "\u21A9","\u21AA",
                "\u2197","\u2196","\u2198","\u2199",
                "\u27F6","\u27F5","\u27F7",
            }
        },
        {
            "Math & Science",
            {
                "\u2211","\u220F","\u221A","\u2202","\u222B",
                "\u0394","\u2207",
                "\u2208","\u2209","\u2229","\u222A",
                "\u2282","\u2283","\u2286","\u2287",
                "\u2200","\u2203","\u2205",
                "\u2115","\u2124","\u211A","\u211D","\u2102",
                "\u03B1","\u03B2","\u03B3","\u03B4","\u03B5",
                "\u03BB","\u03BC","\u03C0","\u03C3","\u03C4","\u03C6","\u03C8","\u03C9",
                "\u0393","\u039B","\u03A0","\u03A3","\u03A6","\u03A8","\u03A9",
            }
        },
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructors
// ─────────────────────────────────────────────────────────────────────────────

UnicodePickerPopover::UnicodePickerPopover(Gtk::Entry* target)
    : m_entry(target) {
    set_has_arrow(true);
    add_css_class("unicode-picker");
    build();
}

UnicodePickerPopover::UnicodePickerPopover(Gtk::TextView* target)
    : m_textview(target) {
    set_has_arrow(true);
    add_css_class("unicode-picker");
    build();
}

// ─────────────────────────────────────────────────────────────────────────────
// build
// ─────────────────────────────────────────────────────────────────────────────

void UnicodePickerPopover::build() {
    auto* scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    scroll->set_min_content_height(260);
    scroll->set_max_content_height(560);
    scroll->set_min_content_width(500);

    auto* outer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    outer->set_margin_top(8);
    outer->set_margin_bottom(8);
    outer->set_margin_start(8);
    outer->set_margin_end(8);
    scroll->set_child(*outer);

    bool first = true;
    for (const auto& group : make_groups()) {
        if (!first) {
            auto* sep = Gtk::make_managed<Gtk::Separator>(
                Gtk::Orientation::HORIZONTAL);
            sep->set_margin_top(6);
            sep->set_margin_bottom(2);
            outer->append(*sep);
        }
        first = false;

        auto* lbl = Gtk::make_managed<Gtk::Label>(group.label);
        lbl->add_css_class("unicode-picker-section");
        lbl->set_halign(Gtk::Align::START);
        lbl->set_margin_top(4);
        lbl->set_margin_bottom(4);
        outer->append(*lbl);

        auto* flow = Gtk::make_managed<Gtk::FlowBox>();
        flow->set_selection_mode(Gtk::SelectionMode::NONE);
        flow->set_max_children_per_line(12);
        flow->set_min_children_per_line(6);
        flow->set_row_spacing(2);
        flow->set_column_spacing(2);
        flow->set_homogeneous(true);

        for (const auto& ch : group.chars) {
            auto* btn = Gtk::make_managed<Gtk::Button>(ch);
            btn->add_css_class("unicode-char-btn");
            btn->set_has_frame(false);
            btn->set_size_request(40, 40);

            Glib::ustring us(ch);
            if (!us.empty()) {
                char tip[32];
                std::snprintf(tip, sizeof(tip), "U+%04X", (unsigned)us[0]);
                btn->set_tooltip_text(tip);
            }

            btn->signal_clicked().connect([this, ch]() {
                insert_char(ch);
                popdown();
            });
            flow->append(*btn);
        }
        outer->append(*flow);
    }

    set_child(*scroll);
}

// ─────────────────────────────────────────────────────────────────────────────
// insert_char — append to Entry or insert at cursor in TextView
// ─────────────────────────────────────────────────────────────────────────────

void UnicodePickerPopover::insert_char(const std::string& utf8_char) {
    if (m_entry) {
        Glib::ustring current = m_entry->get_text();
        current += Glib::ustring(utf8_char);
        m_entry->set_text(current);
        m_entry->set_position(-1);
    } else if (m_textview) {
        auto buf = m_textview->get_buffer();
        if (buf) buf->insert_at_cursor(utf8_char);
        m_textview->grab_focus();
    }
}

} // namespace Folio
