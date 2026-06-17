#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — BarcodeDialog.hpp
//
// Barcode preview and settings dialog for the project ISBN.
// Generates EAN-13 SVG via BarcodeGenerator and saves it to the project.
// Provides Export SVG for use by designers.
// ─────────────────────────────────────────────────────────────────────────────
#include "BarcodeGenerator.hpp"
#include "DocumentModel.hpp"
#include <gtkmm.h>
#include <functional>
#include <string>

namespace Folio {

class BarcodeDialog : public Gtk::Window {
public:
    using SvgSavedCallback = std::function<void()>;

    BarcodeDialog(Gtk::Window& parent, DocumentModel& model);
    void set_svg_saved_callback(SvgSavedCallback cb) { m_on_svg_saved = std::move(cb); }

private:
    DocumentModel&   m_model;
    SvgSavedCallback m_on_svg_saved;

    std::string     m_current_svg;
    BarcodeOptions  m_opts;

    // ── Layout ────────────────────────────────────────────────────────────────
    Gtk::Box         m_root    { Gtk::Orientation::VERTICAL,   0 };
    Gtk::Box         m_content { Gtk::Orientation::HORIZONTAL, 0 };
    Gtk::Box         m_left    { Gtk::Orientation::VERTICAL,  16 };
    Gtk::Box         m_right   { Gtk::Orientation::VERTICAL,  16 };
    Gtk::Box         m_footer  { Gtk::Orientation::HORIZONTAL, 8 };

    // ── Preview ───────────────────────────────────────────────────────────────
    Gtk::Box         m_preview_frame { Gtk::Orientation::VERTICAL, 0 };
    Gtk::DrawingArea m_preview;
    Gtk::Label       m_isbn_label;

    // ── Settings ──────────────────────────────────────────────────────────────
    // Height
    Gtk::Box         m_height_box { Gtk::Orientation::HORIZONTAL, 12 };
    Gtk::CheckButton m_radio_full;
    Gtk::CheckButton m_radio_half;

    // Price supplement
    Gtk::CheckButton m_sw_price;
    Gtk::Box         m_price_row  { Gtk::Orientation::HORIZONTAL, 8 };
    Gtk::DropDown*   m_currency_dd = nullptr;
    Gtk::Entry       m_price_entry;

    // Quiet zone marker
    Gtk::CheckButton m_sw_whitespace;

    // Returnability — radio group
    Gtk::Box         m_return_box  { Gtk::Orientation::VERTICAL, 4 };
    Gtk::CheckButton m_radio_no_triangle;
    Gtk::CheckButton m_radio_non_returnable;
    Gtk::CheckButton m_radio_strippable;

    // ── Footer ────────────────────────────────────────────────────────────────
    Gtk::Button      m_btn_export;
    Gtk::Button      m_btn_close;
    std::string      m_last_export_folder;

    // ── Helpers ───────────────────────────────────────────────────────────────
    void build_css();
    void build_ui();
    void regenerate();
    void update_preview();
    void sync_opts_from_widgets();
    void on_export_svg();
    std::string isbn_display_string() const;
};

} // namespace Folio
