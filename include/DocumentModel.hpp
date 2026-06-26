#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — DocumentModel.hpp
// No GTK dependency. All data lives here; UI is wired through callbacks.
//
// The binder is six parallel trees, each a std::vector<BinderNode>, reached
// through the single door root(Section) — direct member access is closed (s22):
//   model.root(Section::Manuscript)  — Groups and Scenes
//   model.root(Section::Characters)  — Groups and Characters
//   model.root(Section::Places)      — Groups and Places (+ References/Templates/Trash)
//
// All three trees share the same BinderNode type, distinguished by `kind`.
// Path-based addressing works identically across all three sections.
// ─────────────────────────────────────────────────────────────────────────────

#include <algorithm>
#include "FolioPrefs.hpp"
#include "ObjectStore.hpp"   // s31 — objects & templates registry + instances
#include "ImagePool.hpp"     // s58 — the shared image pool (gallery data layer)
#include <chrono>
#include <ctime>
#include <fstream>
#include <functional>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <map>
#include <string>
#include <vector>

namespace Folio {

using json = nlohmann::json;

// ─────────────────────────────────────────────────────────────────────────────
// Section — which root tree a path addresses
// ─────────────────────────────────────────────────────────────────────────────

enum class Section { Manuscript, Characters, Places, References, Templates, Trash };

inline const char* section_name(Section s) {
    switch (s) {
        case Section::Manuscript:  return "Manuscript";
        case Section::Characters:  return "Characters";
        case Section::Places:      return "Places";
        case Section::References:  return "References";
        case Section::Templates:   return "Templates";
        case Section::Trash:       return "Trash";
    }
    return "";
}

// ─────────────────────────────────────────────────────────────────────────────
// BinderKind — what kind of node this is
// ─────────────────────────────────────────────────────────────────────────────

enum class BinderKind {
    Group,      // container node (valid in all sections)
    Scene,      // leaf prose node in the Manuscript section
    Character,  // leaf node in the Characters section
    Place,      // leaf node in the Places section
    Reference,  // leaf node in the References section
    Template,   // leaf node in the Templates section
};

inline std::string binder_kind_to_str(BinderKind k) {
    switch (k) {
        case BinderKind::Group:     return "group";
        case BinderKind::Character: return "character";
        case BinderKind::Place:     return "place";
        case BinderKind::Reference: return "reference";
        case BinderKind::Template:  return "template";
        default:                    return "scene";
    }
}
inline BinderKind binder_kind_from_str(const std::string& s) {
    if (s == "group")     return BinderKind::Group;
    if (s == "character") return BinderKind::Character;
    if (s == "place")     return BinderKind::Place;
    if (s == "reference") return BinderKind::Reference;
    if (s == "template")  return BinderKind::Template;
    return BinderKind::Scene;
}
inline bool binder_kind_is_group(BinderKind k) { return k == BinderKind::Group; }

inline BinderKind section_leaf_kind(Section s) {
    switch (s) {
        case Section::Characters: return BinderKind::Character;
        case Section::Places:     return BinderKind::Place;
        case Section::References: return BinderKind::Reference;
        case Section::Templates:  return BinderKind::Template;
        default:                  return BinderKind::Scene;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Enums shared across kinds
// ─────────────────────────────────────────────────────────────────────────────

enum class NodeStatus { Untitled, RoughDraft, InProgress, Polished, Skip };
enum class NodeColor  { None, Teal, Mauve, Peach, Blue, Flamingo, Green, Red, Yellow, Sky };
enum class CharacterRole { Protagonist, Antagonist, Supporting, Minor, Unknown };

inline std::string node_status_to_str(NodeStatus s) {
    switch (s) {
        case NodeStatus::RoughDraft:  return "rough_draft";
        case NodeStatus::InProgress:  return "in_progress";
        case NodeStatus::Polished:    return "polished";
        case NodeStatus::Skip:        return "skip";
        default:                      return "untitled";
    }
}
inline NodeStatus node_status_from_str(const std::string& s) {
    if (s == "rough_draft") return NodeStatus::RoughDraft;
    if (s == "in_progress") return NodeStatus::InProgress;
    if (s == "polished")    return NodeStatus::Polished;
    if (s == "skip")        return NodeStatus::Skip;
    return NodeStatus::Untitled;
}
inline std::string node_status_label(NodeStatus s) {
    switch (s) {
        case NodeStatus::RoughDraft:  return "Rough Draft";
        case NodeStatus::InProgress:  return "In Progress";
        case NodeStatus::Polished:    return "Polished";
        case NodeStatus::Skip:        return "Skip";
        default:                      return "Untitled";
    }
}

inline std::string node_color_to_str(NodeColor c) {
    switch (c) {
        case NodeColor::Teal:     return "teal";
        case NodeColor::Mauve:    return "mauve";
        case NodeColor::Peach:    return "peach";
        case NodeColor::Blue:     return "blue";
        case NodeColor::Flamingo: return "flamingo";
        case NodeColor::Green:    return "green";
        case NodeColor::Red:      return "red";
        case NodeColor::Yellow:   return "yellow";
        case NodeColor::Sky:      return "sky";
        default:                  return "none";
    }
}
inline NodeColor node_color_from_str(const std::string& s) {
    if (s == "teal")     return NodeColor::Teal;
    if (s == "mauve")    return NodeColor::Mauve;
    if (s == "peach")    return NodeColor::Peach;
    if (s == "blue")     return NodeColor::Blue;
    if (s == "flamingo") return NodeColor::Flamingo;
    if (s == "green")    return NodeColor::Green;
    if (s == "red")      return NodeColor::Red;
    if (s == "yellow")   return NodeColor::Yellow;
    if (s == "sky")      return NodeColor::Sky;
    return NodeColor::None;
}
inline const char* node_color_css(NodeColor c) {
    switch (c) {
        case NodeColor::Teal:     return "teal";
        case NodeColor::Mauve:    return "mauve";
        case NodeColor::Peach:    return "peach";
        case NodeColor::Blue:     return "blue";
        case NodeColor::Flamingo: return "flamingo";
        case NodeColor::Green:    return "green";
        case NodeColor::Red:      return "red";
        case NodeColor::Yellow:   return "yellow";
        case NodeColor::Sky:      return "sky";
        default:                  return "";
    }
}

inline std::string character_role_to_str(CharacterRole r) {
    switch (r) {
        case CharacterRole::Protagonist: return "protagonist";
        case CharacterRole::Antagonist:  return "antagonist";
        case CharacterRole::Supporting:  return "supporting";
        case CharacterRole::Minor:       return "minor";
        default:                         return "unknown";
    }
}
inline CharacterRole character_role_from_str(const std::string& s) {
    if (s == "protagonist") return CharacterRole::Protagonist;
    if (s == "antagonist")  return CharacterRole::Antagonist;
    if (s == "supporting")  return CharacterRole::Supporting;
    if (s == "minor")       return CharacterRole::Minor;
    return CharacterRole::Unknown;
}
inline std::string character_role_label(CharacterRole r) {
    switch (r) {
        case CharacterRole::Protagonist: return "Protagonist";
        case CharacterRole::Antagonist:  return "Antagonist";
        case CharacterRole::Supporting:  return "Supporting";
        case CharacterRole::Minor:       return "Minor";
        default:                         return "Unknown";
    }
}

inline int count_words(const std::string& text) {
    std::string plain;
    plain.reserve(text.size());
    bool in_tag = false;
    for (unsigned char c : text) {
        if (c == '<') { in_tag = true;  continue; }
        if (c == '>') { in_tag = false; plain += ' '; continue; }
        if (!in_tag)  plain += static_cast<char>(c);
    }
    int  count   = 0;
    bool in_word = false;
    for (unsigned char c : plain) {
        if (std::isspace(c)) { in_word = false; }
        else if (!in_word)   { in_word = true; ++count; }
    }
    return count;
}

// ─────────────────────────────────────────────────────────────────────────────
// Snapshot / Note
// ─────────────────────────────────────────────────────────────────────────────

struct Snapshot {
    std::string name;
    std::string content;
    std::string timestamp;
    json to_json() const;
    void from_json(const json& j);
};

struct Note {
    std::string title;
    std::string body;
    json to_json() const;
    void from_json(const json& j);
};

// ─────────────────────────────────────────────────────────────────────────────
// Annotation — inline comment anchored to a character range in the buffer.
// kind:  "Writer" | "Editor" | "Proofreader"
// color_hex: "#fef08a" (yellow) | "#fca5a5" (red) | "#86efac" (green)
// range_start/end: UTF-8 character offsets in the node's HTML buffer.
// ─────────────────────────────────────────────────────────────────────────────
struct Annotation {
    int         id          = 0;
    int         range_start = 0;
    int         range_end   = 0;
    std::string text;
    std::string color_hex   = "#fef08a";  // default: yellow
    std::string kind        = "Writer";
    std::string created_at;               // ISO-8601 timestamp
    json to_json() const;
    void from_json(const json& j);
};

// ─────────────────────────────────────────────────────────────────────────────
// BoardItem — identifies any selected node in any section
//
// s20: the cross-boundary selection token is now the stable iid, not a
// positional child-index path. A selected thing survives reorder/move because
// its iid is immutable; everything downstream of selection (board, grid,
// timeline tabs, persisted selection) is therefore reorder-stable for free.
// `section` is retained as the tree hint for path_for_iid() at the GTK render
// edge — the iid alone resolves the node (find_node_by_iid searches all trees),
// but the section spares a scan when a path is needed. Position is derived from
// the iid only where GTK genuinely needs a row index; it is never the identity.
// (The Sidebar keeps its own positional working set internally — see Sidebar's
//  SelPath — and mints BoardItems by iid only at its callback edge.)
// ─────────────────────────────────────────────────────────────────────────────

struct BoardItem {
    Section     section = Section::Manuscript;
    std::string iid;

    static BoardItem make(Section s, std::string id) {
        return { s, std::move(id) };
    }
    bool operator<(const BoardItem& o) const {
        if (section != o.section) return section < o.section;
        return iid < o.iid;
    }
    bool operator==(const BoardItem& o) const {
        return section == o.section && iid == o.iid;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// BinderNode — unified node for all three binder sections
//
// kind == Group     → children is the container; content is optional preface.
// kind == Scene     → children empty; content is prose.
// kind == Character → children empty; character-specific fields apply.
// kind == Place     → children empty; place-specific fields apply.
//
// RULE: Never cache a BinderNode* across any mutation of the tree.
// ─────────────────────────────────────────────────────────────────────────────

struct BinderNode {
    BinderKind  kind = BinderKind::Scene;
    int         id   = -1;
    // s19: stable cross-layer identity (model ↔ disk ↔ GTK ↔ log). Minted on
    // creation; immutable; the key the v5 bundle stores each node's content
    // under (content/<iid>.md). The int `id` above remains the runtime
    // cross-reference for the backlink index / folio-link tags this slice; the
    // positional→iid replacement of that machinery is the sequenced follow-on.
    std::string iid;

    // Universal fields
    std::string  title;
    std::string  synopsis;
    std::string  content;
    NodeColor    color              = NodeColor::None;  // kept for migration only; use color_idx
    int          color_idx          = 0;                // 0=None, 1-based into FolioPrefs::tag_colors
    bool         include_in_export = true;
    std::vector<Note>       notes;
    std::vector<Annotation> annotations;
    int                     next_annotation_id = 1; // auto-increment
    std::vector<BinderNode> children; // Groups only

    // Scene / Group fields
    NodeStatus   status             = NodeStatus::Untitled;
    std::string  pov_character_name;
    int          word_target        = 0;
    // s23: the Key Point this scene serves (the structure mark; "" = untagged).
    // Stamped by the pattern materializer, sticky through reshaping (split/
    // combine/move keep the tag). The on-ramp to the Structure pillar — the
    // coverage lens reads the body against its anatomy through this. (Was
    // "signpost_id" in the unwired StoryGraph sketch; renamed — "signpost" is
    // Bell's word, Folio uses its own "Key Point".)
    std::string  kp_id;
    // s23: human-readable KP tag, saved with the scene so it is self-describing
    // and travels in the bundle independent of the module. Shown as the Tag row
    // under Label in the inspector. kp_id is the stable machine key; kp_label is
    // the display text ("" = untagged).
    std::string  kp_label;
    // s24: per-scene structure metadata stamped at materialization (s25 model).
    // frenetic = pacing energy (KP baseline × the module pacing pattern, cycled
    // across the told-line); arc = story-arc/tension (its KP's value). Both 0..1,
    // 0 = unset. Sticky through reshaping like kp_id; the lens reads them later.
    double       frenetic           = 0.0;
    double       arc                = 0.0;
    // s29: this scene is a pinned hinge — a single-scene Key Point the author
    // reaches for and writes first; the rest of the arc is connective fill. Sticky
    // through reshaping like kp_id; surfaced as a milestone marker.
    bool         pin                = false;
    std::vector<Snapshot> snapshots;

    // Character / Place fields
    std::string   role;            // free-form role name (e.g. "Protagonist")
    std::string   description; // short one-liner
    std::string   image_path;
    // s35: the object TEMPLATE this character/place has adopted. Empty = the
    // section's built-in floor (Character/Place); a tpl_ id = a cloned, customized
    // form. The object-store projection resolves object.type from this (falling
    // back to the floor when empty / deleted / a built-in id). Serialised omit-
    // when-empty so untouched leaves and all scenes stay clean.
    std::string   template_id;

    // s38 — TEMPLATE NODES ONLY (BinderKind::Template): the form schema this
    // template defines, as a serialized Folio::Template (type_name, icon, fields[],
    // category). The binder node is the truth; rebuild_object_store projects it into
    // the ObjectStore registry under this node's iid (ObjectStore::adopt_template_
    // node). Empty on every other kind; serialised omit-when-empty so the whole tree
    // stays byte-clean. The node's own `content` buffer is the template's
    // description, distinct from the schema here.
    nlohmann::json form_schema;

    // Reference fields
    std::string   url;             // optional URL for Reference nodes

    // Trash fields — set when a node is moved to the Trash section
    std::string   trash_origin_section; // serialised section name of origin
    std::string   trash_origin_path_str; // JSON-serialised origin path (e.g. "[0,2]")

    // Editor view state — persisted so returning to a node restores position
    int    cursor_offset = 0;   // buffer char offset of insert mark
    double scroll_value  = 0.0; // vadjustment value (pixels from top)

    // Word count
    int word_count() const { return count_words(content); }
    int total_words() const {
        int t = word_count();
        for (const auto& c : children) t += c.total_words();
        return t;
    }

    // True after content has changed since the last snapshot (or since creation
    // if no snapshot exists). Cleared by save_snapshot().
    bool content_modified = false;

    void save_snapshot(const std::string& snap_name);

    json to_json() const;
    void from_json(const json& j);
};

// Collect all leaf (non-group) descendants of a node, in order.
// Groups are containers only — their own content field is not included.
inline void collect_leaf_nodes(BinderNode* node, std::vector<BinderNode*>& out) {
    if (!node) return;
    if (node->children.empty()) {
        out.push_back(node);
    } else {
        for (auto& child : node->children)
            collect_leaf_nodes(&child, out);
    }
}

// Collect nodes for Joined View, respecting sidebar collapse state:
//   - A collapsed group → include the group itself THEN all descendants
//     (regardless of how nested groups inside it are expanded — once we
//     decide to enter a collapsed group, everything inside is included)
//   - An expanded group → include only the group itself; children are not
//     auto-added (user can select them individually)
//   - A leaf (scene) → included as-is
//
// force_include_children: set to true when a collapsed ancestor has already
// decided to recurse — bypasses the expansion check for all descendants.
inline void collect_jv_nodes(
    BinderNode* node,
    Section section,
    std::vector<int> path,
    const std::function<bool(Section, const std::vector<int>&)>& is_expanded,
    std::vector<BinderNode*>& out,
    bool force_include_children = false)
{
    if (!node) return;
    if (node->children.empty()) {
        out.push_back(node);
        return;
    }
    // Group node: always include the group itself first
    out.push_back(node);
    // Expand children if this group is collapsed OR a collapsed ancestor
    // already decided to pull everything in
    bool expand = force_include_children || !is_expanded(section, path);
    if (expand) {
        for (int i = 0; i < (int)node->children.size(); ++i) {
            auto child_path = path;
            child_path.push_back(i);
            // Pass force=true so nested groups are always fully included
            collect_jv_nodes(&node->children[i], section, child_path,
                             is_expanded, out, true);
        }
    }
}

// ─── DailyRecord — one entry in the per-project writing history ───────────────
struct DailyRecord {
    std::string date;    // ISO-8601: "2025-06-15"
    int         words;   // words written during that calendar day
};

// ─── PomodoroRecord — one completed (or stopped) pomodoro phase ───────────────
struct PomodoroRecord {
    std::string date;        // ISO-8601 date: "2025-06-15"
    std::string phase;       // "Focus", "Short Break", "Long Break"
    int         duration_sec = 0;   // seconds the phase ran before completion/stop
    bool        completed    = true; // false if the user stopped early
};

// ─────────────────────────────────────────────────────────────────────────────
// DocumentModel
// ─────────────────────────────────────────────────────────────────────────────

// ─── BacklinkEntry — one incoming link recorded in the backlink index ─────────
// Records where a link to a given node originates: which node contains it,
// which paragraph anchor within that source node, and the display text.
// s20: the source is keyed by the stable iid, not the int id — so the index
// survives reorder/move and ties straight into the cross-layer iid thread.
struct BacklinkEntry {
    std::string source_iid;             // iid of the node whose content contains the link
    std::string source_anchor;          // anchor-id of the paragraph in the source (may be empty)
    std::string link_text;              // display text of the link as written
};

class DocumentModel {
public:
    // Project metadata
    std::string project_title    = "Untitled Project";
    std::string author;
    std::string genre;
    std::string project_synopsis;
    std::string publisher;
    std::string isbn;
    std::string barcode_price;    // optional price for EAN-5 supplement e.g. "2395"
    int         barcode_triangle  = 0; // 0=none 1=triangle 2=triangle+S
    std::string barcode_currency = "5"; // EAN-5 currency code
    std::string barcode_svg;    // cached EAN-13 SVG, regenerated by BarcodeDialog
    std::string year;
    std::string cover_image_path;   // original file path (may be empty/stale)
    std::string cover_thumbnail;    // base64-encoded PNG thumbnail 384×576
    int         daily_target         = 1500;
    int         session_words        = 0;
    int         project_word_target  = 80000;
    std::string due_date;                       // "YYYY-MM-DD", empty = none
    std::vector<DailyRecord>    daily_history;  // one entry per writing day
    std::vector<PomodoroRecord> pomodoro_log;   // all recorded pomodoro phases

    // ── Timeline state (saved per-project) ────────────────────────────────────
    struct SavedTab {
        Section     section = Section::Manuscript;
        std::string iid;          // s20: was a positional path — now reorder-stable
    };
    std::vector<SavedTab> open_tabs;
    int                   timeline_active_idx = -1;

    // ── Sidebar selection state (saved per-project) ───────────────────────────
    Section     sidebar_selected_section = Section::Manuscript;
    std::string sidebar_selected_iid;   // s20: was a child-index path; empty = nothing selected

    // s22: the six binder trees moved to the private section below — the body.
    // External code reaches them through the single door root(Section); direct
    // member access (model.manuscript …) no longer compiles. The members keep
    // their names so the class's own serialiser/factories/walks are unchanged.
    // (DESIGN §4.7a — one channel to the source of truth.)

    // Persistence
    bool        is_modified  = false;
    std::string current_path;

    // Active selection
    Section          active_section = Section::Manuscript;
    std::vector<int> active_path;

    BinderNode*       active_node();
    const BinderNode* active_node() const;
    void set_active(Section section, const std::vector<int>& path);

    // Resolve path in section. nullptr if invalid.
    BinderNode*       node_at(Section section, const std::vector<int>& path);
    const BinderNode* node_at(Section section, const std::vector<int>& path) const;

    // Root vector for a section
    std::vector<BinderNode>&       root(Section s);
    const std::vector<BinderNode>& root(Section s) const;

    // Callbacks
    std::function<void(BinderNode*)> on_node_changed;

    // Mutations
    void mark_modified();
    void add_session_words(int delta) { session_words = std::max(0, session_words + delta); }

    std::vector<int> add_group(Section section,
                               const std::vector<int>& parent_path,
                               const std::string& title = "New Group",
                               const NodeDefaults* defaults = nullptr);

    std::vector<int> add_leaf(Section section,
                              const std::vector<int>& parent_path,
                              const std::string& title = "",
                              const NodeDefaults* defaults = nullptr);

    void remove_node(Section section, const std::vector<int>& path);

    // Soft-delete: move node to Trash, recording its origin
    void trash_node(Section section, const std::vector<int>& path);

    // Restore: move a trash item back to a target section (appended at top level)
    void restore_node(int trash_idx, Section target_section);

    // Hard-delete all items in Trash
    void empty_trash();

    void move_node(Section section,
                   const std::vector<int>& from,
                   const std::vector<int>& to_parent, int index);

    // Find the current path of a node by its stable ID. Returns empty if not found.
    std::vector<int> path_for_id(Section section, int id) const;

    // s19: the same, by stable cross-layer iid.
    std::vector<int> path_for_iid(Section section, const std::string& iid) const;

    // Word count (manuscript only)
    int total_words() const {
        int t = 0;
        for (const auto& n : manuscript) t += n.total_words();
        return t;
    }

    // Flat list entry used by SnapshotDialog
    struct NodeRef {
        BinderNode* node    = nullptr;
        Section     section = Section::Manuscript;
        std::vector<int> path;
    };

    // Returns a flat list of every leaf and group node across all three trees.
    std::vector<NodeRef> collect_all_nodes();

    // Const flat list of every node POINTER across the five trees (Trash
    // excluded), mirroring collect_all_nodes() for read-only callers — notably
    // the StoryGraph edge projection (const), which scans instrument node bodies
    // for their object associations. Do not cache across tree mutations.
    std::vector<const BinderNode*> all_node_ptrs() const;

    // Manuscript nodes (groups + scenes) in reading/DFS order, as a flat list
    // of mutable pointers. Unlike compile_nodes(), this does NOT filter on
    // include_in_export — navigation (e.g. FocusWindow's switcher / next-prev)
    // must be able to reach a scene the author has excluded from export. The
    // model owns reading order; callers ask for it rather than walking the raw
    // vectors. Do not cache across tree mutations.
    std::vector<BinderNode*> manuscript_in_reading_order();

    // File I/O
    void save();
    void save_to(const std::string& path);
    void load_from(const std::string& path);
    void reset();

    // s19: human-readable outcome of the last bundle reconcile (empty/"clean"
    // on a clean load; otherwise e.g. "1 missing, 2 orphaned"). For the status
    // bar / log — recovery UI is a follow-on.
    std::string last_load_report;

    // Find any node by its stable integer ID, searching all sections.
    // Returns nullptr if not found.  Do not cache across tree mutations.
    BinderNode* find_node_by_id(int id);
    const BinderNode* find_node_by_id(int id) const;

    // s19: the same, by stable cross-layer iid. The iid survives reorder/move
    // (unlike the positional path) and is the durable runtime handle the
    // GTK-naming / log discipline keys off. Do not cache across mutations.
    BinderNode*       find_node_by_iid(const std::string& iid);
    const BinderNode* find_node_by_iid(const std::string& iid) const;

    // s44 §11 — clear the explicit-default flag on Template nodes of `category`
    // except keep_iid (one default per category). Returns the count cleared.
    int clear_default_template_for_category(const std::string& category,
                                            const std::string& keep_iid);

    // ── Objects & templates (s31) ─────────────────────────────────────────────
    // The object store is the durable form of characters/places (and any future
    // user-defined type). This slice keeps it a PROJECTION of the live binder
    // leaves: rebuilt from them at save time, serialised in the project blob,
    // read back / migrated at load. The form renderer (next) reads this store;
    // when it can edit objects, the binder leaves retire. See ObjectStore.hpp.
    const ObjectStore& object_store() const { return m_object_store; }
    ObjectStore&       object_store()       { return m_object_store; }
    // Rebuild the store from the current Characters/Places binder leaves (seeds
    // the built-in templates, then migrates every leaf to an Object). Called at
    // save time and on loading a legacy project that has no stored object_store.
    void rebuild_object_store();

    // s58 — the shared image POOL. Owns every image-fragment (the Gallery is a
    // lens over this, never an owner). Persisted at the PROJECT level as the
    // manifest's "images" array — shared across all gallery lenses — and read
    // back at load. Unlike the object store it is NOT a projection of leaves; it
    // is owned data, mutated by the import pipeline. Asset file integrity
    // (missing/drift/orphan) is reported through the load reconcile (§9).
    const ImagePool& image_pool() const { return m_image_pool; }
    ImagePool&       image_pool()       { return m_image_pool; }

    // Backlink index — maps target node IID → list of incoming link locations.
    // s20: keyed by the stable iid (was the int id), so it survives reorder/move
    // and joins the cross-layer iid thread. Rebuilt on project load; updated
    // incrementally when node content changes.
    const std::map<std::string, std::vector<BacklinkEntry>>& backlinks() const { return m_backlinks; }
    void rebuild_backlink_index();
    void update_backlinks_for_node(const std::string& node_iid, const std::string& html);

    // Helper: parse all folio-link tags from an HTML string. Returns
    // (target_iid, anchor_id) pairs. Public so free functions in
    // DocumentModel.cpp can call it.
    static std::vector<std::pair<std::string,std::string>> extract_links_from_html(const std::string& html);

    // ID allocation — globally unique across all three trees
    int next_id() const;

    // Compile (manuscript only, include_in_export == true)
    std::vector<const BinderNode*> compile_nodes() const;

    // Factory
    static DocumentModel new_project(const std::string& title = "Untitled Project");
    static DocumentModel make_demo();

private:
    // ── The body: the six binder trees (s22 — private behind root(Section)) ────
    // The one ordered thing the lenses project (DESIGN §4.6). Reached only via
    // root(Section); the named members serve the class's own internals. When the
    // body is later extracted into its own Binder class, these six + the body
    // ops + the graph index move together — the access door (root) stays put.
    std::vector<BinderNode> manuscript;  // Groups and Scenes
    std::vector<BinderNode> characters;  // Groups and Characters
    std::vector<BinderNode> places;      // Groups and Places
    std::vector<BinderNode> references;  // research & web links
    std::vector<BinderNode> templates;   // content templates
    std::vector<BinderNode> trash;       // soft-deleted items

    // s31: objects & templates store — the durable/serialised form of the
    // character/place leaves (projection this slice; editable surface later).
    ObjectStore m_object_store;

    // s58: the shared image pool — owned image-fragments (assets/<iid>.<ext>),
    // serialised as the manifest's "images" array. The Gallery reads it as a lens.
    ImagePool m_image_pool;

    // s19: parse a full in-memory project blob (the shape save_to builds and
    // load paths reassemble) into the model. load_from obtains the blob (from a
    // v5 bundle via implode, or a legacy file via migrate_v4) then calls this.
    void parse_blob(const json& j);

    void collect_compile_nodes(const BinderNode& node,
                               std::vector<const BinderNode*>& out) const;
    int  scan_max_id(const BinderNode& node) const;

    // Backlink index storage — keyed by target node iid (s20)
    std::map<std::string, std::vector<BacklinkEntry>> m_backlinks;
};

} // namespace Folio
