#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// CustomMindMapCanvas.hpp — the OWNED mind map, GTK side (s51).
//
// The counterpart to MindMapCanvas (the lens). Where the lens GENERATES its
// items from the binder and READS edges it never owns, this canvas is a thin
// painter over an OWNED CMMDoc (s50): it reads/writes the document's nodes,
// edges, categories, layout and subjects, and persists the whole CMMDoc back
// into the host Reference's body cell via a persist callback.
//
// DISCIPLINE (HANDOFF §3): pure logic, thin GTK. Every coordinate/geometry/
// hit decision is asked of the pure layer — world_to_screen / screen_to_world /
// hit_test from MindMap.hpp, and cmm_* construction/subjects/recents/stamp from
// CustomMindMap.hpp. This widget only paints, forwards input, and calls them.
//
// OWNED differences from the lens (HANDOFF §B):
//   • Free-flow floor — a node sits where dropped; drag writes node.x/y, no rule
//     owns position (pinned stays inert).
//   • Edges are OWNED — the Link tool calls cmm_add_edge with a typed category;
//     the edge is coloured by its label, recents surfaced in the picker.
//   • No synthetic hubs, no balloon, no containment spokes — those are the lens's
//     binder projection; this document has none of that.
//
// THE TOOLBAR (HANDOFF §C) is why the document has one and the lens doesn't: an
// OSD pill drives the authoring verbs — Text (drop a text node), Anchor (pick a
// project object, drop a node pointing at it), Link (drag node→node, pick a
// category), Frame ▾ (stamp a labelled fan), plus the shared ⤢ fit. The About
// header chip-row drives the many-to-many subjects.
// ─────────────────────────────────────────────────────────────────────────────

#include <functional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <gtkmm.h>

#include "CustomMindMap.hpp"   // the OWNED model (CMMDoc + cmm_*)
#include "MindMap.hpp"         // shared presentation: MapGlyph, MapViewport, hit_test

namespace Folio {

class FolioPrefs;   // category-colour palette (tag_colors) for owned edges
class DocumentModel; // s89 — resolve an anchor target's CURRENT kind (convert-aware glyph)

class CustomMindMapCanvas : public Gtk::Box {
public:
    CustomMindMapCanvas(DocumentModel& model, FolioPrefs& prefs);

    // ── Document lifecycle ─────────────────────────────────────────────────────
    // Load a CMMDoc as the editing surface. `set_document` takes a parsed doc;
    // `load_string` parses the host fragment's stored body (empty/garbage → a
    // fresh empty doc carrying `iid`). The canvas frames-all on the next draw.
    void set_document(const CMMDoc& doc);
    void load_string(const std::string& iid, const std::string& body);
    void clear_document();                       // detach (no host node)

    const CMMDoc& document() const { return m_doc; }

    // ── App-wide wiring (set once by the Editor) ───────────────────────────────
    // Activating an Anchor navigates to its target iid — the same app-wide path
    // the lens uses (switch to Write + select the node).
    using OpenCallback = std::function<void(const std::string& iid)>;
    void set_open_callback(OpenCallback cb) { m_on_open = std::move(cb); }

    // Persist the whole CMMDoc (called after every owned mutation). The Editor
    // writes the string into the host Reference's body and marks the model dirty.
    using PersistCallback = std::function<void(const std::string& cmm_string)>;
    void set_persist_callback(PersistCallback cb) { m_on_persist = std::move(cb); }

    // Rename: the map's title is the host fragment's title. Editing it on the
    // canvas fires this so the Editor can write node->title + refresh chrome (the
    // sidebar row, the editor header). The CMMDoc.name is kept in sync too.
    using RenameCallback = std::function<void(const std::string& name)>;
    void set_rename_callback(RenameCallback cb) { m_on_rename = std::move(cb); }

    // A project node the Anchor / subject pickers can offer, with its section
    // group ("Manuscript"/"Characters"/"Places"/"References") and tree depth so the
    // picker can show Parts ▸ Chapters ▸ Scenes (and Groups ▸ members) indented.
    struct ObjOption { std::string iid; std::string name; std::string group; int depth = 0; };
    // Candidate objects for Anchor + subject pickers (all project objects).
    using ObjectsProvider = std::function<std::vector<ObjOption>()>;
    void set_objects_provider(ObjectsProvider p) { m_objects = std::move(p); }
    // Display name for an iid (Anchor labels, subject chips). "" → fall back to iid.
    using NameProvider = std::function<std::string(const std::string& iid)>;
    void set_name_provider(NameProvider p) { m_name_of = std::move(p); }

    // Label-colour index (1-based into the tag palette; 0 = none) for an iid — an
    // Anchor with no owned colour inherits its target object's label colour, so
    // the map colour-threads to truth the way the lens does.
    using ColorProvider = std::function<int(const std::string& iid)>;
    void set_color_provider(ColorProvider p) { m_color_of = std::move(p); }

private:
    DocumentModel& m_model;   // s89 — anchor-target kind resolution (convert-aware)
    FolioPrefs& m_prefs;

    // ── Owned model ────────────────────────────────────────────────────────────
    CMMDoc      m_doc;
    bool        m_attached = false;   // a host node is loaded

    // ── Widget tree ────────────────────────────────────────────────────────────
    Gtk::Entry       m_title_entry;                                 // the map's name
    bool             m_title_loading = false;                       // guard programmatic set
    Gtk::Box         m_about_row{Gtk::Orientation::HORIZONTAL, 6};   // subjects header
    Gtk::Label       m_about_label;
    Gtk::Box         m_chip_box{Gtk::Orientation::HORIZONTAL, 6};
    Gtk::Button      m_subject_add;

    Gtk::Overlay     m_overlay;
    Gtk::DrawingArea m_area;
    Gtk::Label       m_empty_hint;
    Gtk::Label       m_mode_hint;     // top-centre OSD: which tool is armed
    Gtk::Label       m_sel_badge;     // bottom-left OSD: "N selected"

    // Floating OSD toolbar (the authoring verbs).
    Gtk::Box         m_tools{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::ToggleButton m_t_text;
    Gtk::ToggleButton m_t_anchor;
    Gtk::ToggleButton m_t_link;
    Gtk::Button       m_t_frame;
    Gtk::ToggleButton m_t_select;   // the home tool: 1-n select / move / delete
    Gtk::Button       m_fit_btn;
    // Direct pointers to each tool button's label, so the active highlight is set
    // without depending on get_child()/dynamic_cast.
    Gtk::Label* m_tl_select = nullptr;
    Gtk::Label* m_tl_text   = nullptr;
    Gtk::Label* m_tl_anchor = nullptr;
    Gtk::Label* m_tl_link   = nullptr;

    // ── Presentation state ─────────────────────────────────────────────────────
    MapViewport                            m_vp;
    std::vector<MindMapLayout::Placement>  m_placements;   // built from m_doc.nodes

    // ── Tool mode ──────────────────────────────────────────────────────────────
    // Select is HOME: click / shift-click / marquee to pick 1-n nodes, drag the
    // selection to move it as a group, Del to remove it. The other tools are the
    // authoring verbs; each is one-shot (it returns to Select when its act lands).
    enum class Tool { Select, PlaceText, PlaceAnchor, Link };
    Tool        m_tool = Tool::Select;
    std::string m_pending_anchor_iid;    // armed Anchor target (+ its name)
    std::string m_pending_anchor_name;

    // ── Interaction state ───────────────────────────────────────────────────────
    bool   m_space_held  = false;
    bool   m_panning     = false;
    bool   m_did_pan     = false;
    double m_pan_base_x  = 0.0, m_pan_base_y = 0.0;
    double m_ptr_x       = 0.0, m_ptr_y      = 0.0;
    bool   m_fit_pending = true;

    // Selection (1-n) + group move. m_move_base holds each selected node's world
    // position at drag-begin; the drag applies one shared delta to all of them.
    std::set<std::string>                            m_sel;
    std::set<std::string>                            m_sel_base;   // selection at marquee start
    std::unordered_map<std::string, WorldPt>         m_move_base;
    bool        m_moving         = false;     // a group move is in progress
    bool        m_drag_committed = false;
    double      m_drag_sx = 0.0, m_drag_sy = 0.0;

    // Marquee rubber-band (screen coords; live selection preview).
    bool   m_marquee = false;
    bool   m_marquee_add = false;             // Shift/Ctrl held → union onto m_sel_base
    double m_marq_x0 = 0.0, m_marq_y0 = 0.0, m_marq_x1 = 0.0, m_marq_y1 = 0.0;

    std::string m_hover_id;              // node under cursor

    // Link-drag: from-node + live cursor end (screen).
    std::string m_link_from;
    double      m_link_x = 0.0, m_link_y = 0.0;

    OpenCallback    m_on_open;
    PersistCallback m_on_persist;
    RenameCallback  m_on_rename;
    ObjectsProvider m_objects;
    NameProvider    m_name_of;
    ColorProvider   m_color_of;

    // ── Internals ──────────────────────────────────────────────────────────────
    void build_tools();
    void rebuild_chips();
    void recompute();                    // m_doc.nodes → m_placements, queue draw
    void persist();                      // mirror viewport, serialise, fire callback
    void delete_selection();             // remove every selected node (+ its edges)
    void update_mode_hint();             // banner text for the armed tool
    void refresh_selection_ui();         // selection badge + redraw

    void draw(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h);
    void zoom_to_fit();
    void zoom_about(double factor, double sx, double sy);

    void set_tool(Tool t);
    const CMMNode* node_at_id(const std::string& id) const;
    std::string    display_name(const std::string& iid) const;
    Gdk::RGBA      category_color(const std::string& category);
    Gdk::RGBA      node_tint(const CMMNode& n);   // owned colour → palette / target / default

    // Popovers (built on demand, anchored to a widget or a screen point).
    void open_object_picker(bool for_subject);
    void open_text_editor(const std::string& node_id, double sx, double sy);
    void open_category_popover(const std::string& from_id, const std::string& to_id,
                               double sx, double sy);
    void open_frame_menu();

    static void glyph_path(const Cairo::RefPtr<Cairo::Context>& cr,
                           MapGlyph g, double cx, double cy, double r);
};

}  // namespace Folio
