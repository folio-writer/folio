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

// s44 — per-instance image-preview height is remembered in the object's values map
// under a reserved key (travels with the project, round-trips via ObjectIO, and is
// ignored by the schema-driven renderer and edge reads). One entry per image field.
inline constexpr const char* kImagePreviewKeyPrefix = "__img_h__";
inline std::string image_preview_key(const std::string& field_id) {
    return std::string(kImagePreviewKeyPrefix) + field_id;
}

class ObjectForm : public Gtk::Box {
public:
    // Raw value a widget produced for field `field_id`, to be coerced + stored
    // by the caller (Inspector) via apply_field() and synced to the backing leaf.
    using OnChange = std::function<void(const std::string& field_id, const json& raw)>;

    // Fired when the user clicks the "Edit fields…" affordance (the §7 door).
    // The Inspector opens the template builder for the current object's template.
    // s44 §11 — RETIRED: schema editing lives only on the Template node now
    // (no-mutate). The instance form carries no schema door.

    // s37 — the relation picker's candidate source. Given a relation field's
    // config.target_type, return the pickable objects as {iid, display-name}. The
    // Inspector supplies this closing over the model's ObjectStore, so the form
    // stays decoupled from the store (same idiom as OnChange). An empty target_type
    // means "any type" — every object is a candidate.
    using RelationProvider =
        std::function<std::vector<FieldChoice>(const std::string& target_type)>;

    // s44 — the RELIEF (DESIGN_scrapbook §4): "stand on this object, see everything
    // attached to it." A backlink is one incoming edge, resolved for display:
    // which object points here, and through which field. Computed, never stored —
    // the form asks the provider (which closes over the store's incoming_edges) on
    // every populate, so a rename or a re-point is always reflected. One row per
    // (source, field) pair; read-only this slice (navigation is a clean follow).
    struct Backlink {
        std::string source_iid;    // the object that points here
        std::string source_label;  // its display name
        std::string via_label;     // the relation field's label on the source
    };
    using BacklinkProvider =
        std::function<std::vector<Backlink>(const std::string& iid)>;

    // s70 — the gallery's reverse view (DESIGN_gallery §3, spine #3): "the images
    // that point at me." On a Character/Place, a read-only strip of the image
    // fragments that link TO this object — the SAME image→object links the
    // lightbox draws, surfaced from the other side, computed never stored. The
    // provider returns one entry per linked asset (gallery_images_of, asset
    // sources only); the form stays decoupled from the pool/edges/bundle by taking
    // only resolved display strings (a thumb path + a caption), exactly as the
    // relation/backlink providers hand it plain rows. Empty / no provider → no
    // section (the strip only appears where an image actually points here). Scenes
    // have no form, so "images of a scene" has no home here — that lands later in
    // the editor rail / timeline and does NOT block this strip.
    struct LinkedImage {
        std::string iid;          // the ast_… fragment (stable handle)
        std::string thumb_path;   // absolute path to thumbs/<iid>.<ext> (may be absent on disk)
        std::string caption;      // lightbox caption, used as the tooltip
    };
    using ImageStripProvider =
        std::function<std::vector<LinkedImage>(const std::string& iid)>;

    // s79 — the editable Image field's pool door. The form has no model access
    // (same reason the strip is a provider), so the owner injects two seams:
    //   • ImageResolveFn turns a stored field VALUE (an ast_ pool-fragment iid OR
    //     a legacy external path) into a loadable display path — the s72 dual-read
    //     (image_display_path). Both the on-open preview seed AND the post-set
    //     render route through it; absent → the value is treated as a literal path
    //     (the pre-s79 behaviour).
    //   • ImageImportFn imports a chosen file through the ONE pipeline
    //     (ImageImporter::import_file) and reports the new fragment iid or a clear
    //     error; absent → the Set handler stores the raw path (pre-s79 behaviour).
    // Together they make a Character/Place template image a normalized in-bundle
    // fragment (pool/gallery-visible, survives the source file moving), consistent
    // with gallery import — without coupling this header to the import engine.
    struct ImageImportOutcome {
        bool ok = false;
        std::string iid;       // the new ast_… fragment (on ok)
        std::string error;     // a clear, user-facing message (on failure)
        bool low_res = false;  // imported, but below the chosen detail tier (a cue)
    };
    using ImageResolveFn = std::function<std::string(const std::string& value)>;
    using ImageImportFn  = std::function<ImageImportOutcome(const std::string& path)>;
    // s79 — sibling of ImageImportFn for bytes-sourced imports (texture-drop /
    // paste): already-encoded image bytes + an optional caption → outcome (via
    // ImageImporter::import_bytes). Absent → an image drop/paste is ignored (there
    // is no raw-path fallback for in-memory bytes, unlike a dropped file).
    using ImageImportBytesFn =
        std::function<ImageImportOutcome(const std::string& data,
                                         const std::string& caption)>;

    ObjectForm();

    // Render `obj` through `tmpl`. editable=false (this slice) renders read-only;
    // when true, the text path wires changes through `on_change`. Safe to call
    // repeatedly — the body is rebuilt each time.
    void populate(const Folio::Template& tmpl, const Folio::Object& obj,
                  bool editable = false, OnChange on_change = {});

    // Wire the relation candidate source (s37). Set once; consulted while
    // populating any relation field. Absent → relation rows render read-only.
    void set_relation_provider(RelationProvider cb) { m_relation_provider = std::move(cb); }

    // s44 — wire the relief's source (incoming_edges, resolved). Set once;
    // consulted at the end of every populate. Absent / empty → no section shown.
    void set_backlink_provider(BacklinkProvider cb) { m_backlink_provider = std::move(cb); }

    // s70 — wire the reverse image strip's source (gallery_images_of, resolved to
    // thumb path + caption). Set once; consulted at the end of every populate.
    // Absent / empty → no strip shown.
    void set_image_strip_provider(ImageStripProvider cb) { m_image_strip_provider = std::move(cb); }

    // s79 — wire the editable Image field's resolve + import seams (see above).
    // Set once; consulted while populating any Image field. Both optional; when
    // unset the field falls back to its pre-s79 raw-path behaviour.
    void set_image_resolve_fn(ImageResolveFn cb) { m_image_resolve_fn = std::move(cb); }
    void set_image_import_fn(ImageImportFn cb) { m_image_import_fn = std::move(cb); }
    void set_image_import_bytes_fn(ImageImportBytesFn cb) { m_image_import_bytes_fn = std::move(cb); }

    // Show an empty state (no object selected / no template resolved).
    void clear();

private:
    Gtk::Box   m_body;      // the rebuilt content column
    Gtk::Label m_heading;   // type_name heading
    RelationProvider m_relation_provider;   // s37 — candidate source for relations
    BacklinkProvider m_backlink_provider;   // s44 — the relief (incoming edges)
    ImageStripProvider m_image_strip_provider;  // s70 — reverse image strip (gallery_images_of)
    ImageResolveFn   m_image_resolve_fn;        // s79 — value -> loadable display path (dual-read)
    ImageImportFn    m_image_import_fn;         // s79 — chosen file -> pool fragment
    ImageImportBytesFn m_image_import_bytes_fn; // s79 — texture-drop / paste -> pool fragment
    json             m_obj_values;          // s44 — current object's values (preview state)

    void clear_body();
    void append_compact_row(const FormRow& row);                 // label / value
    void append_full_width(const FormRow& row);                  // richtext / list block
    void append_editable_text(const FormRow& row, const OnChange& on_change);     // text/image entry
    void append_editable_image(const FormRow& row, const OnChange& on_change);    // s44 — picker + preview
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
    void append_backlinks(const std::string& iid);              // s44 — the relief section
    void append_image_strip(const std::string& iid);            // s70 — reverse image strip
};

}  // namespace Folio
