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

namespace Folio {

ObjectForm::ObjectForm()
    : Gtk::Box(Gtk::Orientation::VERTICAL, 0),
      m_body(Gtk::Orientation::VERTICAL, 0) {
    set_name("object-form");
    m_heading.add_css_class("inspector-section-label");
    m_heading.set_halign(Gtk::Align::START);
    m_heading.set_margin_start(12);
    m_heading.set_margin_top(8);
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
    rb->set_margin_top(8);
    rb->set_margin_bottom(8);

    auto* l = Gtk::make_managed<Gtk::Label>(row.label);
    l->add_css_class("pref-row-label");
    l->set_hexpand(true);
    l->set_halign(Gtk::Align::START);

    const std::string text = field_display_string(row.type, row.value);
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

    const std::string text = field_display_string(row.type, row.value);

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
        frame->set_margin_bottom(8);
        frame->set_child(*tv);
        m_body.append(*frame);
    } else {
        auto* card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
        card->add_css_class("pomo-tile-card");
        card->set_margin_start(12);
        card->set_margin_end(12);
        card->set_margin_bottom(8);
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
    rb->set_margin_top(8);
    rb->set_margin_bottom(8);

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
    frame->set_margin_bottom(8);
    frame->set_child(*tv);
    m_body.append(*frame);
}

// The §7 "door you walk through only when you want more": a quiet affordance at
// the bottom of the editable form that opens the template builder. Shown only
// when editable and a handler is wired.
void ObjectForm::append_edit_template_button() {
    auto* btn = Gtk::make_managed<Gtk::Button>("Edit fields…");
    btn->add_css_class("flat");
    btn->set_halign(Gtk::Align::START);
    btn->set_margin_start(12);
    btn->set_margin_top(4);
    btn->set_margin_bottom(8);
    btn->signal_clicked().connect([this]() {
        if (m_on_edit_template) m_on_edit_template();
    });
    m_body.append(*btn);
}

void ObjectForm::populate(const Folio::Template& tmpl, const Folio::Object& obj,
                          bool editable, OnChange on_change) {
    clear_body();
    m_heading.set_text(tmpl.type_name.empty() ? "Object" : tmpl.type_name);

    Folio::FormPlan plan = plan_form(tmpl, obj);
    for (const auto& row : plan.rows) {
        const bool can_edit = editable && !row.read_only;
        if (row.full_width) {
            if (can_edit && row.type == FieldType::RichText)
                append_editable_richtext(row, on_change);   // the writable buffer (s32)
            else
                append_full_width(row);                      // read-only block (list, etc.)
        } else if (can_edit && (row.type == FieldType::Text
                             || row.type == FieldType::Date
                             || row.type == FieldType::Image)) {
            append_editable_text(row, on_change);            // wired entry: name / image path
        } else {
            append_compact_row(row);                         // read-only label/value
        }
    }
    if (editable && m_on_edit_template)
        append_edit_template_button();                       // the §7 door (s33)
}

}  // namespace Folio
