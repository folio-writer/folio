#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — ObjectForm.hpp   (s31 — the form renderer's HANDS; GTK/gtkmm4)
//
// §6 made visible: a self-contained widget that renders an Object through its
// Template. It is the thin GTK consumer of the pure FormPlan brain — it walks
// plan_form()'s rows, emits one widget per field type, and lays them out in the
// Inspector's idiom (disclosure-style heading + compact label/value rows, with a
// full-width block where a field needs room). No novel logic lives here; the
// ordering, defaulting, layout-hints, and display strings are all decided in
// FormPlan (pure, tested). This file is assembly.
//
// THIS SLICE (s32) makes the floor fields EDITABLE — the form becomes the
// editing surface for name (text), image (path entry), and the description
// buffer (richtext). Each editable widget reports its raw value through the
// `on_change` sink; the Inspector coerces it (apply_field) and writes it THROUGH
// to the backing binder leaf via the pure ObjectIO::floor_field_to_leaf inverse
// mapping, so the projection stays the single source of truth and the Sidebar
// (which reads the leaf title) updates live. Relation stays read-only (no picker
// yet); list/multiselect and the remaining input types (number/slider/toggle/
// dropdown) render read-only until a template can create them (template builder,
// a later slice) — the routing in populate() is the seam where they slot in.
//
// Self-contained: owns its own heading/row builders (reusing the shipped CSS
// classes pref-listbox / pref-row-label / inspector-section-label) so it does
// not depend on Inspector internals. Rebuilds its rows on populate() — a form's
// field set is template-defined and varies per object, so the body is cleared
// and re-emitted (the persistent-window rule governs top-level lenses/popovers,
// not the rows inside a panel).
// ─────────────────────────────────────────────────────────────────────────────

#include "Object.hpp"
#include "FormPlan.hpp"

#include <gtkmm/box.h>
#include <gtkmm/label.h>

#include <functional>
#include <string>

namespace Folio {

class ObjectForm : public Gtk::Box {
public:
    // Raw value a widget produced for field `field_id`, to be coerced + stored
    // by the caller (Inspector) via apply_field() and synced to the backing leaf.
    using OnChange = std::function<void(const std::string& field_id, const json& raw)>;

    ObjectForm();

    // Render `obj` through `tmpl`. editable=false (this slice) renders read-only;
    // when true, the text path wires changes through `on_change`. Safe to call
    // repeatedly — the body is rebuilt each time.
    void populate(const Folio::Template& tmpl, const Folio::Object& obj,
                  bool editable = false, OnChange on_change = {});

    // Show an empty state (no object selected / no template resolved).
    void clear();

private:
    Gtk::Box   m_body;      // the rebuilt content column
    Gtk::Label m_heading;   // type_name heading

    void clear_body();
    void append_compact_row(const FormRow& row);                 // label / value
    void append_full_width(const FormRow& row);                  // richtext / list block
    void append_editable_text(const FormRow& row, const OnChange& on_change);     // text/image entry
    void append_editable_richtext(const FormRow& row, const OnChange& on_change); // the buffer (s32)
};

}  // namespace Folio
