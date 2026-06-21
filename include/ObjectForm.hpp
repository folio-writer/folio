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

    // Fired when the user clicks the "Edit fields…" affordance (the §7 door).
    // The Inspector opens the template builder for the current object's template.
    using OnEditTemplate = std::function<void()>;

    // s37 — the relation picker's candidate source. Given a relation field's
    // config.target_type, return the pickable objects as {iid, display-name}. The
    // Inspector supplies this closing over the model's ObjectStore, so the form
    // stays decoupled from the store (same idiom as OnChange). An empty target_type
    // means "any type" — every object is a candidate.
    using RelationProvider =
        std::function<std::vector<FieldChoice>(const std::string& target_type)>;

    ObjectForm();

    // Render `obj` through `tmpl`. editable=false (this slice) renders read-only;
    // when true, the text path wires changes through `on_change`. Safe to call
    // repeatedly — the body is rebuilt each time.
    void populate(const Folio::Template& tmpl, const Folio::Object& obj,
                  bool editable = false, OnChange on_change = {});

    // Wire the "Edit fields…" affordance (shown at the bottom when editable).
    void set_on_edit_template(OnEditTemplate cb) { m_on_edit_template = std::move(cb); }

    // Wire the relation candidate source (s37). Set once; consulted while
    // populating any relation field. Absent → relation rows render read-only.
    void set_relation_provider(RelationProvider cb) { m_relation_provider = std::move(cb); }

    // Show an empty state (no object selected / no template resolved).
    void clear();

private:
    Gtk::Box   m_body;      // the rebuilt content column
    Gtk::Label m_heading;   // type_name heading
    OnEditTemplate m_on_edit_template;
    RelationProvider m_relation_provider;   // s37 — candidate source for relations

    void clear_body();
    void append_compact_row(const FormRow& row);                 // label / value
    void append_full_width(const FormRow& row);                  // richtext / list block
    void append_editable_text(const FormRow& row, const OnChange& on_change);     // text/image entry
    void append_editable_richtext(const FormRow& row, const OnChange& on_change); // the buffer (s32)
    // s36 — configured editable widgets, each wired to the live value-write path.
    void append_editable_number(const FormRow& row, const OnChange& on_change);      // SpinButton(min/max/step)
    void append_editable_slider(const FormRow& row, const OnChange& on_change);      // Scale(min/max/step)
    void append_editable_toggle(const FormRow& row, const OnChange& on_change);      // Switch (.active)
    void append_editable_dropdown(const FormRow& row, const OnChange& on_change);    // DropDown(config.options)
    void append_editable_multiselect(const FormRow& row, const OnChange& on_change); // CheckButtons → [id]
    // s37 — collection widgets: the relation picker (over store candidates) and the
    // free-text list value editor (presets offered as quick-add).
    void append_editable_relation_single(const FormRow& row, const OnChange& on_change); // DropDown(candidates)+none
    void append_editable_relation_multi(const FormRow& row, const OnChange& on_change);  // CheckButtons(candidates)
    void append_editable_list(const FormRow& row, const OnChange& on_change);            // [entry][x]+add card
    void append_edit_template_button(bool builtin);             // the §7 door (s33/s35)
};

}  // namespace Folio
