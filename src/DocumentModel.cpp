// ─────────────────────────────────────────────────────────────────────────────
// Folio — DocumentModel.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "DocumentModel.hpp"
#include "Iid.hpp"
#include "ProjectBundle.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace Folio {

// s19: which iid kind a binder node mints under.
static IidKind iid_kind_for(BinderKind k) {
    switch (k) {
        case BinderKind::Group:     return IidKind::Group;
        case BinderKind::Character: return IidKind::Character;
        case BinderKind::Place:     return IidKind::Place;
        case BinderKind::Reference: return IidKind::Reference;
        case BinderKind::Template:  return IidKind::Template;
        default:                    return IidKind::Scene;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Timestamp helper
// ─────────────────────────────────────────────────────────────────────────────

static std::string now_iso8601() {
    auto now  = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
    gmtime_r(&time, &tm_buf);
    std::ostringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// Snapshot
// ─────────────────────────────────────────────────────────────────────────────

json Snapshot::to_json() const {
    return { {"name", name}, {"content", content}, {"timestamp", timestamp} };
}
void Snapshot::from_json(const json& j) {
    name      = j.value("name",      "");
    content   = j.value("content",   "");
    timestamp = j.value("timestamp", j.value("date_str", ""));
}

// ─────────────────────────────────────────────────────────────────────────────
// Note
// ─────────────────────────────────────────────────────────────────────────────

json Note::to_json() const {
    return { {"title", title}, {"body", body} };
}
void Note::from_json(const json& j) {
    if (j.contains("title") || j.contains("body")) {
        title = j.value("title", "");
        body  = j.value("body",  "");
    } else {
        body  = j.value("text", "");
        title = j.value("date_str", "");
    }
}

json Annotation::to_json() const {
    return {
        {"id",          id},
        {"range_start", range_start},
        {"range_end",   range_end},
        {"text",        text},
        {"color_hex",   color_hex},
        {"kind",        kind},
        {"created_at",  created_at}
    };
}
void Annotation::from_json(const json& j) {
    id          = j.value("id",          0);
    range_start = j.value("range_start", 0);
    range_end   = j.value("range_end",   0);
    text        = j.value("text",        "");
    color_hex   = j.value("color_hex",   "#fef08a");
    kind        = j.value("kind",        "Writer");
    created_at  = j.value("created_at",  "");
}

// ─────────────────────────────────────────────────────────────────────────────
// BinderNode — save_snapshot
// ─────────────────────────────────────────────────────────────────────────────

void BinderNode::save_snapshot(const std::string& snap_name) {
    Snapshot s;
    s.name      = snap_name;
    s.content   = content;
    s.timestamp = now_iso8601();
    snapshots.push_back(std::move(s));
    content_modified = false;  // content is now snapshotted
}

// ─────────────────────────────────────────────────────────────────────────────
// BinderNode — serialization
// ─────────────────────────────────────────────────────────────────────────────

json BinderNode::to_json() const {
    json j;
    j["kind"]               = binder_kind_to_str(kind);
    j["id"]                 = id;
    j["iid"]                = iid;   // s19: stable cross-layer identity
    j["title"]              = title;
    j["synopsis"]           = synopsis;
    j["content"]            = content;
    j["color"]              = node_color_to_str(NodeColor::None); // legacy compat placeholder
    j["color_idx"]          = color_idx;
    j["include_in_export"] = include_in_export;
    j["status"]             = node_status_to_str(status);
    j["pov_character_name"] = pov_character_name;
    j["word_target"]        = word_target;
    j["role"]               = role;
    j["description"]        = description;
    j["image_path"]         = image_path;
    j["url"]                = url;
    if (!trash_origin_section.empty())
        j["trash_origin_section"]  = trash_origin_section;
    if (!trash_origin_path_str.empty())
        j["trash_origin_path_str"] = trash_origin_path_str;
    if (cursor_offset > 0)
        j["cursor_offset"] = cursor_offset;
    if (scroll_value > 0.0)
        j["scroll_value"]  = scroll_value;

    json snap_arr = json::array();
    for (const auto& s : snapshots) snap_arr.push_back(s.to_json());
    j["snapshots"] = snap_arr;

    json note_arr = json::array();
    for (const auto& n : notes) note_arr.push_back(n.to_json());
    j["notes"] = note_arr;

    json ann_arr = json::array();
    for (const auto& a : annotations) ann_arr.push_back(a.to_json());
    j["annotations"]        = ann_arr;
    j["next_annotation_id"] = next_annotation_id;

    json child_arr = json::array();
    for (const auto& c : children) child_arr.push_back(c.to_json());
    j["children"] = child_arr;

    return j;
}

void BinderNode::from_json(const json& j) {
    kind               = binder_kind_from_str(j.value("kind", "scene"));
    id                 = j.value("id", -1);
    iid                = j.value("iid", "");   // s19: empty on pre-iid (v4) data
    title              = j.value("title", j.value("name", "")); // compat: old char/place used "name"
    synopsis           = j.value("synopsis", "");
    content            = j.value("content", "");
    // color_idx: new format. Migrate from legacy "color" name if absent.
    if (j.contains("color_idx")) {
        color_idx = j.value("color_idx", 0);
    } else {
        // Legacy migration: map old NodeColor name → default palette index (1-based)
        // Default palette order in FolioPrefs matches: teal=1,yellow=2,red=3,green=4,mauve=5,peach=6,sky=7
        std::string cname = j.value("color", "none");
        static const std::pair<const char*,int> kLegacy[] = {
            {"teal",1},{"yellow",2},{"red",3},{"green",4},
            {"mauve",5},{"peach",6},{"sky",7},{"blue",0},{"flamingo",0}
        };
        color_idx = 0;
        for (const auto& p : kLegacy)
            if (cname == p.first) { color_idx = p.second; break; }
    }
    color = NodeColor::None; // field kept for compat but unused
    // Accept both new name and legacy "include_in_compile" key from older saves
    include_in_export = j.contains("include_in_export")
                        ? j.value("include_in_export", true)
                        : j.value("include_in_compile", true);
    status             = node_status_from_str(j.value("status", "untitled"));
    pov_character_name = j.value("pov_character_name", "");
    word_target        = j.value("word_target", 0);
    {   // Migrate legacy enum role strings to display names
        std::string r = j.value("role", "");
        if      (r == "protagonist") role = "Protagonist";
        else if (r == "antagonist")  role = "Antagonist";
        else if (r == "supporting")  role = "Supporting";
        else if (r == "minor")       role = "Minor";
        else if (r == "unknown" || r.empty()) role = "";
        else    role = r; // already a display-name string
    }
    description        = j.value("description", "");
    image_path         = j.value("image_path",  "");
    url                = j.value("url",          "");
    trash_origin_section  = j.value("trash_origin_section",  "");
    trash_origin_path_str = j.value("trash_origin_path_str", "");
    cursor_offset = j.value("cursor_offset", 0);
    scroll_value  = j.value("scroll_value",  0.0);

    snapshots.clear();
    if (j.contains("snapshots") && j["snapshots"].is_array())
        for (const auto& s : j["snapshots"]) { Snapshot snap; snap.from_json(s); snapshots.push_back(std::move(snap)); }

    notes.clear();
    if (j.contains("notes") && j["notes"].is_array())
        for (const auto& n : j["notes"]) { Note note; note.from_json(n); notes.push_back(std::move(note)); }

    annotations.clear();
    if (j.contains("annotations") && j["annotations"].is_array())
        for (const auto& a : j["annotations"]) {
            Annotation ann; ann.from_json(a); annotations.push_back(std::move(ann));
        }
    next_annotation_id = j.value("next_annotation_id", 1);
    // Ensure next_id is always > any existing id
    for (const auto& a : annotations)
        if (a.id >= next_annotation_id) next_annotation_id = a.id + 1;
    // compat: old Character/Place used "item_notes"
    if (j.contains("item_notes") && j["item_notes"].is_array())
        for (const auto& n : j["item_notes"]) { Note note; note.from_json(n); notes.push_back(std::move(note)); }

    children.clear();
    if (j.contains("children") && j["children"].is_array())
        for (const auto& c : j["children"]) { BinderNode child; child.from_json(c); children.push_back(std::move(child)); }
}

// ─────────────────────────────────────────────────────────────────────────────
// DocumentModel — root access
// ─────────────────────────────────────────────────────────────────────────────

std::vector<BinderNode>& DocumentModel::root(Section s) {
    switch (s) {
        case Section::Characters: return characters;
        case Section::Places:     return places;
        case Section::References: return references;
        case Section::Templates:  return templates;
        case Section::Trash:      return trash;
        default:                  return manuscript;
    }
}
const std::vector<BinderNode>& DocumentModel::root(Section s) const {
    return const_cast<DocumentModel*>(this)->root(s);
}

// ─────────────────────────────────────────────────────────────────────────────
// DocumentModel — path resolution
// ─────────────────────────────────────────────────────────────────────────────

BinderNode* DocumentModel::node_at(Section section, const std::vector<int>& path) {
    if (path.empty()) return nullptr;
    auto& vec = root(section);
    int top = path[0];
    if (top < 0 || top >= (int)vec.size()) return nullptr;
    BinderNode* node = &vec[top];
    for (size_t i = 1; i < path.size(); ++i) {
        int idx = path[i];
        if (idx < 0 || idx >= (int)node->children.size()) return nullptr;
        node = &node->children[idx];
    }
    return node;
}
const BinderNode* DocumentModel::node_at(Section section, const std::vector<int>& path) const {
    return const_cast<DocumentModel*>(this)->node_at(section, path);
}

BinderNode* DocumentModel::active_node() {
    return node_at(active_section, active_path);
}
const BinderNode* DocumentModel::active_node() const {
    return node_at(active_section, active_path);
}

// ─────────────────────────────────────────────────────────────────────────────
// DocumentModel — active selection
// ─────────────────────────────────────────────────────────────────────────────

void DocumentModel::set_active(Section section, const std::vector<int>& path) {
    active_section = section;
    active_path    = path;
    if (on_node_changed)
        on_node_changed(active_node());
}

// ─────────────────────────────────────────────────────────────────────────────
// DocumentModel — mark_modified
// ─────────────────────────────────────────────────────────────────────────────

void DocumentModel::mark_modified() { is_modified = true; }

// ─────────────────────────────────────────────────────────────────────────────
// DocumentModel — collect_all_nodes
// ─────────────────────────────────────────────────────────────────────────────

static void collect_nodes_recursive(std::vector<BinderNode>& vec,
                                    Section section,
                                    std::vector<int> path,
                                    std::vector<DocumentModel::NodeRef>& out) {
    for (int i = 0; i < (int)vec.size(); ++i) {
        auto cur_path = path;
        cur_path.push_back(i);
        out.push_back({ &vec[i], section, cur_path });
        if (!vec[i].children.empty())
            collect_nodes_recursive(vec[i].children, section, cur_path, out);
    }
}

std::vector<DocumentModel::NodeRef> DocumentModel::collect_all_nodes() {
    std::vector<NodeRef> out;
    collect_nodes_recursive(manuscript,  Section::Manuscript,  {}, out);
    collect_nodes_recursive(characters,  Section::Characters,  {}, out);
    collect_nodes_recursive(places,      Section::Places,      {}, out);
    collect_nodes_recursive(references,  Section::References,  {}, out);
    collect_nodes_recursive(templates,   Section::Templates,   {}, out);
    // Note: Trash is intentionally excluded from the global node list
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// DocumentModel — add helpers
// ─────────────────────────────────────────────────────────────────────────────

static std::vector<BinderNode>*
children_of(DocumentModel& model, Section section, const std::vector<int>& parent_path) {
    if (parent_path.empty()) return &model.root(section);
    BinderNode* parent = model.node_at(section, parent_path);
    if (!parent) return nullptr;
    return &parent->children;
}

std::vector<int> DocumentModel::add_group(Section section,
                                          const std::vector<int>& parent_path,
                                          const std::string& title,
                                          const NodeDefaults* defaults) {
    auto* vec = children_of(*this, section, parent_path);
    if (!vec) throw std::runtime_error("add_group: invalid parent path");

    BinderNode g;
    g.kind  = BinderKind::Group;
    g.id    = next_id();
    g.iid   = make_iid(iid_kind_for(g.kind));   // s19
    if (defaults && !defaults->title.empty())
        g.title = defaults->title;
    else
        g.title = title;
    if (defaults) {
        g.color_idx        = defaults->color_idx;
        g.include_in_export = defaults->include_in_export;
        if (!defaults->status_name.empty()) {
            // Match display name to NodeStatus
            std::string lo = defaults->status_name;
            std::transform(lo.begin(), lo.end(), lo.begin(), ::tolower);
            if      (lo == "rough draft") g.status = NodeStatus::RoughDraft;
            else if (lo == "in progress") g.status = NodeStatus::InProgress;
            else if (lo == "polished")    g.status = NodeStatus::Polished;
            else if (lo == "skip")        g.status = NodeStatus::Skip;
        }
    }
    vec->push_back(std::move(g));
    mark_modified();

    std::vector<int> new_path = parent_path;
    new_path.push_back((int)vec->size() - 1);
    return new_path;
}

std::vector<int> DocumentModel::add_leaf(Section section,
                                         const std::vector<int>& parent_path,
                                         const std::string& title,
                                         const NodeDefaults* defaults) {
    auto* vec = children_of(*this, section, parent_path);
    if (!vec) throw std::runtime_error("add_leaf: invalid parent path");

    BinderNode n;
    n.kind  = section_leaf_kind(section);
    n.id    = next_id();
    n.iid   = make_iid(iid_kind_for(n.kind));   // s19

    if (defaults && !defaults->title.empty())
        n.title = defaults->title;
    else
        n.title = title.empty() ? (section == Section::Manuscript ? "New Scene"
                                  : section == Section::Characters ? "New Character"
                                  : section == Section::Places     ? "New Place"
                                  : section == Section::References ? "New Reference"
                                  : section == Section::Templates  ? "New Template"
                                  : "Untitled")
                                : title;

    if (defaults) {
        n.color_idx         = defaults->color_idx;
        n.include_in_export = defaults->include_in_export;
        n.word_target       = defaults->word_target;
        n.role              = defaults->role_name;
        if (!defaults->status_name.empty()) {
            std::string lo = defaults->status_name;
            std::transform(lo.begin(), lo.end(), lo.begin(), ::tolower);
            if      (lo == "rough draft") n.status = NodeStatus::RoughDraft;
            else if (lo == "in progress") n.status = NodeStatus::InProgress;
            else if (lo == "polished")    n.status = NodeStatus::Polished;
            else if (lo == "skip")        n.status = NodeStatus::Skip;
        }
    } else if (section == Section::Manuscript) {
        n.word_target = 1500;
    }

    vec->push_back(std::move(n));
    mark_modified();

    std::vector<int> new_path = parent_path;
    new_path.push_back((int)vec->size() - 1);
    return new_path;
}

// ─────────────────────────────────────────────────────────────────────────────
// DocumentModel — remove_node
// ─────────────────────────────────────────────────────────────────────────────

static bool path_matches_or_descends(const std::vector<int>& candidate,
                                     const std::vector<int>& target) {
    if (candidate.size() < target.size()) return false;
    for (size_t i = 0; i < target.size(); ++i)
        if (candidate[i] != target[i]) return false;
    return true;
}

void DocumentModel::remove_node(Section section, const std::vector<int>& path) {
    if (path.empty()) return;
    std::vector<int> parent_path(path.begin(), path.end() - 1);
    int remove_idx = path.back();
    auto* vec = children_of(*this, section, parent_path);
    if (!vec || remove_idx < 0 || remove_idx >= (int)vec->size()) return;
    vec->erase(vec->begin() + remove_idx);
    if (active_section == section && path_matches_or_descends(active_path, path))
        active_path.clear();
    mark_modified();
}

void DocumentModel::trash_node(Section section, const std::vector<int>& path) {
    if (path.empty()) return;
    // Don't trash items that are already in the trash
    if (section == Section::Trash) { remove_node(section, path); return; }

    std::vector<int> parent_path(path.begin(), path.end() - 1);
    int idx = path.back();
    auto* vec = children_of(*this, section, parent_path);
    if (!vec || idx < 0 || idx >= (int)vec->size()) return;

    // Copy the node, stamp origin fields, append to trash
    BinderNode trashed = (*vec)[idx];
    trashed.trash_origin_section = section_name(section);

    // Serialise path as a simple JSON array string e.g. "[0,2]"
    std::string ps = "[";
    for (size_t i = 0; i < path.size(); ++i) {
        if (i) ps += ",";
        ps += std::to_string(path[i]);
    }
    ps += "]";
    trashed.trash_origin_path_str = ps;

    trash.push_back(std::move(trashed));

    // Remove from original location
    vec->erase(vec->begin() + idx);
    if (active_section == section && path_matches_or_descends(active_path, path))
        active_path.clear();
    mark_modified();
}

void DocumentModel::restore_node(int trash_idx, Section target_section) {
    if (trash_idx < 0 || trash_idx >= (int)trash.size()) return;
    // Copy node, clear trash fields, append to top level of target section
    BinderNode restored = trash[trash_idx];
    restored.trash_origin_section.clear();
    restored.trash_origin_path_str.clear();
    root(target_section).push_back(std::move(restored));
    trash.erase(trash.begin() + trash_idx);
    mark_modified();
}

void DocumentModel::empty_trash() {
    if (trash.empty()) return;
    trash.clear();
    if (active_section == Section::Trash)
        active_path.clear();
    mark_modified();
}

// ─────────────────────────────────────────────────────────────────────────────
// DocumentModel — path_for_id
// ─────────────────────────────────────────────────────────────────────────────

static bool find_id_in(const std::vector<BinderNode>& nodes, int id,
                       std::vector<int>& path)
{
    for (int i = 0; i < (int)nodes.size(); ++i) {
        if (nodes[i].id == id) { path.push_back(i); return true; }
        path.push_back(i);
        if (find_id_in(nodes[i].children, id, path)) return true;
        path.pop_back();
    }
    return false;
}

std::vector<int> DocumentModel::path_for_id(Section section, int id) const {
    std::vector<int> path;
    if (find_id_in(root(section), id, path)) return path;
    return {};
}

// s19: by stable cross-layer iid.
static bool find_iid_in(const std::vector<BinderNode>& nodes, const std::string& iid,
                        std::vector<int>& path)
{
    for (int i = 0; i < (int)nodes.size(); ++i) {
        if (nodes[i].iid == iid) { path.push_back(i); return true; }
        path.push_back(i);
        if (find_iid_in(nodes[i].children, iid, path)) return true;
        path.pop_back();
    }
    return false;
}

std::vector<int> DocumentModel::path_for_iid(Section section, const std::string& iid) const {
    std::vector<int> path;
    if (!iid.empty() && find_iid_in(root(section), iid, path)) return path;
    return {};
}

// DocumentModel — move_node
// ─────────────────────────────────────────────────────────────────────────────

void DocumentModel::move_node(Section section,
                              const std::vector<int>& from,
                              const std::vector<int>& to_parent, int index) {
    if (from.empty()) return;
    if (path_matches_or_descends(to_parent, from)) return; // can't move into self

    std::vector<int> from_parent(from.begin(), from.end() - 1);
    int from_idx = from.back();

    auto* src_vec = children_of(*this, section, from_parent);
    if (!src_vec || from_idx < 0 || from_idx >= (int)src_vec->size()) return;

    BinderNode moving = std::move((*src_vec)[from_idx]);
    src_vec->erase(src_vec->begin() + from_idx);

    auto* dst_vec = children_of(*this, section, to_parent);
    if (!dst_vec) {
        src_vec->insert(src_vec->begin() + from_idx, std::move(moving));
        return;
    }

    index = std::clamp(index, 0, (int)dst_vec->size());
    dst_vec->insert(dst_vec->begin() + index, std::move(moving));

    if (active_section == section && path_matches_or_descends(active_path, from)) {
        // Account for same-parent index shift: when src is before the insertion
        // point in the same parent, the erase shifts the effective index down by 1.
        int landed = index;
        if (from_parent == to_parent && from_idx < index)
            --landed;
        std::vector<int> suffix(active_path.begin() + from.size(), active_path.end());
        active_path = to_parent;
        active_path.push_back(landed);
        active_path.insert(active_path.end(), suffix.begin(), suffix.end());
    }
    mark_modified();
}

// ─────────────────────────────────────────────────────────────────────────────
// DocumentModel — ID allocation
// ─────────────────────────────────────────────────────────────────────────────

int DocumentModel::scan_max_id(const BinderNode& node) const {
    int m = node.id;
    for (const auto& c : node.children) { int cm = scan_max_id(c); if (cm > m) m = cm; }
    return m;
}

int DocumentModel::next_id() const {
    int max_id = 0;
    for (const auto& n : manuscript) { int m = scan_max_id(n); if (m > max_id) max_id = m; }
    for (const auto& n : characters) { int m = scan_max_id(n); if (m > max_id) max_id = m; }
    for (const auto& n : places)     { int m = scan_max_id(n); if (m > max_id) max_id = m; }
    return max_id + 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// DocumentModel — compile
// ─────────────────────────────────────────────────────────────────────────────

void DocumentModel::collect_compile_nodes(const BinderNode& node,
                                          std::vector<const BinderNode*>& out) const {
    if (!node.include_in_export) return;
    out.push_back(&node);
    for (const auto& c : node.children) collect_compile_nodes(c, out);
}

std::vector<const BinderNode*> DocumentModel::compile_nodes() const {
    std::vector<const BinderNode*> out;
    for (const auto& n : manuscript) collect_compile_nodes(n, out);
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// DocumentModel — reset
// ─────────────────────────────────────────────────────────────────────────────

void DocumentModel::reset() {
    project_title    = "Untitled Project";
    author           = "";
    genre            = "";
    project_synopsis = "";
    publisher        = "";
    isbn             = "";
    barcode_price    = "";
    barcode_currency = "5";
    barcode_triangle = 0;
    barcode_svg      = "";
    year             = "";
    cover_image_path = "";
    cover_thumbnail  = "";
    daily_target          = 1500;
    session_words         = 0;
    project_word_target   = 80000;
    due_date              = "";
    daily_history.clear();
    manuscript.clear();
    characters.clear();
    places.clear();
    references.clear();
    templates.clear();
    trash.clear();
    active_section = Section::Manuscript;
    active_path.clear();
    open_tabs.clear();
    timeline_active_idx = -1;
    pomodoro_log.clear();
    is_modified  = false;
    current_path = "";
}

// ─────────────────────────────────────────────────────────────────────────────
// DocumentModel — file I/O
// ─────────────────────────────────────────────────────────────────────────────

void DocumentModel::save() {
    if (current_path.empty())
        throw std::runtime_error("No path set — use save_to() first.");
    save_to(current_path);
}

void DocumentModel::save_to(const std::string& path) {
    json j;
    j["folio_version"]    = kFolioBundleVersion;   // s19: v5 bundle
    j["project_title"]    = project_title;
    j["author"]           = author;
    j["genre"]            = genre;
    j["project_synopsis"] = project_synopsis;
    j["publisher"]        = publisher;
    j["isbn"]             = isbn;
    j["barcode_price"]    = barcode_price;
    j["barcode_currency"] = barcode_currency;
    j["barcode_triangle"] = barcode_triangle;
    j["barcode_svg"]      = barcode_svg;
    j["year"]             = year;
    j["cover_image_path"] = cover_image_path;
    j["cover_thumbnail"]  = cover_thumbnail;
    j["daily_target"]         = daily_target;
    j["project_word_target"]  = project_word_target;
    j["due_date"]             = due_date;

    // Daily writing history
    {
        json dlog = json::array();
        for (const auto& r : daily_history) {
            json e;
            e["date"]  = r.date;
            e["words"] = r.words;
            dlog.push_back(e);
        }
        j["daily_history"] = dlog;
    }

    auto serialise_tree = [](const std::vector<BinderNode>& tree) {
        json arr = json::array();
        for (const auto& n : tree) arr.push_back(n.to_json());
        return arr;
    };
    j["manuscript"]  = serialise_tree(manuscript);
    j["characters"]  = serialise_tree(characters);
    j["places"]      = serialise_tree(places);
    j["references"]  = serialise_tree(references);
    j["templates"]   = serialise_tree(templates);
    j["trash"]       = serialise_tree(trash);

    // Timeline open tabs
    {
        auto sec_str = [](Section s) -> std::string {
            if (s == Section::Characters) return "characters";
            if (s == Section::Places)     return "places";
            if (s == Section::References) return "references";
            if (s == Section::Templates)  return "templates";
            if (s == Section::Trash)      return "trash";
            return "manuscript";
        };
        json tl = json::array();
        for (const auto& t : open_tabs) {
            json e;
            e["section"] = sec_str(t.section);
            e["iid"]     = t.iid;          // s20: reorder-stable tab identity
            tl.push_back(e);
        }
        j["timeline_tabs"]       = tl;
        j["timeline_active_idx"] = timeline_active_idx;
    }

    // Sidebar selection state
    {
        auto sec_str = [](Section s) -> std::string {
            if (s == Section::Characters) return "characters";
            if (s == Section::Places)     return "places";
            if (s == Section::References) return "references";
            if (s == Section::Templates)  return "templates";
            if (s == Section::Trash)      return "trash";
            return "manuscript";
        };
        j["sidebar_selected_section"] = sec_str(sidebar_selected_section);
        j["sidebar_selected_iid"]     = sidebar_selected_iid;   // s20
    }

    // Pomodoro activity log
    {
        json plog = json::array();
        for (const auto& r : pomodoro_log) {
            json e;
            e["date"]         = r.date;
            e["phase"]        = r.phase;
            e["duration_sec"] = r.duration_sec;
            e["completed"]    = r.completed;
            plog.push_back(e);
        }
        j["pomodoro_log"] = plog;
    }

    // s19: write the v5 bundle. The blob `j` carries an iid on every node
    // (minted at creation / on migrate); ProjectBundle::explode strips each
    // node's content to content/<iid>.md and snapshots to snapshots/<iid>.json,
    // keeping structure + refs + drift hash in project.json, written last and
    // swapped into place atomically.
    explode(j, path);
    current_path = path;
    is_modified  = false;
}

void DocumentModel::load_from(const std::string& path) {
    json j;
    last_load_report.clear();

    switch (detect_format(path)) {
        case ProjectFormat::BundleV5: {
            ReconcileReport rep;
            j = implode(path, rep);              // (C)-hybrid reconcile
            last_load_report = rep.summary();
            break;
        }
        case ProjectFormat::LegacyFile: {
            std::ifstream f(path);
            if (!f) throw std::runtime_error("Cannot open: " + path);
            json raw = json::parse(f);
            int v = raw.value("folio_version", 0);
            if (v > kFolioBundleVersion)
                throw std::runtime_error("File requires a newer version of Folio (format " +
                                         std::to_string(v) + ").");
            j = migrate_v4(std::move(raw));      // mint iids, stamp v5
            break;
        }
        default:
            throw std::runtime_error("Not a Folio project: " + path);
    }

    parse_blob(j);
    current_path = path;
}

void DocumentModel::parse_blob(const json& j) {
    project_title    = j.value("project_title",    "Untitled Project");
    author           = j.value("author",           "");
    genre            = j.value("genre",            "");
    project_synopsis = j.value("project_synopsis", "");
    publisher        = j.value("publisher",        "");
    isbn             = j.value("isbn",             "");
    barcode_price    = j.value("barcode_price",    "");
    barcode_currency = j.value("barcode_currency", "5");
    barcode_triangle = j.value("barcode_triangle", 0);
    barcode_svg      = j.value("barcode_svg",      "");
    year             = j.value("year",             "");
    cover_image_path = j.value("cover_image_path", "");
    cover_thumbnail  = j.value("cover_thumbnail",  "");    daily_target         = j.value("daily_target",        1500);
    project_word_target  = j.value("project_word_target", 80000);
    due_date             = j.value("due_date",             "");

    // Daily writing history
    daily_history.clear();
    if (j.contains("daily_history") && j["daily_history"].is_array()) {
        for (const auto& e : j["daily_history"]) {
            DailyRecord r;
            r.date  = e.value("date",  "");
            r.words = e.value("words", 0);
            daily_history.push_back(std::move(r));
        }
    }

    auto load_tree = [](const json& j, const std::string& key, std::vector<BinderNode>& tree) {
        tree.clear();
        if (j.contains(key) && j[key].is_array())
            for (const auto& n : j[key]) { BinderNode node; node.from_json(n); tree.push_back(std::move(node)); }
    };

    load_tree(j, "manuscript",  manuscript);
    load_tree(j, "characters",  characters);
    load_tree(j, "places",      places);
    load_tree(j, "references",  references);
    load_tree(j, "templates",   templates);
    load_tree(j, "trash",       trash);

    // Timeline open tabs
    open_tabs.clear();
    timeline_active_idx = -1;
    if (j.contains("timeline_tabs") && j["timeline_tabs"].is_array()) {
        for (const auto& e : j["timeline_tabs"]) {
            SavedTab t;
            std::string sec = e.value("section", "manuscript");
            if      (sec == "characters") t.section = Section::Characters;
            else if (sec == "places")     t.section = Section::Places;
            else if (sec == "references") t.section = Section::References;
            else if (sec == "templates")  t.section = Section::Templates;
            else if (sec == "trash")      t.section = Section::Trash;
            else                          t.section = Section::Manuscript;
            if (e.contains("iid") && e["iid"].is_string())
                t.iid = e["iid"].get<std::string>();
            open_tabs.push_back(std::move(t));
        }
        timeline_active_idx = j.value("timeline_active_idx", -1);
    }

    // Sidebar selection state
    sidebar_selected_iid.clear();
    sidebar_selected_section = Section::Manuscript;
    if (j.contains("sidebar_selected_section")) {
        std::string sec = j.value("sidebar_selected_section", "manuscript");
        if      (sec == "characters") sidebar_selected_section = Section::Characters;
        else if (sec == "places")     sidebar_selected_section = Section::Places;
        else if (sec == "references") sidebar_selected_section = Section::References;
        else if (sec == "templates")  sidebar_selected_section = Section::Templates;
        else if (sec == "trash")      sidebar_selected_section = Section::Trash;
        else                          sidebar_selected_section = Section::Manuscript;
    }
    sidebar_selected_iid = j.value("sidebar_selected_iid", std::string{});

    // Pomodoro activity log
    pomodoro_log.clear();
    if (j.contains("pomodoro_log") && j["pomodoro_log"].is_array()) {
        for (const auto& e : j["pomodoro_log"]) {
            PomodoroRecord r;
            r.date         = e.value("date",         "");
            r.phase        = e.value("phase",        "Focus");
            r.duration_sec = e.value("duration_sec", 0);
            r.completed    = e.value("completed",    true);
            pomodoro_log.push_back(std::move(r));
        }
    }

    // ── Backwards-compatibility: old format v3 stored characters/places as
    //    flat arrays of {id, name, ...} without a "kind" field. The from_json
    //    already handles title←name; we just ensure kind is correct. ─────────
    std::function<void(BinderNode&, BinderKind)> fix_leaf_kind =
        [&](BinderNode& node, BinderKind leaf) {
            if (node.kind != BinderKind::Group) node.kind = leaf;
            for (auto& c : node.children) fix_leaf_kind(c, leaf);
        };
    for (auto& n : characters) fix_leaf_kind(n, BinderKind::Character);
    for (auto& n : places)     fix_leaf_kind(n, BinderKind::Place);
    for (auto& n : references) fix_leaf_kind(n, BinderKind::Reference);
    for (auto& n : templates)  fix_leaf_kind(n, BinderKind::Template);

    active_section = Section::Manuscript;
    active_path.clear();
    session_words = 0;
    is_modified   = false;
    // current_path is set by load_from (it owns the path); parse_blob is
    // path-agnostic and works on an already-obtained blob.
    rebuild_backlink_index();
}

// ─────────────────────────────────────────────────────────────────────────────
// DocumentModel — factory helpers
// ─────────────────────────────────────────────────────────────────────────────

DocumentModel DocumentModel::new_project(const std::string& title) {
    DocumentModel m;
    m.project_title = title;
    m.daily_target  = 1500;
    auto gp = m.add_group(Section::Manuscript, {}, "Part I");
    m.add_leaf(Section::Manuscript, gp, "Chapter 1");
    m.active_path.clear();
    m.is_modified = false;
    return m;
}

DocumentModel DocumentModel::make_demo() {
    DocumentModel m;
    m.project_title = "The Hollow Meridian";
    m.author        = "Author";
    m.daily_target  = 1500;

    // ── Manuscript ────────────────────────────────────────────────────────────
    auto p1 = m.add_group(Section::Manuscript, {}, "Part I — The Awakening");
    {
        auto path = m.add_leaf(Section::Manuscript, p1, "Prologue");
        auto* s = m.node_at(Section::Manuscript, path);
        s->synopsis = "The Institute in darkness. A signal arrives.";
        s->content  = "The lights were always on inside the Institute.";
        s->status = NodeStatus::Polished; s->color_idx = 1; s->word_target = 1200; // teal
        s->save_snapshot("First draft");
    }
    {
        auto path = m.add_leaf(Section::Manuscript, p1, "Chapter 1 — Emergence");
        auto* s = m.node_at(Section::Manuscript, path);
        s->synopsis = "Mara Voss arrives at the Institute for the first time.";
        s->content  = "The train from the coast took four hours.";
        s->status = NodeStatus::Polished; s->color_idx = 4; // green
        s->pov_character_name = "Mara Voss"; s->word_target = 4000;
    }
    {
        auto path = m.add_leaf(Section::Manuscript, p1, "Chapter 2 — The Signal");
        auto* s = m.node_at(Section::Manuscript, path);
        s->synopsis = "Routine telemetry shift. Something anomalous appears.";
        s->status = NodeStatus::InProgress; s->color_idx = 2; // yellow
        s->pov_character_name = "Mara Voss"; s->word_target = 3500;
    }
    auto p2 = m.add_group(Section::Manuscript, {}, "Part II — Descent");
    {
        auto path = m.add_leaf(Section::Manuscript, p2, "Chapter 5 — Echo");
        auto* s = m.node_at(Section::Manuscript, path);
        s->synopsis = "The northern array goes dark.";
        s->status = NodeStatus::InProgress; s->color_idx = 5; // mauve
        s->pov_character_name = "Mara Voss";
    }
    {
        auto path = m.add_leaf(Section::Manuscript, p2, "Chapter 7 — Hollow Meridian");
        auto* s = m.node_at(Section::Manuscript, path);
        s->synopsis = "Mara discovers the signal was real and suppressed.";
        s->status = NodeStatus::InProgress; s->color_idx = 1; // teal
        s->word_target = 1500; s->save_snapshot("Before major edit");
    }

    // ── Characters ────────────────────────────────────────────────────────────
    auto cg = m.add_group(Section::Characters, {}, "Main Characters");
    {
        auto path = m.add_leaf(Section::Characters, cg, "Mara Voss");
        auto* c = m.node_at(Section::Characters, path);
        c->description = "Protagonist · Analyst";
        c->synopsis    = "Signal analyst at the Institute. Methodical, perceptive.";
        c->color_idx   = 1; // teal
        c->role        = "Protagonist";
    }
    {
        auto path = m.add_leaf(Section::Characters, cg, "Director Lyell");
        auto* c = m.node_at(Section::Characters, path);
        c->description = "Antagonist · Institute";
        c->synopsis    = "Head of the Institute. Authoritative and calculating.";
        c->color_idx   = 3; // red
        c->role        = "Antagonist";
    }
    {
        auto path = m.add_leaf(Section::Characters, {}, "The Archivist");
        auto* c = m.node_at(Section::Characters, path);
        c->description = "Unknown · Cryptic";
        c->synopsis    = "Identity and motives unclear.";
        c->color_idx   = 5; // mauve
        c->role        = "";
    }

    // ── Places ────────────────────────────────────────────────────────────────
    {
        auto path = m.add_leaf(Section::Places, {}, "The Institute");
        auto* p = m.node_at(Section::Places, path);
        p->description = "Research complex · Remote";
        p->content     = "A vast research facility. Nineteen years of continuous operation.";
        p->synopsis    = "Key setting for most of Act I and II.";
        p->color_idx   = 1; // teal
    }
    {
        auto path = m.add_leaf(Section::Places, {}, "Sector 7 — Northern Array");
        auto* p = m.node_at(Section::Places, path);
        p->description = "Signal array · Restricted";
        p->synopsis    = "Where the anomalous signal originated.";
        p->color_idx   = 5; // mauve
    }

    m.active_path.clear();
    m.is_modified = false;
    return m;
}

// ─────────────────────────────────────────────────────────────────────────────
// find_node_by_id — search all sections for a node with the given id
// ─────────────────────────────────────────────────────────────────────────────

static BinderNode* find_in_tree(std::vector<BinderNode>& nodes, int id) {
    for (auto& n : nodes) {
        if (n.id == id) return &n;
        if (!n.children.empty()) {
            auto* found = find_in_tree(n.children, id);
            if (found) return found;
        }
    }
    return nullptr;
}

BinderNode* DocumentModel::find_node_by_id(int id) {
    for (auto* tree : { &manuscript, &characters, &places, &references, &templates, &trash }) {
        auto* n = find_in_tree(*tree, id);
        if (n) return n;
    }
    return nullptr;
}

const BinderNode* DocumentModel::find_node_by_id(int id) const {
    return const_cast<DocumentModel*>(this)->find_node_by_id(id);
}

// s19: by stable cross-layer iid (survives reorder/move).
static BinderNode* find_in_tree_iid(std::vector<BinderNode>& nodes,
                                    const std::string& iid) {
    for (auto& n : nodes) {
        if (n.iid == iid) return &n;
        if (!n.children.empty()) {
            auto* found = find_in_tree_iid(n.children, iid);
            if (found) return found;
        }
    }
    return nullptr;
}

BinderNode* DocumentModel::find_node_by_iid(const std::string& iid) {
    if (iid.empty()) return nullptr;
    for (auto* tree : { &manuscript, &characters, &places, &references, &templates, &trash }) {
        auto* n = find_in_tree_iid(*tree, iid);
        if (n) return n;
    }
    return nullptr;
}

const BinderNode* DocumentModel::find_node_by_iid(const std::string& iid) const {
    return const_cast<DocumentModel*>(this)->find_node_by_iid(iid);
}

// ─────────────────────────────────────────────────────────────────────────────
// extract_links_from_html — parse <a data-folio-link="iid:anchor"> tags
// Returns list of (target_iid, anchor_id) pairs found in the HTML. The iid is a
// type-prefixed token (e.g. "scn_k3f9a2b7") and contains no ':', so splitting
// on the first ':' cleanly separates iid from the (possibly empty) anchor.
// ─────────────────────────────────────────────────────────────────────────────

std::vector<std::pair<std::string,std::string>>
DocumentModel::extract_links_from_html(const std::string& html) {
    std::vector<std::pair<std::string,std::string>> out;
    size_t pos = 0;
    const std::string marker = "data-folio-link=";
    while ((pos = html.find(marker, pos)) != std::string::npos) {
        pos += marker.size();
        if (pos >= html.size()) break;
        char q = html[pos++];
        size_t end = html.find(q, pos);
        if (end == std::string::npos) break;
        std::string val = html.substr(pos, end - pos);
        pos = end + 1;
        // val = "iid:anchor_id"  (anchor may be empty → "iid:")
        auto colon = val.find(':');
        if (colon == std::string::npos) continue;
        std::string iid    = val.substr(0, colon);
        std::string anchor = val.substr(colon + 1);
        if (!iid.empty())
            out.push_back({iid, anchor});
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// rebuild_backlink_index — scan every node in every section and populate
// m_backlinks.  Called once after project load.
// ─────────────────────────────────────────────────────────────────────────────

static void collect_backlinks_from_tree(
    const std::vector<BinderNode>& nodes,
    std::map<std::string, std::vector<BacklinkEntry>>& index)
{
    for (const auto& node : nodes) {
        auto links = DocumentModel::extract_links_from_html(node.content);
        for (auto& [target_iid, anchor] : links) {
            BacklinkEntry e;
            e.source_iid    = node.iid;
            e.source_anchor = anchor;
            // Extract link text — find the <a ...> tag and grab inner text
            // (best-effort; display text is not critical for the index)
            index[target_iid].push_back(std::move(e));
        }
        if (!node.children.empty())
            collect_backlinks_from_tree(node.children, index);
    }
}

void DocumentModel::rebuild_backlink_index() {
    m_backlinks.clear();
    for (auto* tree : { &manuscript, &characters, &places, &references, &templates }) {
        collect_backlinks_from_tree(*tree, m_backlinks);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// update_backlinks_for_node — incremental update when one node's content changes.
// Removes all backlinks sourced from node_iid, then re-adds from new html.
// ─────────────────────────────────────────────────────────────────────────────

void DocumentModel::update_backlinks_for_node(const std::string& node_iid, const std::string& html) {
    // Remove all existing entries sourced from this node
    for (auto& [target_iid, entries] : m_backlinks) {
        entries.erase(
            std::remove_if(entries.begin(), entries.end(),
                [&node_iid](const BacklinkEntry& e){ return e.source_iid == node_iid; }),
            entries.end());
    }
    // Add fresh entries from the new content
    auto links = extract_links_from_html(html);
    for (auto& [target_iid, anchor] : links) {
        BacklinkEntry e;
        e.source_iid    = node_iid;
        e.source_anchor = anchor;
        m_backlinks[target_iid].push_back(std::move(e));
    }
}

} // namespace Folio
