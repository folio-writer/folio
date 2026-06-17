// ─────────────────────────────────────────────────────────────────────────────
// Folio — UnicodePickerPopover.hpp
// A reusable popover displaying a curated grid of Unicode characters in
// labelled sections.  Clicking a character inserts it into either a
// Gtk::Entry (appends) or a Gtk::TextView (inserts at cursor).
//
// Entry usage:
//   auto* picker = Gtk::make_managed<UnicodePickerPopover>(my_entry);
//   picker->set_parent(my_entry);
//   picker->popup();
//
// TextView usage:
//   auto* picker = Gtk::make_managed<UnicodePickerPopover>(my_text_view);
//   picker->set_parent(my_text_view);
//   picker->popup();
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include <gtkmm/popover.h>
#include <gtkmm/entry.h>
#include <gtkmm/textview.h>
#include <string>
#include <vector>

namespace Folio {

class UnicodePickerPopover : public Gtk::Popover {
public:
    // Construct with an Entry target — character is appended to the entry text.
    explicit UnicodePickerPopover(Gtk::Entry* target);

    // Construct with a TextView target — character is inserted at the cursor.
    explicit UnicodePickerPopover(Gtk::TextView* target);

    // Default — no target; call set_target() before popup().
    UnicodePickerPopover() { set_has_arrow(true); add_css_class("unicode-picker"); build(); }

    // Retarget to a different Entry without rebuilding the widget tree.
    void set_target(Gtk::Entry*    target) { m_entry = target; m_textview = nullptr; }
    void set_target(Gtk::TextView* target) { m_textview = target; m_entry = nullptr; }

private:
    void build();
    void insert_char(const std::string& utf8_char);

    Gtk::Entry*    m_entry    = nullptr;
    Gtk::TextView* m_textview = nullptr;

    struct Group {
        std::string              label;
        std::vector<std::string> chars;
    };
    static std::vector<Group> make_groups();
};

} // namespace Folio
