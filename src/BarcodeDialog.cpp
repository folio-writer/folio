// ─────────────────────────────────────────────────────────────────────────────
// Folio — BarcodeDialog.cpp
// ─────────────────────────────────────────────────────────────────────────────
#include "BarcodeDialog.hpp"
#include "FolioLog.hpp"
#include <librsvg/rsvg.h>
#include <giomm.h>
#include <fstream>

namespace Folio {

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────
BarcodeDialog::BarcodeDialog(Gtk::Window& parent, DocumentModel& model)
    : Gtk::Window(), m_model(model)
{
    set_transient_for(parent);
    set_modal(true);
    set_title("ISBN Barcode");
    set_default_size(620, 360);
    set_resizable(false);

    auto kc = Gtk::EventControllerKey::create();
    kc->signal_key_pressed().connect([this](guint kv, guint, Gdk::ModifierType) -> bool {
        if (kv == GDK_KEY_Escape) { close(); return true; }
        return false;
    }, false);
    add_controller(kc);

    build_css();
    build_ui();

    // Restore price from model
    if (!m_model.barcode_price.empty()) {
        m_price_entry.set_text(m_model.barcode_price);
        m_sw_price.set_active(true);
        m_price_row.set_sensitive(true);
    }
    // Restore currency
    static const char* codes[] = {"5","6","4","3","0","9"};
    for (guint i = 0; i < 6; ++i)
        if (m_currency_dd && m_model.barcode_currency == codes[i])
            m_currency_dd->set_selected(i);

    regenerate();
}

// ─────────────────────────────────────────────────────────────────────────────
// CSS
// ─────────────────────────────────────────────────────────────────────────────
void BarcodeDialog::build_css() {
    auto css = Gtk::CssProvider::create();
    css->load_from_data(R"(
        .barcode-content       { padding: 20px; }

        .barcode-isbn-label {
            font-size: 11px;
            color: alpha(currentColor, 0.55);
            margin-top: 8px;
        }
        .barcode-section-title {
            font-size: 11px;
            font-weight: 700;
            letter-spacing: 0.06em;
            color: alpha(currentColor, 0.5);
            text-transform: uppercase;
        }
        .barcode-footer {
            padding: 10px 16px;
            border-top: 1px solid alpha(currentColor, 0.10);
        }
        .barcode-price-row { margin-left: 20px; }
        .barcode-return-row { margin-left: 0px; }
    )");
    get_style_context()->add_provider_for_display(
        get_display(), css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// UI construction
// ─────────────────────────────────────────────────────────────────────────────
void BarcodeDialog::build_ui() {
    set_child(m_root);
    m_root.append(m_content);
    m_root.append(m_footer);

    m_content.add_css_class("barcode-content");
    m_content.set_spacing(20);
    m_content.set_vexpand(true);
    m_content.append(m_left);
    m_content.append(m_right);

    // ── Left: preview ─────────────────────────────────────────────────────────
    m_left.set_valign(Gtk::Align::CENTER);
    m_preview.set_size_request(260, 160);
    m_preview.set_draw_func([this](const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
        // Dark background
        cr->set_source_rgb(0.15, 0.15, 0.18);
        cr->paint();

        if (m_current_svg.empty()) return;

        GError* err = nullptr;
        RsvgHandle* rsvg = rsvg_handle_new_from_data(
            (const guint8*)m_current_svg.data(), (gsize)m_current_svg.size(), &err);
        if (!rsvg) { if (err) g_error_free(err); return; }

        // Set 72 DPI so SVG pt units map 1:1 to pixels in the offscreen buffer
        rsvg_handle_set_dpi(rsvg, 72.0);

        // Get SVG natural size in pt
        gdouble svg_w = 0.0, svg_h = 0.0;
        rsvg_handle_get_intrinsic_size_in_pixels(rsvg, &svg_w, &svg_h);
        if (svg_w <= 0.0 || svg_h <= 0.0) {
            RsvgRectangle ink_r = {};
            rsvg_handle_get_geometry_for_element(rsvg, nullptr, &ink_r, nullptr, &err);
            if (err) { g_error_free(err); err = nullptr; }
            svg_w = ink_r.width  > 0 ? ink_r.width  : (double)w;
            svg_h = ink_r.height > 0 ? ink_r.height : (double)h;
        }

        // Compute uniform scale to fit widget with padding
        const double pad = 14.0;
        double scale = std::min((w - pad*2.0) / svg_w,
                                (h - pad*2.0) / svg_h);
        int buf_w = (int)std::ceil(svg_w * scale);
        int buf_h = (int)std::ceil(svg_h * scale);

        // ── Offscreen render at 1:1 (macOS-style: render to known state, then blit)
        // The SVG is rendered into a fresh image surface with no device scale,
        // so librsvg sees exactly the coordinate space we intend.
        cairo_surface_t* offscreen = cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32, buf_w, buf_h);
        cairo_t* ocr = cairo_create(offscreen);

        // White background for barcode
        cairo_set_source_rgb(ocr, 1.0, 1.0, 1.0);
        cairo_paint(ocr);

        // Render SVG into offscreen buffer at computed size — no device scale
        RsvgRectangle vp = {0.0, 0.0, (double)buf_w, (double)buf_h};
        rsvg_handle_render_document(rsvg, ocr, &vp, &err);
        if (err) { g_error_free(err); err = nullptr; }
        cairo_destroy(ocr);
        cairo_surface_flush(offscreen);

        // ── Blit offscreen buffer onto widget, centred
        double ox = (w - buf_w) / 2.0;
        double oy = (h - buf_h) / 2.0;
        cairo_set_source_surface(cr->cobj(), offscreen, ox, oy);
        cairo_paint(cr->cobj());

        cairo_surface_destroy(offscreen);
        g_object_unref(rsvg);
    });

    m_isbn_label.add_css_class("barcode-isbn-label");
    m_isbn_label.set_halign(Gtk::Align::CENTER);
    m_preview.set_halign(Gtk::Align::CENTER);
    m_left.append(m_preview);
    m_left.append(m_isbn_label);

    // ── Right: settings ───────────────────────────────────────────────────────
    m_right.set_valign(Gtk::Align::CENTER);
    m_right.set_hexpand(true);

    auto* title = Gtk::make_managed<Gtk::Label>("Settings");
    title->add_css_class("barcode-section-title");
    title->set_halign(Gtk::Align::START);
    m_right.append(*title);
    m_right.append(*Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL));

    // Height
    auto* hl = Gtk::make_managed<Gtk::Label>("Bar height:");
    hl->set_halign(Gtk::Align::START);
    hl->set_hexpand(true);
    m_radio_full.set_label("Full (72pt)");
    m_radio_full.set_active(true);
    m_radio_half.set_label("Half (36pt)");
    m_radio_half.set_group(m_radio_full);
    m_height_box.append(*hl);
    m_height_box.append(m_radio_full);
    m_height_box.append(m_radio_half);
    m_right.append(m_height_box);
    m_radio_full.signal_toggled().connect([this]() { if (m_radio_full.get_active()) regenerate(); });
    m_radio_half.signal_toggled().connect([this]() { if (m_radio_half.get_active()) regenerate(); });

    m_right.append(*Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL));

    // Price supplement
    m_sw_price.set_label("Include price supplement (EAN-5)");
    m_sw_price.set_halign(Gtk::Align::START);
    m_right.append(m_sw_price);

    m_price_row.add_css_class("barcode-price-row");
    auto currency_model = Gtk::StringList::create({
        "United States (5)", "Canada (6)", "New Zealand (4)",
        "Australia (3)", "British Pound (0)", "NACS (9)"});
    m_currency_dd = Gtk::make_managed<Gtk::DropDown>(currency_model);
    m_currency_dd->set_selected(0);
    m_price_entry.set_placeholder_text("0000");
    m_price_entry.set_max_length(4);
    m_price_entry.set_width_chars(6);
    auto* pl = Gtk::make_managed<Gtk::Label>("Price:");
    m_price_row.append(*pl);
    m_price_row.append(*m_currency_dd);
    m_price_row.append(m_price_entry);
    m_price_row.set_sensitive(false);
    m_right.append(m_price_row);

    m_sw_price.signal_toggled().connect([this]() {
        m_price_row.set_sensitive(m_sw_price.get_active());
        regenerate();
    });
    m_currency_dd->property_selected().signal_changed().connect([this]() { regenerate(); });
    m_price_entry.signal_changed().connect([this]() { regenerate(); });

    m_right.append(*Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL));

    // Quiet zone marker
    m_sw_whitespace.set_label("Show quiet zone marker (>) after last element");
    m_sw_whitespace.set_halign(Gtk::Align::START);
    m_right.append(m_sw_whitespace);
    m_sw_whitespace.signal_toggled().connect([this]() { regenerate(); });

    m_right.append(*Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL));

    // Returnability
    auto* rl = Gtk::make_managed<Gtk::Label>("Returnability:");
    rl->set_halign(Gtk::Align::START);
    m_right.append(*rl);

    m_radio_no_triangle.set_label("None");
    m_radio_no_triangle.set_active(true);
    m_radio_non_returnable.set_label("Non-returnable  △");
    m_radio_non_returnable.set_group(m_radio_no_triangle);
    m_radio_strippable.set_label("Strippable  △S  (cover removable for reimbursement)");
    m_radio_strippable.set_group(m_radio_no_triangle);

    m_return_box.append(m_radio_no_triangle);
    m_return_box.append(m_radio_non_returnable);
    m_return_box.append(m_radio_strippable);
    m_right.append(m_return_box);

    m_radio_no_triangle.signal_toggled().connect([this]()    { if (m_radio_no_triangle.get_active())    regenerate(); });
    m_radio_non_returnable.signal_toggled().connect([this]() { if (m_radio_non_returnable.get_active()) regenerate(); });
    m_radio_strippable.signal_toggled().connect([this]()     { if (m_radio_strippable.get_active())     regenerate(); });

    // ── Footer ────────────────────────────────────────────────────────────────
    m_footer.add_css_class("barcode-footer");
    m_btn_export.set_label("Export SVG…");
    m_btn_export.set_halign(Gtk::Align::START);
    m_btn_export.signal_clicked().connect([this]() { on_export_svg(); });
    m_btn_close.set_label("Close");
    m_btn_close.set_halign(Gtk::Align::END);
    m_btn_close.set_hexpand(true);
    m_btn_close.signal_clicked().connect([this]() { close(); });
    m_footer.append(m_btn_export);
    m_footer.append(m_btn_close);
}

// ─────────────────────────────────────────────────────────────────────────────
// Regenerate
// ─────────────────────────────────────────────────────────────────────────────
void BarcodeDialog::sync_opts_from_widgets() {
    m_opts.full_height     = m_radio_full.get_active();
    m_opts.show_whitespace = m_sw_whitespace.get_active();
    m_opts.include_price   = m_sw_price.get_active();
    m_opts.price           = std::string(m_price_entry.get_text());
    while (m_opts.price.size() < 4) m_opts.price += '0';
    m_opts.price = m_opts.price.substr(0, 4);

    static const char* codes[] = {"5","6","4","3","0","9"};
    guint idx = m_currency_dd ? m_currency_dd->get_selected() : 0;
    m_opts.currency = (idx < 6) ? codes[idx] : "5";

    if (m_radio_strippable.get_active())     m_opts.triangle_state = 2;
    else if (m_radio_non_returnable.get_active()) m_opts.triangle_state = 1;
    else                                          m_opts.triangle_state = 0;

    m_model.barcode_price    = m_opts.include_price ? m_opts.price : "";
    m_model.barcode_currency = m_opts.currency;
}

void BarcodeDialog::regenerate() {
    sync_opts_from_widgets();
    m_current_svg = BarcodeGenerator::generate_svg_from_isbn(m_model.isbn, m_opts);
    m_model.barcode_svg = m_current_svg;
    m_model.mark_modified();
    if (m_on_svg_saved) m_on_svg_saved();
    m_isbn_label.set_text(isbn_display_string());
    update_preview();
}

void BarcodeDialog::update_preview() {
    m_preview.queue_draw();
    m_btn_export.set_sensitive(!m_current_svg.empty());
}

std::string BarcodeDialog::isbn_display_string() const {
    if (m_model.isbn.empty()) return "No ISBN set";
    std::string d = BarcodeGenerator::normalise(m_model.isbn);
    if (d.size()==13) return "ISBN-13: " + m_model.isbn;
    if (d.size()==10) return "ISBN-10: " + m_model.isbn
                            + "  →  EAN-13: " + BarcodeGenerator::isbn10_to_ean13(d);
    return "ISBN: " + m_model.isbn;
}

// ─────────────────────────────────────────────────────────────────────────────
// Export SVG
// ─────────────────────────────────────────────────────────────────────────────
void BarcodeDialog::on_export_svg() {
    if (m_current_svg.empty()) return;
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title("Export Barcode SVG");
    std::string base = BarcodeGenerator::normalise(m_model.isbn);
    dialog->set_initial_name((base.empty() ? "barcode" : base) + ".svg");
    if (!m_last_export_folder.empty()) {
        auto folder = Gio::File::create_for_path(m_last_export_folder);
        dialog->set_initial_folder(folder);
    }
    auto filter = Gtk::FileFilter::create();
    filter->set_name("SVG files");
    filter->add_pattern("*.svg");
    auto filters = Gio::ListStore<Gtk::FileFilter>::create();
    filters->append(filter);
    dialog->set_filters(filters);
    dialog->save(*this, [this, dialog](Glib::RefPtr<Gio::AsyncResult>& result) {
        try {
            auto file = dialog->save_finish(result);
            if (!file) return;
            std::ofstream out(file->get_path(), std::ios::binary);
            out << m_current_svg;
            // Remember folder for next export
            m_last_export_folder = Gio::File::create_for_path(file->get_path())
                ->get_parent()->get_path();
            LOG_DEBUG("BarcodeDialog: exported SVG to '{}'", file->get_path());
        } catch (...) {
            LOG_DEBUG("BarcodeDialog: export cancelled or failed");
        }
    });
}

} // namespace Folio
