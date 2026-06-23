// ─────────────────────────────────────────────────────────────────────────────
// CustomMindMap.cpp — the owned mind-map document (s50). Pure / GTK-free.
//
// Novel logic lives here (construction, subjects, category recents, the frame
// STAMP, and the json round-trip); the GTK surface is a thin renderer that calls
// it. No geometry beyond placing a frame's fan — and that is owned coordinates,
// not a live rule. See header for the lens-vs-document line.
// ─────────────────────────────────────────────────────────────────────────────
#include "CustomMindMap.hpp"

#include <algorithm>
#include <cmath>
#include <random>

namespace Folio {

namespace {

// Crockford base32 minus the ambiguous set (i, l, o, u) — matches Iid.cpp so a
// doc-local id reads the same as a project iid (just a different, non-IidKind
// prefix). These ids never leave the document, so a local generator is correct:
// we are not minting a project part.
constexpr char B32[]  = "0123456789abcdefghjkmnpqrstvwxyz";
constexpr int  B32_N  = 32;

std::mt19937_64& rng() {
    static thread_local std::mt19937_64 gen(std::random_device{}());
    return gen;
}

std::string local_id(const char* prefix, int entropy = 8) {
    std::string s = prefix;
    s += '_';
    std::uniform_int_distribution<int> d(0, B32_N - 1);
    for (int i = 0; i < entropy; ++i) s += B32[d(rng())];
    return s;
}

}  // namespace

// ── Glyph (presentation; reuse the shared vocabulary) ────────────────────────
MapGlyph cmm_node_glyph(const CMMNode& n) {
    if (n.kind == CMMNodeKind::Anchor && !n.iid.empty())
        return map_glyph_for(iid_kind_of(n.iid));   // borrow the target's kind shape
    return MapGlyph::Card;                            // a Text node is a card
}

// ── Doc-local id minting ─────────────────────────────────────────────────────
std::string cmm_make_node_id() { return local_id("cmn"); }
std::string cmm_make_edge_id() { return local_id("cme"); }

// ── Construction helpers ─────────────────────────────────────────────────────
std::string cmm_add_text(CMMDoc& d, const std::string& title, double x, double y,
                         const std::string& body) {
    CMMNode n;
    n.id = cmm_make_node_id();
    n.kind = CMMNodeKind::Text;
    n.x = x; n.y = y;
    n.title = title;
    n.body  = body;
    d.nodes.push_back(std::move(n));
    return d.nodes.back().id;
}

std::string cmm_add_anchor(CMMDoc& d, const std::string& target_iid,
                           const std::string& cached_title, double x, double y) {
    CMMNode n;
    n.id = cmm_make_node_id();
    n.kind = CMMNodeKind::Anchor;
    n.x = x; n.y = y;
    n.iid   = target_iid;
    n.title = cached_title;
    d.nodes.push_back(std::move(n));
    return d.nodes.back().id;
}

std::string cmm_add_edge(CMMDoc& d, const std::string& from, const std::string& to,
                         const std::string& category, bool directed) {
    if (from.empty() || to.empty() || from == to)   return "";
    if (!cmm_find_node(d, from) || !cmm_find_node(d, to)) return "";
    CMMEdge e;
    e.id = cmm_make_edge_id();
    e.from = from; e.to = to;
    e.category = category;
    e.directed = directed;
    d.edges.push_back(std::move(e));
    if (!category.empty()) cmm_note_category(d, category);
    return d.edges.back().id;
}

const CMMNode* cmm_find_node(const CMMDoc& d, const std::string& id) {
    for (const CMMNode& n : d.nodes) if (n.id == id) return &n;
    return nullptr;
}

// ── Subjects ─────────────────────────────────────────────────────────────────
bool cmm_has_subject(const CMMDoc& d, const std::string& iid) {
    return std::find(d.subject_iids.begin(), d.subject_iids.end(), iid)
           != d.subject_iids.end();
}

bool cmm_add_subject(CMMDoc& d, const std::string& iid) {
    if (iid.empty() || cmm_has_subject(d, iid)) return false;
    d.subject_iids.push_back(iid);
    return true;
}

bool cmm_remove_subject(CMMDoc& d, const std::string& iid) {
    auto it = std::find(d.subject_iids.begin(), d.subject_iids.end(), iid);
    if (it == d.subject_iids.end()) return false;
    d.subject_iids.erase(it);
    return true;
}

// ── Category recents (front, dedupe, cap — the colour-recents idiom) ─────────
void cmm_note_category(CMMDoc& d, const std::string& category) {
    if (category.empty()) return;
    auto& r = d.recent_categories;
    r.erase(std::remove(r.begin(), r.end(), category), r.end());
    r.insert(r.begin(), category);
    if (static_cast<int>(r.size()) > kCmmRecentCap) r.resize(kCmmRecentCap);
}

// ── Frame stamp ──────────────────────────────────────────────────────────────
std::string cmm_stamp_frame(CMMDoc& d, const MindMapFrame& frame,
                            double cx, double cy, double ring,
                            const std::string& subject_iid,
                            const std::string& center_label) {
    // Centre: an Anchor on the subject (and register it on the doc) or a Text node.
    std::string center_id;
    if (!subject_iid.empty()) {
        cmm_add_subject(d, subject_iid);   // the map is now "about" this object
        center_id = cmm_add_anchor(d, subject_iid, center_label, cx, cy);
    } else {
        center_id = cmm_add_text(d, center_label, cx, cy);
    }

    // Slots fanned evenly around the ring, starting at the top, clockwise. Each is
    // a Text node labelled from the slot, linked to the centre by a structural
    // (uncategorised) edge — the skeleton the author hangs snippets off.
    const std::size_t n = frame.slots.size();
    for (std::size_t i = 0; i < n; ++i) {
        const double ang = -M_PI / 2.0 + (2.0 * M_PI * static_cast<double>(i)
                                          / static_cast<double>(n == 0 ? 1 : n));
        const double sx = cx + ring * std::cos(ang);
        const double sy = cy + ring * std::sin(ang);
        const std::string sid = cmm_add_text(d, frame.slots[i].label, sx, sy);
        cmm_add_edge(d, center_id, sid, /*category=*/"", /*directed=*/false);
    }
    return center_id;
}

// ── Round-trip ───────────────────────────────────────────────────────────────
namespace {

const char* kind_token(CMMNodeKind k) { return k == CMMNodeKind::Anchor ? "anchor" : "text"; }
CMMNodeKind kind_from(const std::string& s) {
    return s == "anchor" ? CMMNodeKind::Anchor : CMMNodeKind::Text;
}

json node_to_json(const CMMNode& n) {
    json j = {
        { "id",    n.id },
        { "kind",  kind_token(n.kind) },
        { "x",     n.x },
        { "y",     n.y },
        { "title", n.title },
    };
    if (!n.body.empty())  j["body"] = n.body;
    if (n.kind == CMMNodeKind::Anchor) j["iid"] = n.iid;   // the bridge to truth
    if (n.pinned)    j["pinned"]    = true;
    if (n.collapsed) j["collapsed"] = true;
    return j;
}

CMMNode node_from_json(const json& j) {
    CMMNode n;
    n.id    = j.value("id", std::string());
    n.kind  = kind_from(j.value("kind", std::string("text")));
    n.x     = j.value("x", 0.0);
    n.y     = j.value("y", 0.0);
    n.title = j.value("title", std::string());
    n.body  = j.value("body",  std::string());
    n.iid   = j.value("iid",   std::string());
    n.pinned    = j.value("pinned",    false);
    n.collapsed = j.value("collapsed", false);
    return n;
}

json edge_to_json(const CMMEdge& e) {
    json j = {
        { "id",   e.id },
        { "from", e.from },
        { "to",   e.to },
    };
    if (!e.category.empty()) j["category"] = e.category;
    if (e.directed)          j["directed"] = true;
    return j;
}

CMMEdge edge_from_json(const json& j) {
    CMMEdge e;
    e.id       = j.value("id", std::string());
    e.from     = j.value("from", std::string());
    e.to       = j.value("to", std::string());
    e.category = j.value("category", std::string());
    e.directed = j.value("directed", false);
    return e;
}

}  // namespace

json cmm_to_json(const CMMDoc& d) {
    json nodes = json::array();
    for (const CMMNode& n : d.nodes) nodes.push_back(node_to_json(n));
    json edges = json::array();
    for (const CMMEdge& e : d.edges) edges.push_back(edge_to_json(e));

    return json{
        { "v",        1 },
        { "id",       d.id },
        { "name",     d.name },
        { "subjects", d.subject_iids },
        { "nodes",    nodes },
        { "edges",    edges },
        { "recent_categories", d.recent_categories },
        { "viewport", { { "pan_x", d.viewport.pan_x },
                        { "pan_y", d.viewport.pan_y },
                        { "zoom",  d.viewport.zoom } } },
    };
}

CMMDoc cmm_from_json(const json& j) {
    CMMDoc d;
    d.id   = j.value("id", std::string());
    d.name = j.value("name", std::string());

    if (j.contains("subjects") && j["subjects"].is_array())
        for (const auto& s : j["subjects"])
            if (s.is_string()) d.subject_iids.push_back(s.get<std::string>());

    if (j.contains("nodes") && j["nodes"].is_array())
        for (const auto& nj : j["nodes"]) d.nodes.push_back(node_from_json(nj));

    if (j.contains("edges") && j["edges"].is_array())
        for (const auto& ej : j["edges"]) d.edges.push_back(edge_from_json(ej));

    if (j.contains("recent_categories") && j["recent_categories"].is_array())
        for (const auto& c : j["recent_categories"])
            if (c.is_string()) d.recent_categories.push_back(c.get<std::string>());

    if (j.contains("viewport") && j["viewport"].is_object()) {
        const json& v = j["viewport"];
        d.viewport.pan_x = v.value("pan_x", 0.0);
        d.viewport.pan_y = v.value("pan_y", 0.0);
        d.viewport.zoom  = v.value("zoom",  1.0);
    }
    return d;
}

std::string cmm_to_string(const CMMDoc& d, bool pretty) {
    return cmm_to_json(d).dump(pretty ? 2 : -1);
}

CMMDoc cmm_from_string(const std::string& text) {
    return cmm_from_json(json::parse(text));
}

}  // namespace Folio
