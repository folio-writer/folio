// ─────────────────────────────────────────────────────────────────────────────
// Folio — ObjectForm.cpp   (s31)   GTK/gtkmm4. See ObjectForm.hpp.
//
// Thin renderer over the pure Folio::FormPlan. Read-only this slice (the editable flip
// is the next step); the text-edit path is wired behind `editable` so turning it
// on later does not change this file's shape. Mirrors the verified Inspector row
// idiom: HBox(label hexpand START, value END) inside a pref-listbox, with full-
// width fields dropped in as their own card.
// ─────────────────────────────────────────────────────────────────────────────
#include "ObjectForm.hpp"

#include <gtkmm.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <memory>
#include <set>
#include <utility>
#include <vector>

namespace {

// The shared compact label/value scaffold (pref-listbox > row > hbox), returning
// the inner HBox so an editable widget appends its value control at the END —
// identical structure to ObjectForm::append_compact_row, so an editable row sits
// flush with the read-only rows around it.
Gtk::Box& compact_scaffold(Gtk::Box& body, const Glib::ustring& label) {
    auto* lb = Gtk::make_managed<Gtk::ListBox>();
    lb->set_selection_mode(Gtk::SelectionMode::NONE);
    lb->add_css_class("pref-listbox");

    auto* lbr = Gtk::make_managed<Gtk::ListBoxRow>();
    auto* rb  = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    rb->set_margin_start(12);
    rb->set_margin_end(12);
    rb->set_margin_top(3);
    rb->set_margin_bottom(3);

    auto* l = Gtk::make_managed<Gtk::Label>(label);
    l->add_css_class("pref-row-label");
    l->set_hexpand(true);
    l->set_halign(Gtk::Align::START);
    rb->append(*l);

    lbr->set_child(*rb);
    lb->append(*lbr);
    body.append(*lb);
    return *rb;
}

// s37 — (re)build the editable rows of a List value card: one [entry][trash] row
// per string in `state`, in order. A named free function so the trash handler can
// recurse by name (no self-referential shared_ptr → no lifetime cycle); `state` is
// shared so every row's handlers see the same backing array, and `emit` reports
// the whole array on any edit. Entry edits mutate state in place WITHOUT a rebuild
// (so typing never destroys the focused entry); only add/trash rebuild. Destroying
// the trash button from its own click is the established safe pattern — GTK holds a
// ref through the emission, so the handler runs to completion first.
void rebuild_list_rows(Gtk::Box& rows_box,
                       const std::shared_ptr<std::vector<std::string>>& state,
                       const std::function<void()>& emit) {
    while (Gtk::Widget* c = rows_box.get_first_child()) rows_box.remove(*c);
    for (std::size_t i = 0; i < state->size(); ++i) {
        const std::size_t idx = i;
        auto* r = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        r->set_margin_start(8);
        r->set_margin_end(8);

        auto* e = Gtk::make_managed<Gtk::Entry>();
        e->set_text((*state)[i]);
        e->set_hexpand(true);
        e->signal_changed().connect([e, state, idx, emit]() {
            if (idx < state->size()) { (*state)[idx] = std::string(e->get_text()); emit(); }
        });

        auto* del = Gtk::make_managed<Gtk::Button>();
        del->set_icon_name("user-trash-symbolic");
        del->add_css_class("flat");
        del->set_valign(Gtk::Align::CENTER);
        del->signal_clicked().connect([&rows_box, state, idx, emit]() {
            if (idx < state->size()) {
                state->erase(state->begin() + static_cast<std::ptrdiff_t>(idx));
                rebuild_list_rows(rows_box, state, emit);   // recurse by name
                emit();
            }
        });

        r->append(*e);
        r->append(*del);
        rows_box.append(*r);
    }
}

}  // namespace

namespace Folio {

ObjectForm::ObjectForm()
    : Gtk::Box(Gtk::Orientation::VERTICAL, 0),
      m_body(Gtk::Orientation::VERTICAL, 0) {
    set_name("object-form");
    m_heading.add_css_class("inspector-section-label");
    m_heading.set_halign(Gtk::Align::START);
    m_heading.set_margin_start(12);
    m_heading.set_margin_top(3);
    m_heading.set_margin_bottom(4);
    append(m_heading);
    append(m_body);
}

void ObjectForm::clear_body() {
    while (Gtk::Widget* c = m_body.get_first_child())
        m_body.remove(*c);
}

void ObjectForm::clear() {
    m_heading.set_text("");
    clear_body();
}

// Compact label / value row — read-only display (a right-aligned value label).
void ObjectForm::append_compact_row(const Folio::FormRow& row) {
    auto* lb  = Gtk::make_managed<Gtk::ListBox>();
    lb->set_selection_mode(Gtk::SelectionMode::NONE);
    lb->add_css_class("pref-listbox");

    auto* lbr = Gtk::make_managed<Gtk::ListBoxRow>();
    auto* rb  = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    rb->set_margin_start(12);
    rb->set_margin_end(12);
    rb->set_margin_top(3);
    rb->set_margin_bottom(3);

    auto* l = Gtk::make_managed<Gtk::Label>(row.label);
    l->add_css_class("pref-row-label");
    l->set_hexpand(true);
    l->set_halign(Gtk::Align::START);

    const std::string text = field_display_string(row.type, row.value, row.config);
    auto* v = Gtk::make_managed<Gtk::Label>(text.empty() ? "—" : text);
    v->set_halign(Gtk::Align::END);
    v->set_ellipsize(Pango::EllipsizeMode::END);
    v->set_max_width_chars(28);
    if (text.empty() || row.read_only) v->add_css_class("dim-label");

    rb->append(*l);
    rb->append(*v);
    lbr->set_child(*rb);
    lb->append(*lbr);
    m_body.append(*lb);
}

// Full-width block — richtext (the dissertation floor) / list. Read-only: a
// non-editable TextView (richtext) or a wrapped label (list).
void ObjectForm::append_full_width(const Folio::FormRow& row) {
    auto* hdr = Gtk::make_managed<Gtk::Label>(row.label);
    hdr->add_css_class("inspector-section-label");
    hdr->set_halign(Gtk::Align::START);
    hdr->set_margin_start(12);
    hdr->set_margin_top(6);
    hdr->set_margin_bottom(2);
    m_body.append(*hdr);

    const std::string text = field_display_string(row.type, row.value, row.config);

    if (row.type == FieldType::RichText) {
        auto* tv = Gtk::make_managed<Gtk::TextView>();
        tv->set_editable(false);
        tv->set_cursor_visible(false);
        tv->set_wrap_mode(Gtk::WrapMode::WORD);
        tv->add_css_class("object-form-richtext");
        tv->get_buffer()->set_text(text);
        auto* frame = Gtk::make_managed<Gtk::Frame>();
        frame->set_margin_start(12);
        frame->set_margin_end(12);
        frame->set_margin_bottom(3);
        frame->set_child(*tv);
        m_body.append(*frame);
    } else {
        auto* card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
        card->add_css_class("pomo-tile-card");
        card->set_margin_start(12);
        card->set_margin_end(12);
        card->set_margin_bottom(3);
        auto* l = Gtk::make_managed<Gtk::Label>(text.empty() ? "—" : text);
        l->set_halign(Gtk::Align::START);
        l->set_wrap(true);
        l->set_margin_start(8);
        l->set_margin_end(8);
        l->set_margin_top(6);
        l->set_margin_bottom(6);
        if (text.empty()) l->add_css_class("dim-label");
        card->append(*l);
        m_body.append(*card);
    }
}

// Editable single-line entry (s32) — name (text) and the image path. Reports the
// raw string through on_change; the Inspector coerces + writes it through to the
// backing leaf. A real image picker is a later slice; the path entry is the floor.
void ObjectForm::append_editable_text(const Folio::FormRow& row, const OnChange& on_change) {
    auto* lb  = Gtk::make_managed<Gtk::ListBox>();
    lb->set_selection_mode(Gtk::SelectionMode::NONE);
    lb->add_css_class("pref-listbox");

    auto* lbr = Gtk::make_managed<Gtk::ListBoxRow>();
    auto* rb  = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    rb->set_margin_start(12);
    rb->set_margin_end(12);
    rb->set_margin_top(3);
    rb->set_margin_bottom(3);

    auto* l = Gtk::make_managed<Gtk::Label>(row.label);
    l->add_css_class("pref-row-label");
    l->set_hexpand(true);
    l->set_halign(Gtk::Align::START);

    auto* e = Gtk::make_managed<Gtk::Entry>();
    e->set_text(field_display_string(row.type, row.value));
    e->set_halign(Gtk::Align::END);
    e->set_size_request(140, -1);
    std::string field_id = row.field_id;
    e->signal_changed().connect([e, field_id, on_change]() {
        if (on_change) on_change(field_id, json(std::string(e->get_text())));
    });

    rb->append(*l);
    rb->append(*e);
    lbr->set_child(*rb);
    lb->append(*lbr);
    m_body.append(*lb);
}

// Editable Image (s44) — a bounded preview plus a "Set image…" / "Clear" control,
// replacing the bare path entry. The value stays a filesystem path (string), so it
// round-trips through the same coercion and the read-only renderer is unchanged.
// The picker mirrors the avatar strip's FileChooserNative idiom; the preview loads
// a size-bounded pixbuf. (Copying the chosen file into the bundle's assets/ for
// portability — the link-death guard, scrapbook §3 — is the follow; this stores
// the external path.)
void ObjectForm::append_editable_image(const Folio::FormRow& row, const OnChange& on_change) {
    const std::string field_id = row.field_id;
    const std::string cur_path = field_display_string(row.type, row.value);

    auto* lb  = Gtk::make_managed<Gtk::ListBox>();
    lb->set_selection_mode(Gtk::SelectionMode::NONE);
    lb->add_css_class("pref-listbox");
    auto* lbr = Gtk::make_managed<Gtk::ListBoxRow>();

    auto* col = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    col->set_margin_start(12);
    col->set_margin_end(12);
    col->set_margin_top(6);
    col->set_margin_bottom(6);

    auto* label = Gtk::make_managed<Gtk::Label>(row.label);
    label->add_css_class("pref-row-label");
    label->set_halign(Gtk::Align::START);
    col->append(*label);

    // Preview — a Picture that fills the panel width and preserves aspect, plus a
    // "sizer" slider to scale its height (so a small image can be made large enough
    // to read). Hidden when there is no path; an unloadable path shows an inline
    // notice instead of vanishing.
    auto* pic = Gtk::make_managed<Gtk::Picture>();
    pic->set_hexpand(true);
    pic->set_halign(Gtk::Align::FILL);     // fill width so the image centers in it
    pic->set_valign(Gtk::Align::FILL);
    pic->set_can_shrink(true);
    pic->set_content_fit(Gtk::ContentFit::SCALE_DOWN);  // never upscale → never fuzzy
    pic->add_css_class("object-image-preview");
    col->append(*pic);

    // Sizer: drag to scale the preview's height (48–720px). Seeded from the
    // remembered per-instance height (object values, reserved key) and persisted
    // back through on_change so it is restored next time this object opens. Only
    // shown when an image is loaded.
    const std::string size_key = image_preview_key(field_id);
    double init_h = 300.0;
    if (m_obj_values.is_object() && m_obj_values.contains(size_key)
        && m_obj_values[size_key].is_number())
        init_h = m_obj_values[size_key].get<double>();
    if (init_h < 48.0)  init_h = 48.0;
    if (init_h > 720.0) init_h = 720.0;
    auto size_adj = Gtk::Adjustment::create(init_h, 48.0, 720.0, 10.0, 60.0, 0.0);
    auto* sizer = Gtk::make_managed<Gtk::Scale>(size_adj, Gtk::Orientation::HORIZONTAL);
    sizer->set_draw_value(false);
    sizer->set_hexpand(true);
    sizer->set_tooltip_text("Scale the preview");
    col->append(*sizer);

    auto* err = Gtk::make_managed<Gtk::Label>("Image could not be loaded.");
    err->add_css_class("dim-label");
    err->set_halign(Gtk::Align::START);
    err->set_visible(false);
    col->append(*err);

    // The full-res source is loaded ONCE and cached. The sizer sets the WIDGET
    // height (H); the image is scaled DOWN to fit that height when larger, shown at
    // natural size when smaller (never upscaled → never fuzzy), and centered in the
    // widget. We pre-scale to ≤H so the source paintable's natural height never
    // exceeds H — that lets size_request(-1, H) pin the widget to exactly H with
    // the (possibly smaller) image centered inside it.
    auto orig = std::make_shared<Glib::RefPtr<Gdk::Pixbuf>>();

    auto render_at = [pic, orig](int H) {
        if (!*orig) return;
        const int oh = (*orig)->get_height();
        const int ow = (*orig)->get_width();
        if (oh <= 0 || ow <= 0) return;
        if (H < 1) H = 1;
        const int th = std::min(oh, H);                 // downscale only — never up
        const int tw = std::max(1, static_cast<int>(std::lround(
                          static_cast<double>(ow) * th / oh)));
        Glib::RefPtr<Gdk::Pixbuf> shown =
            (th == oh) ? *orig
                       : (*orig)->scale_simple(tw, th, Gdk::InterpType::BILINEAR);
        pic->set_paintable(Gdk::Texture::create_for_pixbuf(shown));
        pic->set_size_request(-1, H);                   // the WIDGET height = slider
    };
    auto apply_height = [size_adj, render_at]() {
        render_at(static_cast<int>(size_adj->get_value()));
    };
    // Slider drag: re-render at the new height AND remember it on the object so the
    // next open restores this size. The initial seed (above) and load_preview use
    // render_at directly, so they never write back.
    sizer->signal_value_changed().connect(
        [size_adj, render_at, on_change, size_key]() {
            const int H = static_cast<int>(size_adj->get_value());
            render_at(H);
            if (on_change) on_change(size_key, json(static_cast<double>(H)));
        });

    auto load_preview = [pic, sizer, err, orig, apply_height](const std::string& path) {
        if (path.empty()) {
            *orig = Glib::RefPtr<Gdk::Pixbuf>{};
            pic->set_visible(false); sizer->set_visible(false); err->set_visible(false);
            return;
        }
        try {
            *orig = Gdk::Pixbuf::create_from_file(path);
            pic->set_visible(true); sizer->set_visible(true); err->set_visible(false);
            apply_height();
        } catch (...) {
            *orig = Glib::RefPtr<Gdk::Pixbuf>{};
            pic->set_paintable(Glib::RefPtr<Gdk::Paintable>{});
            pic->set_visible(false); sizer->set_visible(false); err->set_visible(true);
        }
    };
    load_preview(cur_path);

    auto* btn_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    auto* set_btn = Gtk::make_managed<Gtk::Button>(
        cur_path.empty() ? "Set image…" : "Change image…");
    set_btn->add_css_class("flat");
    auto* clear_btn = Gtk::make_managed<Gtk::Button>("Clear");
    clear_btn->add_css_class("flat");
    clear_btn->set_visible(!cur_path.empty());
    btn_row->append(*set_btn);
    btn_row->append(*clear_btn);
    col->append(*btn_row);

    set_btn->signal_clicked().connect(
        [this, field_id, on_change, load_preview, set_btn, clear_btn]() {
            auto* win = dynamic_cast<Gtk::Window*>(get_root());
            if (!win) return;
            auto dlg = Gtk::FileChooserNative::create(
                "Choose Image", *win, Gtk::FileChooser::Action::OPEN, "Open", "Cancel");
            auto filter = Gtk::FileFilter::create();
            filter->set_name("Images (PNG, JPG, WebP)");
            filter->add_mime_type("image/png");
            filter->add_mime_type("image/jpeg");
            filter->add_mime_type("image/webp");
            dlg->add_filter(filter);
            dlg->signal_response().connect(
                [dlg, field_id, on_change, load_preview, set_btn, clear_btn](int response) {
                    if (response != Gtk::ResponseType::ACCEPT) return;
                    auto file = dlg->get_file();
                    if (!file) return;
                    const std::string path = file->get_path();
                    if (path.empty()) return;
                    if (on_change) on_change(field_id, json(path));
                    load_preview(path);
                    set_btn->set_label("Change image…");
                    clear_btn->set_visible(true);
                });
            dlg->show();
        });

    clear_btn->signal_clicked().connect(
        [field_id, on_change, load_preview, set_btn, clear_btn]() {
            if (on_change) on_change(field_id, json(std::string{}));
            load_preview("");
            set_btn->set_label("Set image…");
            clear_btn->set_visible(false);
        });

    lbr->set_child(*col);
    lb->append(*lbr);
    m_body.append(*lb);
}

// Editable richtext path (s32) — the dissertation buffer made writable. Same
// framed TextView as the read-only block, but editable, with the buffer's
// changed signal wired through on_change. Plain text this slice (the value is a
// string; rich HTML round-tripping is a later concern, matching the read-only
// renderer which also set_text() the value plainly). The buffer is captured by
// RefPtr so the handler reads the live text.
void ObjectForm::append_editable_richtext(const Folio::FormRow& row, const OnChange& on_change) {
    auto* hdr = Gtk::make_managed<Gtk::Label>(row.label);
    hdr->add_css_class("inspector-section-label");
    hdr->set_halign(Gtk::Align::START);
    hdr->set_margin_start(12);
    hdr->set_margin_top(6);
    hdr->set_margin_bottom(2);
    m_body.append(*hdr);

    auto* tv = Gtk::make_managed<Gtk::TextView>();
    tv->set_editable(true);
    tv->set_cursor_visible(true);
    tv->set_wrap_mode(Gtk::WrapMode::WORD);
    tv->add_css_class("object-form-richtext");
    tv->set_size_request(-1, 96);

    auto buf = tv->get_buffer();
    buf->set_text(field_display_string(row.type, row.value));

    std::string field_id = row.field_id;
    buf->signal_changed().connect([buf, field_id, on_change]() {
        if (on_change)
            on_change(field_id, json(std::string(buf->get_text())));
    });

    auto* frame = Gtk::make_managed<Gtk::Frame>();
    frame->set_margin_start(12);
    frame->set_margin_end(12);
    frame->set_margin_bottom(3);
    frame->set_child(*tv);
    m_body.append(*frame);
}

// ── s36 — configured editable widgets ────────────────────────────────────────
// Each reads its band/options from the field config (via the tested FormPlan
// accessors), seeds its value BEFORE wiring the change signal (no priming-fire),
// and reports a correctly-shaped raw value through on_change — the Inspector
// coerces (apply_field) and writes it through to the backing leaf, same path the
// text/richtext editors use. Read-only routing (relation, color, empty dropdown)
// never reaches these; populate() gates them.

// Number → SpinButton over [min,max] stepping by step. A stored value outside the
// configured band widens the band to admit it (never silently clamp the model);
// integer step shows 0 decimals, otherwise 2.
void ObjectForm::append_editable_number(const Folio::FormRow& row, const OnChange& on_change) {
    Gtk::Box& rb = compact_scaffold(m_body, row.label);

    double mn = config_num(row.config, "min", 0.0);
    double mx = config_num(row.config, "max", 1000000.0);
    double st = config_num(row.config, "step", 1.0);
    if (mx < mn) std::swap(mn, mx);
    if (st <= 0.0) st = 1.0;

    double cur = row.value.is_number() ? row.value.get<double>() : 0.0;
    if (cur < mn) mn = cur;
    if (cur > mx) mx = cur;

    auto adj  = Gtk::Adjustment::create(cur, mn, mx, st, st * 10.0, 0.0);
    auto* sb  = Gtk::make_managed<Gtk::SpinButton>(adj);
    sb->set_digits((st == std::floor(st)) ? 0 : 2);
    sb->set_halign(Gtk::Align::END);

    std::string field_id = row.field_id;
    sb->signal_value_changed().connect([sb, field_id, on_change]() {       // after seed
        if (on_change) on_change(field_id, json(sb->get_value()));
    });

    rb.append(*sb);
}

// Slider → horizontal Scale with the value drawn at the right; same band rules as
// number (widen to fit a stored out-of-band value).
void ObjectForm::append_editable_slider(const Folio::FormRow& row, const OnChange& on_change) {
    Gtk::Box& rb = compact_scaffold(m_body, row.label);

    double mn = config_num(row.config, "min", 0.0);
    double mx = config_num(row.config, "max", 100.0);
    double st = config_num(row.config, "step", 1.0);
    if (mx < mn) std::swap(mn, mx);
    if (st <= 0.0) st = 1.0;

    double cur = row.value.is_number() ? row.value.get<double>() : 0.0;
    if (cur < mn) mn = cur;
    if (cur > mx) mx = cur;

    auto adj = Gtk::Adjustment::create(cur, mn, mx, st, st * 10.0, 0.0);
    auto* sc = Gtk::make_managed<Gtk::Scale>(adj, Gtk::Orientation::HORIZONTAL);
    sc->set_draw_value(true);
    sc->set_value_pos(Gtk::PositionType::RIGHT);
    sc->set_digits((st == std::floor(st)) ? 0 : 2);
    sc->set_size_request(200, -1);
    sc->set_halign(Gtk::Align::END);

    std::string field_id = row.field_id;
    sc->signal_value_changed().connect([sc, field_id, on_change]() {
        if (on_change) on_change(field_id, json(sc->get_value()));
    });

    rb.append(*sc);
}

// Toggle → a plain Switch. GTK styles it via :checked natively (no custom class),
// so the handler just reports the bool; value seeded before connect.
void ObjectForm::append_editable_toggle(const Folio::FormRow& row, const OnChange& on_change) {
    Gtk::Box& rb = compact_scaffold(m_body, row.label);

    auto* sw = Gtk::make_managed<Gtk::Switch>();
    sw->set_active(row.value.is_boolean() && row.value.get<bool>());   // before connect
    sw->set_halign(Gtk::Align::END);
    sw->set_valign(Gtk::Align::CENTER);

    std::string field_id = row.field_id;
    sw->property_active().signal_changed().connect([sw, field_id, on_change]() {
        if (on_change) on_change(field_id, json(sw->get_active()));
    });

    rb.append(*sw);
}

// Dropdown → DropDown over the option LABELS, reporting the selected option's
// stable id. Only reached when options is non-empty (populate() falls an empty
// dropdown back to a read-only row). An unset / orphaned value selects index 0.
void ObjectForm::append_editable_dropdown(const Folio::FormRow& row, const OnChange& on_change) {
    auto options = config_options(row.config);
    Gtk::Box& rb = compact_scaffold(m_body, row.label);

    std::vector<Glib::ustring> labels;
    std::vector<std::string>   ids;
    labels.reserve(options.size());
    ids.reserve(options.size());
    for (const auto& o : options) { labels.push_back(o.label); ids.push_back(o.id); }

    auto  sl = Gtk::StringList::create(labels);
    auto* dd = Gtk::make_managed<Gtk::DropDown>(sl);
    dd->set_halign(Gtk::Align::END);

    std::string cur = row.value.is_string() ? row.value.get<std::string>() : std::string{};
    guint sel = 0;
    for (guint i = 0; i < ids.size(); ++i) if (ids[i] == cur) { sel = i; break; }
    dd->set_selected(sel);   // before connect

    std::string field_id = row.field_id;
    dd->property_selected().signal_changed().connect([dd, ids, field_id, on_change]() {
        guint i = dd->get_selected();
        if (on_change && i != GTK_INVALID_LIST_POSITION && i < ids.size())
            on_change(field_id, json(ids[i]));
    });

    rb.append(*dd);
}

// MultiSelect → a full-width card of fixed CheckButtons (the option set is closed
// by config, so no add/remove rebuild — safe to wire directly). A shared state
// vector in option order is kept alive by the toggle lambdas; each toggle rebuilds
// the checked-id array in order and reports it.
void ObjectForm::append_editable_multiselect(const Folio::FormRow& row, const OnChange& on_change) {
    auto* hdr = Gtk::make_managed<Gtk::Label>(row.label);
    hdr->add_css_class("inspector-section-label");
    hdr->set_halign(Gtk::Align::START);
    hdr->set_margin_start(12);
    hdr->set_margin_top(6);
    hdr->set_margin_bottom(2);
    m_body.append(*hdr);

    auto* card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    card->add_css_class("pomo-tile-card");
    card->set_margin_start(12);
    card->set_margin_end(12);
    card->set_margin_bottom(3);
    card->set_margin_top(2);

    auto options = config_options(row.config);

    std::set<std::string> checked;
    if (row.value.is_array())
        for (const auto& e : row.value)
            if (e.is_string()) checked.insert(e.get<std::string>());

    // (id, checked) in option order — shared so every checkbox's lambda sees the
    // same vector; the shared_ptr capture keeps it alive for the row's lifetime.
    auto state = std::make_shared<std::vector<std::pair<std::string, bool>>>();
    for (const auto& o : options)
        state->push_back({ o.id, checked.count(o.id) > 0 });

    std::string field_id = row.field_id;
    for (std::size_t i = 0; i < options.size(); ++i) {
        auto* cb = Gtk::make_managed<Gtk::CheckButton>(options[i].label);
        cb->set_active((*state)[i].second);   // before connect
        cb->set_margin_start(8);
        cb->set_margin_end(8);
        cb->signal_toggled().connect([cb, state, i, field_id, on_change]() {
            (*state)[i].second = cb->get_active();
            json arr = json::array();
            for (const auto& pr : *state) if (pr.second) arr.push_back(pr.first);
            if (on_change) on_change(field_id, arr);
        });
        card->append(*cb);
    }

    if (options.empty()) {
        auto* l = Gtk::make_managed<Gtk::Label>("No options defined");
        l->add_css_class("dim-label");
        l->set_halign(Gtk::Align::START);
        l->set_margin_start(8);
        l->set_margin_top(4);
        l->set_margin_bottom(4);
        card->append(*l);
    }

    m_body.append(*card);
}

// ── s37 — collection widgets: relation picker + list value editor ────────────

// Relation (single) → DropDown over the candidate objects with a leading "(none)"
// that clears the edge. Candidates come from the provider (over the store); the
// reported value is the selected object's iid (or "" for none). An unset/orphaned
// value lands on "(none)" without writing back until the user picks.
void ObjectForm::append_editable_relation_single(const Folio::FormRow& row, const OnChange& on_change) {
    const std::string target = row.config.is_object()
        ? row.config.value("target_type", std::string{}) : std::string{};
    std::vector<FieldChoice> cands =
        m_relation_provider ? m_relation_provider(target) : std::vector<FieldChoice>{};

    Gtk::Box& rb = compact_scaffold(m_body, row.label);

    std::vector<Glib::ustring> labels; labels.push_back("(none)");
    std::vector<std::string>   ids;    ids.push_back(std::string{});
    for (const auto& c : cands) { labels.push_back(c.label); ids.push_back(c.id); }

    auto  sl = Gtk::StringList::create(labels);
    auto* dd = Gtk::make_managed<Gtk::DropDown>(sl);
    dd->set_halign(Gtk::Align::END);

    std::string cur = row.value.is_string() ? row.value.get<std::string>() : std::string{};
    guint sel = 0;
    for (guint i = 0; i < ids.size(); ++i) if (ids[i] == cur) { sel = i; break; }
    dd->set_selected(sel);   // before connect

    std::string field_id = row.field_id;
    dd->property_selected().signal_changed().connect([dd, ids, field_id, on_change]() {
        guint i = dd->get_selected();
        if (on_change && i != GTK_INVALID_LIST_POSITION && i < ids.size())
            on_change(field_id, json(ids[i]));
    });

    rb.append(*dd);
}

// Relation (multi) → a full-width card of CheckButtons over the candidate objects,
// reporting the checked iids as an array in candidate order. Same closed-set safety
// as multiselect — the candidate list is baked at render, no live rebuild — and an
// empty candidate set shows a quiet placeholder (make the target type's objects
// first).
void ObjectForm::append_editable_relation_multi(const Folio::FormRow& row, const OnChange& on_change) {
    const std::string target = row.config.is_object()
        ? row.config.value("target_type", std::string{}) : std::string{};
    std::vector<FieldChoice> cands =
        m_relation_provider ? m_relation_provider(target) : std::vector<FieldChoice>{};

    auto* hdr = Gtk::make_managed<Gtk::Label>(row.label);
    hdr->add_css_class("inspector-section-label");
    hdr->set_halign(Gtk::Align::START);
    hdr->set_margin_start(12);
    hdr->set_margin_top(6);
    hdr->set_margin_bottom(2);
    m_body.append(*hdr);

    auto* card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    card->add_css_class("pomo-tile-card");
    card->set_margin_start(12);
    card->set_margin_end(12);
    card->set_margin_bottom(3);
    card->set_margin_top(2);

    std::set<std::string> checked;
    if (row.value.is_array())
        for (const auto& e : row.value)
            if (e.is_string()) checked.insert(e.get<std::string>());

    auto state = std::make_shared<std::vector<std::pair<std::string, bool>>>();
    for (const auto& c : cands)
        state->push_back({ c.id, checked.count(c.id) > 0 });

    std::string field_id = row.field_id;
    for (std::size_t i = 0; i < cands.size(); ++i) {
        auto* cb = Gtk::make_managed<Gtk::CheckButton>(cands[i].label);
        cb->set_active((*state)[i].second);   // before connect
        cb->set_margin_start(8);
        cb->set_margin_end(8);
        cb->signal_toggled().connect([cb, state, i, field_id, on_change]() {
            (*state)[i].second = cb->get_active();
            json arr = json::array();
            for (const auto& pr : *state) if (pr.second) arr.push_back(pr.first);
            if (on_change) on_change(field_id, arr);
        });
        card->append(*cb);
    }

    if (cands.empty()) {
        auto* l = Gtk::make_managed<Gtk::Label>("No objects of this type yet");
        l->add_css_class("dim-label");
        l->set_halign(Gtk::Align::START);
        l->set_margin_start(8);
        l->set_margin_top(4);
        l->set_margin_bottom(4);
        card->append(*l);
    }

    m_body.append(*card);
}

// List → a full-width card of free-text [entry][trash] rows over the value array,
// plus a "+ Add" (blank row) and, when the field configures presets, a row of
// quick-add chips. The working array lives in a shared_ptr kept alive by the row/
// button handlers; rebuild_list_rows redraws the rows region on add/remove (entry
// edits mutate in place without a rebuild). Every mutation emits the whole array
// through on_change → apply_field (List coercion) → the store.
void ObjectForm::append_editable_list(const Folio::FormRow& row, const OnChange& on_change) {
    auto* hdr = Gtk::make_managed<Gtk::Label>(row.label);
    hdr->add_css_class("inspector-section-label");
    hdr->set_halign(Gtk::Align::START);
    hdr->set_margin_start(12);
    hdr->set_margin_top(6);
    hdr->set_margin_bottom(2);
    m_body.append(*hdr);

    auto* card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    card->add_css_class("pomo-tile-card");
    card->set_margin_start(12);
    card->set_margin_end(12);
    card->set_margin_bottom(3);
    card->set_margin_top(2);

    auto state = std::make_shared<std::vector<std::string>>();
    if (row.value.is_array())
        for (const auto& e : row.value)
            if (e.is_string()) state->push_back(e.get<std::string>());

    std::string field_id = row.field_id;
    std::function<void()> emit = [state, field_id, on_change]() {
        json arr = json::array();
        for (const auto& s : *state) arr.push_back(s);
        if (on_change) on_change(field_id, arr);
    };

    auto* rows_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    rows_box->set_margin_top(4);
    card->append(*rows_box);
    rebuild_list_rows(*rows_box, state, emit);   // initial fill

    // Preset quick-add chips (config.presets) — tapping appends that value.
    const auto presets = config_presets(row.config);
    if (!presets.empty()) {
        auto* prow = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        prow->set_margin_start(8);
        prow->set_margin_top(2);
        auto* hint = Gtk::make_managed<Gtk::Label>("Add:");
        hint->add_css_class("dim-label");
        hint->set_valign(Gtk::Align::CENTER);
        prow->append(*hint);
        for (const auto& p : presets) {
            auto* chip = Gtk::make_managed<Gtk::Button>(p);
            chip->add_css_class("flat");
            chip->signal_clicked().connect([rows_box, state, emit, p]() {
                state->push_back(p);
                rebuild_list_rows(*rows_box, state, emit);
                emit();
            });
            prow->append(*chip);
        }
        card->append(*prow);
    }

    auto* add = Gtk::make_managed<Gtk::Button>("+ Add");
    add->add_css_class("flat");
    add->set_halign(Gtk::Align::START);
    add->set_margin_start(4);
    add->signal_clicked().connect([rows_box, state, emit]() {
        state->push_back(std::string{});
        rebuild_list_rows(*rows_box, state, emit);
        emit();
    });
    card->append(*add);

    m_body.append(*card);
}

// The §7 "door you walk through only when you want more": a quiet affordance at
// the bottom of the editable form that opens the template builder. Shown only
// when editable and a handler is wired. s35 — the label reflects state: a locked
// built-in invites a CLONE ("Customize fields…"), an already-cloned template
// edits in place ("Edit fields…"). Either way the click runs the same handler;
// the Inspector decides whether to clone first.
// s44 — the relief (DESIGN_scrapbook §4): a read-only "Referenced by" section that
// lists every object pointing AT this one, and through which field. The provider
// computes incoming_edges live (node-is-truth, projection-not-stored), so the list
// reflects the current graph on every populate. Hidden entirely when nothing points
// here — the relief only appears where there is contact. Navigation is deferred:
// rows are labels this slice, made activatable in a clean follow.
void ObjectForm::append_backlinks(const std::string& iid) {
    if (iid.empty() || !m_backlink_provider) return;
    const std::vector<Backlink> links = m_backlink_provider(iid);
    if (links.empty()) return;

    auto* h = Gtk::make_managed<Gtk::Label>("Referenced by");
    h->add_css_class("inspector-section-label");
    h->set_halign(Gtk::Align::START);
    h->set_margin_start(12);
    h->set_margin_top(12);
    h->set_margin_bottom(2);
    m_body.append(*h);

    auto* lb = Gtk::make_managed<Gtk::ListBox>();
    lb->set_selection_mode(Gtk::SelectionMode::NONE);
    lb->add_css_class("pref-listbox");
    for (const auto& bl : links) {
        auto* lbr = Gtk::make_managed<Gtk::ListBoxRow>();
        auto* rb  = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        rb->set_margin_start(12);
        rb->set_margin_end(12);
        rb->set_margin_top(3);
        rb->set_margin_bottom(3);

        auto* name = Gtk::make_managed<Gtk::Label>(
            bl.source_label.empty() ? "(unnamed)" : bl.source_label);
        name->add_css_class("pref-row-label");
        name->set_hexpand(true);
        name->set_halign(Gtk::Align::START);
        name->set_ellipsize(Pango::EllipsizeMode::END);

        auto* via = Gtk::make_managed<Gtk::Label>(
            bl.via_label.empty() ? std::string{} : "via " + bl.via_label);
        via->add_css_class("dim-label");
        via->set_halign(Gtk::Align::END);
        via->set_ellipsize(Pango::EllipsizeMode::END);
        via->set_max_width_chars(24);

        rb->append(*name);
        rb->append(*via);
        lbr->set_child(*rb);
        lb->append(*lbr);
    }
    m_body.append(*lb);
}

// s44 §11 — the instance-side "Edit fields…"/"Customize fields…" door is retired.
// Schema is edited only on the Template node (no-mutate); append_edit_template_button
// and its clone-to-customize path are gone.

void ObjectForm::populate(const Folio::Template& tmpl, const Folio::Object& obj,
                          bool editable, OnChange on_change) {
    clear_body();
    m_heading.set_text(tmpl.type_name.empty() ? "Object" : tmpl.type_name);
    m_obj_values = obj.values;   // s44 — for per-instance preview state (image height)

    Folio::FormPlan plan = plan_form(tmpl, obj);
    for (const auto& row : plan.rows) {
        const bool can_edit = editable && !row.read_only;

        // ── Heading (s39): a section divider. Renders as a standalone header; the
        // fields after it group under it visually until the next heading. Carries
        // no value — never editable, intercepted before any value routing.
        if (row.type == FieldType::Heading) {
            auto* h = Gtk::make_managed<Gtk::Label>(
                row.label.empty() ? "Section" : row.label);
            h->add_css_class("inspector-section-label");
            h->set_halign(Gtk::Align::START);
            h->set_margin_start(12);
            h->set_margin_top(12);     // extra top gap sets the section apart
            h->set_margin_bottom(2);
            m_body.append(*h);
            continue;
        }

        // ── Relation (s37): config decides single (dropdown) vs multi (card).
        // Bypasses the full_width flag — a multi relation wants the card width,
        // a single relation a compact row, regardless of field_is_full_width.
        if (row.type == FieldType::Relation) {
            const bool multi = row.config.is_object()
                            && row.config.value("multi", false);
            if (can_edit && m_relation_provider) {
                if (multi) append_editable_relation_multi(row, on_change);
                else       append_editable_relation_single(row, on_change);
            } else if (multi) {
                append_full_width(row);                        // read-only chips (no provider)
            } else {
                append_compact_row(row);                       // read-only (shows raw iid)
            }
            continue;
        }

        // ── Full-width fields (richtext / list / multiselect) ────────────────
        if (row.full_width) {
            if (can_edit && row.type == FieldType::RichText)
                append_editable_richtext(row, on_change);      // the writable buffer (s32)
            else if (can_edit && row.type == FieldType::MultiSelect)
                append_editable_multiselect(row, on_change);   // fixed checkbox set (s36)
            else if (can_edit && row.type == FieldType::List)
                append_editable_list(row, on_change);          // free-text value editor (s37)
            else
                append_full_width(row);                        // read-only block
            continue;
        }

        // ── Read-only compact rows ───────────────────────────────────────────
        if (!can_edit) { append_compact_row(row); continue; }

        // ── Editable compact rows ────────────────────────────────────────────
        switch (row.type) {
            case FieldType::Text:
            case FieldType::Date:
                append_editable_text(row, on_change);          // wired entry: name / date
                break;
            case FieldType::Image:
                append_editable_image(row, on_change);         // s44 — picker + preview
                break;
            case FieldType::Number:
                append_editable_number(row, on_change);        // SpinButton (s36)
                break;
            case FieldType::Slider:
                append_editable_slider(row, on_change);        // Scale (s36)
                break;
            case FieldType::Toggle:
                append_editable_toggle(row, on_change);        // Switch (s36)
                break;
            case FieldType::Dropdown:
                if (config_options(row.config).empty())
                    append_compact_row(row);                   // no options to pick yet → read-only
                else
                    append_editable_dropdown(row, on_change);  // DropDown (s36)
                break;
            default:
                append_compact_row(row);                       // Color, etc. → read-only this slice
                break;
        }
    }
    append_backlinks(obj.iid);                                 // s44 — the relief
    // s44 §11 — NO schema door on the instance. Editing fields lives only on the
    // Template node now (no-mutate); a Character is born on a Template and reshaped
    // only by editing that Template. The clone-to-customize path is retired.
}

}  // namespace Folio
