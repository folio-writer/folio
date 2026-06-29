#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// MindMapCanvas.hpp — the FOURTH LENS, GTK side (s48).
//
// A thin painter over the pure MindMap layer. It owns NO layout, coordinate, or
// hit logic — every such decision is already in MindMap.{hpp,cpp} (reflow / frame
// / world_to_screen / hit_test), proven in the sandbox. This widget only:
//   • builds MindMapItems from the binder + edges from StoryGraph (read, never
//     owned — rebuilt live each time the lens opens),
//   • paints each Placement glyph at world_to_screen under the current viewport,
//   • forwards pointer input through hit_test, and
//   • pans / zooms / frames-all (zoom-to-fit, the "I'm lost" recovery).
//
// Discipline (HANDOFF §3): pure logic, thin GTK. If a geometry question arises,
// it is answered in the pure unit and CALLED here, not re-implemented. The map is
// an Editor VIEW MODE (a projection of the one graph alongside write/board/
// outline), so the Editor hosts this in its view-stack as the "map" child.
//
// s48 slice 1 is READ-ONLY navigation: render placements → pan/zoom/fit →
// click-open. The two authoring verbs (double-click-empty → create a Reference
// there; drag node→node → author a relation edge) are slice 2 — they mutate the
// model and get their own cut.
// ─────────────────────────────────────────────────────────────────────────────

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <gtkmm.h>

#include "MindMap.hpp"
#include "StoryGraph.hpp"   // StoryEdge — the connection layer (read, not owned)
#include "TimelineThreads.hpp"   // s87 — ThreadLane (the Story Threads cluster source)

namespace Folio {

class DocumentModel;
class FolioPrefs;   // s48 — label-colour palette for owner-coloured edges

class MindMapCanvas : public Gtk::Box {
public:
    MindMapCanvas(DocumentModel& model, FolioPrefs& prefs);

    // Rebuild the lens from the model: items from the binder, edges from the
    // StoryGraph reading of the s20 link + s44 relation indices. Cheap; called
    // every time the Editor switches INTO Map view (the model is the truth, this
    // is a projection — never cached across a mutation).
    void rebuild();

    // Fired when the author activates a node glyph. The Editor wires this to the
    // app-wide navigate path (switch to Write + select the node), so opening a
    // node from the map behaves exactly like opening it from the sidebar.
    using OpenCallback = std::function<void(const std::string& iid)>;
    void set_open_callback(OpenCallback cb) { m_on_open = std::move(cb); }

    // s48 slice 2 — fired on double-click of empty canvas: "author a Reference
    // HERE". The owner (MainWindow, via the Editor) creates a real Reference node
    // and returns its iid; the canvas then places it PINNED at the drop point.
    // Returns "" if creation failed (the canvas places nothing).
    using CreateCallback = std::function<std::string(double world_x, double world_y)>;
    void set_create_callback(CreateCallback cb) { m_on_create = std::move(cb); }

private:
    DocumentModel& m_model;
    FolioPrefs&    m_prefs;   // label-colour palette for owner-coloured edges

    // ── Widget tree ──────────────────────────────────────────────────────────
    Gtk::Overlay     m_overlay;
    Gtk::DrawingArea m_area;
    Gtk::Button      m_fit_btn;       // ⤢ zoom-to-fit (frame all placements)
    Gtk::Label       m_empty_hint;    // shown when there is nothing to place

    // ── Lens state (presentation only; the model stays the truth) ────────────
    MapViewport                            m_vp;          // pan + zoom
    std::vector<MindMapItem>               m_items;       // placed nodes (layout)
    std::vector<StoryEdge>                 m_edges;       // read from StoryGraph
    std::vector<ReflowRule>                m_rules;       // default: lane.kind on
    std::vector<MindMapLayout::Placement>  m_placements;  // last reflow (draw+hit)

    // s87 — the Story Threads cluster. Lanes come from assemble_thread_lanes (the
    // SAME pure source the Timeline uses), rebuilt each rebuild(); the claim map is
    // thread iid → # of on-spine scenes it holds, for the hover read-out.
    std::vector<ThreadLane>             m_thread_lanes;
    std::unordered_map<std::string, int> m_thread_claims;

    // s48 — effective node colour: a Scene wears its KP/label colour, a Chapter/
    // Part the BLEND (RGB average) of its descendant scenes' colours, an entity its
    // own label colour. Absent → fall back to the kind tint. Recomputed on rebuild
    // (depends only on the tree + label colours, not the viewport).
    std::unordered_map<std::string, Gdk::RGBA> m_color;

    // s48 — for cloud hulls: each container's descendant iids (incl. self), so the
    // canvas can draw a soft boundary enclosing the whole subtree. Containers only.
    std::unordered_map<std::string, std::vector<std::string>> m_descendants;

    // ── Interaction state ────────────────────────────────────────────────────
    bool   m_space_held  = false;   // space = pan modifier (Scapple/Figma idiom)
    bool   m_panning     = false;   // a pan drag is in progress
    bool   m_did_pan     = false;   // suppress the click-open that ends a drag
    double m_pan_base_x  = 0.0;     // viewport pan at drag-begin
    double m_pan_base_y  = 0.0;
    double m_ptr_x       = 0.0;     // last pointer pos (anchors ctrl-scroll zoom)
    double m_ptr_y       = 0.0;
    bool   m_fit_pending = true;    // frame-all once on first real allocation

    // s48 slice 2 — drag-to-move. A primary drag that begins ON a node grabs it;
    // the node is lifted from its (possibly rule-driven) spot, pinned, and tracks
    // the cursor. "" = the drag is a pan, not a move.
    std::string m_drag_iid;
    double      m_move_base_wx = 0.0;   // grabbed node's world pos at drag-begin
    double      m_move_base_wy = 0.0;
    bool        m_drag_committed = false;  // a drag only acts after passing the move threshold
    double      m_drag_sx = 0.0, m_drag_sy = 0.0;  // drag start (for deferred hit-test)

    // s48 — hover: the node under the cursor (\"\" = none). Drives the floating
    // metadata card and the relationship-thread highlight (its edges brighten, the
    // rest of the web dims — read one node's connections without the tangle).
    std::string m_hover_iid;

    OpenCallback   m_on_open;
    CreateCallback m_on_create;

    // ── Internals ────────────────────────────────────────────────────────────
    void   recompute();   // run reflow → m_placements, then queue a redraw
    void   draw(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h);
    void   draw_hover_card(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h);
    void   zoom_to_fit(); // set pan/zoom so every placement is comfortably framed
    void   zoom_about(double factor, double sx, double sy); // keep (sx,sy) fixed

    std::string node_title(const std::string& iid) const;

    static void   glyph_path(const Cairo::RefPtr<Cairo::Context>& cr,
                             MapGlyph g, double cx, double cy, double r);
};

}  // namespace Folio
