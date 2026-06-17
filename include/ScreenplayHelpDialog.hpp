#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — ScreenplayHelpDialog.hpp
// Floating (non-modal) reference panel for screenplay formatting rules.
// Single scrollable column: rules on top, collapsible example page below.
// ─────────────────────────────────────────────────────────────────────────────
#include <gtkmm.h>

namespace Folio {

class ScreenplayHelpDialog : public Gtk::Window {
public:
    explicit ScreenplayHelpDialog(Gtk::Window& parent);

private:
    void build_rules_panel(Gtk::Box& container);
    void build_example_panel(Gtk::Box& container);

    Gtk::Box m_root{Gtk::Orientation::VERTICAL};
};

} // namespace Folio
