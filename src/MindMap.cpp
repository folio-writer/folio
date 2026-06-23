// ─────────────────────────────────────────────────────────────────────────────
// MindMap.cpp — the canvas lens (s47). Pure; see MindMap.hpp.
//
// First cuts of the block-in: glyph derivation (s47), the W-frame engine, and the
// reflow engine — free-flow + pin + the first Position rule (grid) + Pull (soft
// gravity along an edge kind). Each is a pure, self-contained piece with a verb
// behind it ("a node wears the right shape", "a scene's core laid out as a form",
// "tidy the loose pile / leave it free", "let related things drift together").
// ─────────────────────────────────────────────────────────────────────────────
#include "MindMap.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace Folio {

// Shape carries kind, vocabulary kept small (the Scapple lesson). DERIVED from
// truth, never stored on the item. The three manuscript-object kinds get distinct
// shapes; an Asset (image/binary) reads as a Thumb. Everything else — including a
// Reference until its FRAGMENT FORM is known — defaults to the Card, the neutral
// "loose scrap" shape. The finer Reference path (note→Card, source/link→Clip,
// image→Thumb) lands with the capture/forms slice and will call a form-aware
// overload; this IidKind-only version is the safe default that path falls back to.
MapGlyph map_glyph_for(IidKind kind) {
    switch (kind) {
        case IidKind::Scene:      return MapGlyph::Square;   // the prose, the fired clay
        case IidKind::Character:  return MapGlyph::Circle;   // a person/agent
        case IidKind::Place:      return MapGlyph::Pin;      // a location
        case IidKind::Asset:      return MapGlyph::Thumb;    // an image / research binary

        case IidKind::Reference:                             // default until form known
        case IidKind::Group:
        case IidKind::Template:
        case IidKind::Snapshot:
        case IidKind::Unknown:
        default:                  return MapGlyph::Card;
    }
}

// ── The shipped default frame (data) ─────────────────────────────────────────
MindMapFrame five_ws_frame() {
    MindMapFrame f;
    f.name = "Five W's";
    f.slots = {
        { "Who",   SlotSource::Edge,  EdgeKind::Involves,   ""     },  // characters in the scene
        { "Where", SlotSource::Edge,  EdgeKind::SetIn,      ""     },  // the place it happens
        { "When",  SlotSource::Field, EdgeKind::Reference,  "date" },  // the focal node's Date field
        { "Why",   SlotSource::Edge,  EdgeKind::Foreshadow, ""     },  // what it plants (+ motivation later)
    };
    return f;   // "What" is the focal node at the centre, not a spoke.
}

// ── The W-frame engine — a focal node's neighbours laid into labelled zones ──
// Pure geometry over the typed edges. Coordinates are RELATIVE to the focus at the
// origin (0,0); the caller translates the rosette to the focus's canvas spot. The
// focus is placed at the centre (the "What"); each slot becomes a spoke at a fixed
// angle, and the neighbours connected to the focus by that slot's edge kind sit
// along the spoke. A slot that catches no edge is reported in `out_empty_slots` — a
// surfaced FACT (this scene has no Where/Why linked yet), never a verdict.
std::vector<MindMapLayout::Placement>
MindMapLayout::frame(const MindMapFrame& f, const std::string& focus_iid,
                     const std::vector<StoryEdge>& edges,
                     const std::vector<std::pair<std::string, std::string>>& focus_fields,
                     std::vector<std::string>& out_empty_slots) {
    out_empty_slots.clear();
    std::vector<Placement> out;
    if (focus_iid.empty()) return out;

    constexpr double kPi      = 3.14159265358979323846;
    constexpr double kZoneR   = 240.0;   // spoke length to the first neighbour
    constexpr double kStepR   =  90.0;   // extra reach per additional neighbour in a zone

    // Centre = the focus itself (the "What").
    out.push_back({ focus_iid, 0.0, 0.0, map_glyph_for(iid_kind_of(focus_iid)), "", false });

    const std::size_t S = f.slots.size();
    for (std::size_t i = 0; i < S; ++i) {
        const FrameSlot& slot = f.slots[i];

        // Spoke direction: start at top (−90°), distribute clockwise around the dial.
        const double ang = -kPi / 2.0 + (S ? (2.0 * kPi * static_cast<double>(i) / static_cast<double>(S)) : 0.0);
        const double dx = std::cos(ang), dy = std::sin(ang);

        // ── Field-bound slot: read a value off the focal node itself (e.g. When). ──
        if (slot.source == SlotSource::Field) {
            std::string val;
            for (const auto& kv : focus_fields)
                if (kv.first == slot.field_id) { val = kv.second; break; }

            if (!val.empty()) {
                Placement chip;
                chip.x = dx * kZoneR; chip.y = dy * kZoneR;
                chip.text = val; chip.field_chip = true;
                out.push_back(chip);                       // a value chip in the zone
            } else {
                out_empty_slots.push_back(slot.label);     // no value set → surfaced fact
            }
            continue;
        }

        // ── Edge-bound slot: collect neighbours via this slot's edge kind (either ──
        // direction), de-duplicated — a node linked twice still occupies one spot.
        std::unordered_set<std::string> seen;
        int k = 0;
        for (const StoryEdge& e : edges) {
            if (e.kind != slot.edge) continue;
            std::string neighbour;
            if      (e.from_iid == focus_iid) neighbour = e.to_iid;
            else if (e.to_iid   == focus_iid) neighbour = e.from_iid;
            else continue;
            if (neighbour.empty() || !seen.insert(neighbour).second) continue;

            const double r = kZoneR + kStepR * static_cast<double>(k++);
            out.push_back({ neighbour, dx * r, dy * r, map_glyph_for(iid_kind_of(neighbour)), "", false });
        }

        if (k == 0) out_empty_slots.push_back(slot.label);   // surfaced fact, not a defect
    }
    return out;
}

// ── Pull helpers (file-local) ────────────────────────────────────────────────
namespace {

// Canonical band order for lane-by-kind: the manuscript objects first (the spine),
// then the looser kinds. Lower rank = higher band. Only kinds actually present get
// a band (compacted, no empty gaps).
int kind_rank(IidKind k) {
    switch (k) {
        case IidKind::Scene:     return 0;
        case IidKind::Character: return 1;
        case IidKind::Place:     return 2;
        case IidKind::Reference: return 3;
        case IidKind::Group:     return 4;
        case IidKind::Template:  return 5;
        case IidKind::Asset:     return 6;
        case IidKind::Snapshot:  return 7;
        default:                 return 8;   // Unknown last
    }
}

// Token → EdgeKind for a Pull rule's param ("involves", "setin", …). Returns false
// for empty/unrecognised, which the caller reads as "pull along ALL edge kinds"
// (lenient, like the rest of the config layer).
bool pull_kind_from_token(const std::string& s, EdgeKind& out) {
    if      (s == "reference")  out = EdgeKind::Reference;
    else if (s == "foreshadow") out = EdgeKind::Foreshadow;
    else if (s == "setin")      out = EdgeKind::SetIn;
    else if (s == "involves")   out = EdgeKind::Involves;
    else if (s == "signpost")   out = EdgeKind::Signpost;
    else return false;
    return true;
}

// One deterministic, SIMULTANEOUS pull pass: each unpinned node drifts a fixed
// fraction toward the centroid of its edge-neighbours' CURRENT positions. Pinned
// nodes don't move but still anchor (their position pulls others). Neighbour
// positions are read from a snapshot taken before any node moves, so the result is
// independent of iteration order. A node with no matching neighbours stays put.
void apply_pull_pass(std::vector<MindMapLayout::Placement>& out,
                     const std::vector<StoryEdge>& edges,
                     const std::string& param,
                     const std::unordered_set<std::string>& pinned) {
    constexpr double kPull = 0.25;   // soft: halve-ish the gap, never collapse to a point

    EdgeKind want = EdgeKind::Reference;
    const bool all = !pull_kind_from_token(param, want);   // empty/garbage → all kinds

    // Snapshot of current node positions (chips excluded).
    std::unordered_map<std::string, std::pair<double, double>> pos;
    for (const auto& p : out)
        if (!p.field_chip && !p.iid.empty()) pos[p.iid] = { p.x, p.y };

    for (auto& p : out) {
        if (p.field_chip || p.iid.empty() || pinned.count(p.iid)) continue;

        double cx = 0.0, cy = 0.0; int n = 0;
        std::unordered_set<std::string> seen;
        for (const StoryEdge& e : edges) {
            if (!all && e.kind != want) continue;
            std::string nb;
            if      (e.from_iid == p.iid) nb = e.to_iid;
            else if (e.to_iid   == p.iid) nb = e.from_iid;
            else continue;
            if (nb.empty() || !seen.insert(nb).second) continue;
            auto it = pos.find(nb);
            if (it == pos.end()) continue;          // neighbour not on this map → no pull
            cx += it->second.first; cy += it->second.second; ++n;
        }
        if (n == 0) continue;
        cx /= n; cy /= n;
        p.x += kPull * (cx - p.x);
        p.y += kPull * (cy - p.y);
    }
}

}  // namespace

// ── Balloon (radial) layout — the nested-clock / cloud structure ─────────────
// A container owns a RING of its children, recursively: Project ▸ Parts ▸
// Chapters ▸ Scenes (▸ KP). Each node sits at the centre of a clock whose hands
// seat its children; a child that is itself a container brings its own clock, so
// clocks nest. The hard part is SIZING, done bottom-up: a node's radius is big
// enough to seat its largest child's whole subtree on the ring without overlap,
// so clouds never collide no matter how deep the book. Deterministic and pure —
// these are the RESTING positions the canvas animates toward (the "elastic" feel
// is a render-side tween over these targets, not a force sim).
namespace {

constexpr double kBNode = 34.0;   // leaf footprint radius (world units)
constexpr double kBGap  = 30.0;   // breathing room between sibling subtrees
constexpr double kBJit  = 12.0;   // organic wobble: structure underneath, not a perfect ring
constexpr double kFan   = 2.4;    // arc (rad) a non-root node fans its children over (outward)

// Deterministic per-iid pseudo-random in [0,1) (FNV-1a + salt). Seeded by the iid
// so the wobble is STABLE across rebuilds — organic, never a jitter-dance. This is
// what keeps the balloon from reading as mechanically perfect (the Obsidian feel)
// while staying fully deterministic (a child sits NEAR its ring slot, not on it).
inline double jrand(const std::string& s, unsigned salt) {
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    h ^= static_cast<std::uint64_t>(salt) * 2654435761u; h *= 1099511628211ull;
    return static_cast<double>((h >> 11) & 0xFFFFFu) / static_cast<double>(0x100000);
}

struct Balloon {
    const std::unordered_map<std::string, std::vector<std::string>>& kids;
    std::unordered_map<std::string, double> R_memo, ring_memo;
    std::unordered_set<std::string> active;     // cycle guard (malformed parent loop)

    // Subtree radius: how far this node's whole cluster reaches from its centre.
    // `span` is the arc its children fan over (2π for the centre/root, kFan below):
    // a tighter fan packs children closer angularly, so it needs a LARGER ring to
    // keep their subtrees from touching — that's why span enters the sizing.
    double radius(const std::string& id, double span) {
        auto m = R_memo.find(id);
        if (m != R_memo.end()) return m->second;
        if (!active.insert(id).second) return kBNode;   // cycle → treat as leaf

        double r = kBNode, ring = 0.0;
        auto it = kids.find(id);
        if (it != kids.end() && !it->second.empty()) {
            const std::vector<std::string>& ch = it->second;
            double maxrc = 0.0;
            for (const std::string& c : ch) maxrc = std::max(maxrc, radius(c, kFan));
            const std::size_t k = ch.size();
            if (k == 1) {
                ring = maxrc + kBGap + kBNode;          // single child sits clear of the hub
            } else {
                const double step = span / static_cast<double>(k);   // angular step in the fan
                const double s = std::sin(std::min(step, M_PI) / 2.0);
                ring = std::max((maxrc + kBGap) / (s > 1e-6 ? s : 1.0), maxrc + kBGap + kBNode);
            }
            r = ring + maxrc + kBJit;                   // reserve room for the wobble
        }
        active.erase(id);
        R_memo[id] = r; ring_memo[id] = ring;
        return r;
    }

    // Place a node's children fanned over `span`, centred on the OUTWARD direction
    // a0 (away from the grandparent), each nudged off its slot by a stable per-iid
    // wobble, then recurse. The centre/root fans over a full 2π (a true ring); every
    // node below fans its children outward over kFan, so subtrees splay away from
    // the trunk instead of streaking back through it. A two-child cluster becomes a
    // gentle V, not a line through the hub.
    void place(const std::string& id, double cx, double cy, double a0, double span,
               std::unordered_map<std::string, std::pair<double, double>>& pos) {
        pos[id] = { cx, cy };
        auto it = kids.find(id);
        if (it == kids.end() || it->second.empty()) return;
        const std::vector<std::string>& ch = it->second;
        const double ring = ring_memo.count(id) ? ring_memo[id] : 0.0;
        const std::size_t k = ch.size();
        const double rot = (jrand(id, 4) * 2.0 - 1.0) * 0.25;   // small organic rotation
        for (std::size_t i = 0; i < k; ++i) {
            // distribute evenly across the fan, centred on a0
            const double frac = (static_cast<double>(i) + 0.5) / static_cast<double>(k) - 0.5;
            const double th = a0 + rot + span * frac;
            const double jx = (jrand(ch[i], 1) * 2.0 - 1.0) * kBJit;
            const double jy = (jrand(ch[i], 2) * 2.0 - 1.0) * kBJit;
            place(ch[i], cx + ring * std::cos(th) + jx, cy + ring * std::sin(th) + jy, th, kFan, pos);
        }
    }
};

}  // namespace

// ── The reflow engine — free-flow floor + pin + Position(grid) + Pull ────────
// Composition (locked): exactly ONE Position and ONE Lane rule own the axes (radio);
// Pull rules STACK on top (checkbox). Stage 1 places nodes (floor / grid / lane);
// stage 2 lets any enabled Pull rules drift connected nodes together. A pinned item
// is the manual exception throughout — fixed in stage 1, an anchor in stage 2.
//
// With no rule the map is FREE-FLOW (every item at its authored x/y, the lossless
// floor). "pos.grid" (DESIGN R14) tidies unpinned items into a grid. "lane.kind"
// bands unpinned items into horizontal lanes by kind (scenes, then characters, …),
// left-to-right by iid within a band. Lane takes the arrangement when active (grid +
// lane don't compose — both are 2-D tidies); a 1-D Position rule (manuscript order,
// chronology) will compose properly with Lane — driving within-band X — when it
// lands. Thread / timeframe / signpost lanes need per-node SceneMark data (a later
// cut). Any unrecognised Position/Lane id falls back to the floor (safe). Style not
// consumed yet.
std::vector<MindMapLayout::Placement>
MindMapLayout::reflow(const std::vector<MindMapItem>& items,
                      const std::vector<ReflowRule>&  rules,
                      const std::vector<StoryEdge>&   edges) {
    // One active Position and one active Lane (first enabled of each — radio).
    const ReflowRule* pos  = nullptr;
    const ReflowRule* lane = nullptr;
    for (const ReflowRule& r : rules) {
        if (r.enabled && r.role == RuleRole::Position && !pos)  pos  = &r;
        if (r.enabled && r.role == RuleRole::Lane     && !lane) lane = &r;
    }
    const bool use_lane = lane && lane->id == "lane.kind";
    const bool grid     = !use_lane && pos && pos->id == "pos.grid";   // lane takes precedence
    const bool balloon  = !use_lane && pos && pos->id == "pos.balloon";

    std::vector<Placement> out;
    out.reserve(items.size());
    std::unordered_set<std::string> pinned;

    auto at_authored = [&](const MindMapItem& it) {
        out.push_back({ it.iid, it.x, it.y, map_glyph_for(iid_kind_of(it.iid)), "", false });
    };

    // Collect the unpinned (flowed) set once; pinned items are emitted at authored
    // coords and recorded as anchors. Shared by the grid and lane branches.
    auto collect_flow = [&](std::vector<const MindMapItem*>& flow) {
        for (const MindMapItem& it : items) {
            if (it.iid.empty()) continue;
            if (it.pinned) { pinned.insert(it.iid); at_authored(it); }
            else           flow.push_back(&it);
        }
    };

    constexpr double kCell = 160.0;

    // ── Stage 1: position / lane ──
    if (use_lane) {
        // LANE-by-kind: kind blocks, top-to-bottom. Within a kind, items wrap at
        // kLaneCols into stacked rows (a block, not a runaway ribbon), then a
        // one-row gap separates kinds. Left-to-right by iid within a kind.
        constexpr int kLaneCols = 10;
        std::vector<const MindMapItem*> flow;
        collect_flow(flow);
        std::sort(flow.begin(), flow.end(), [](const MindMapItem* a, const MindMapItem* b) {
            const int ra = kind_rank(iid_kind_of(a->iid)), rb = kind_rank(iid_kind_of(b->iid));
            return ra != rb ? ra < rb : a->iid < b->iid;
        });
        int base_row = 0, col = 0, block_rows = 1;
        IidKind prev = IidKind::Unknown; bool first = true;
        for (const MindMapItem* it : flow) {
            const IidKind k = iid_kind_of(it->iid);
            if (first || k != prev) {                      // new kind block
                if (!first) base_row += block_rows + 1;    // past prev block + 1-row gap
                col = 0; block_rows = 1; prev = k; first = false;
            }
            const int lrow = col / kLaneCols, lcol = col % kLaneCols;
            block_rows = std::max(block_rows, lrow + 1);
            out.push_back({ it->iid, lcol * kCell, (base_row + lrow) * kCell,
                            map_glyph_for(k), "", false });
            ++col;
        }
    } else if (grid) {
        // GRID: pinned stay authored; unpinned tidy into a square-ish lattice.
        std::vector<const MindMapItem*> flow;
        collect_flow(flow);
        std::sort(flow.begin(), flow.end(),
                  [](const MindMapItem* a, const MindMapItem* b) { return a->iid < b->iid; });

        int cols = 0;
        if (!pos->param.empty()) { try { cols = std::stoi(pos->param); } catch (...) { cols = 0; } }
        if (cols <= 0) cols = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(flow.size())))));

        for (std::size_t i = 0; i < flow.size(); ++i) {
            const int row = static_cast<int>(i) / cols;
            const int col = static_cast<int>(i) % cols;
            out.push_back({ flow[i]->iid, col * kCell, row * kCell,
                            map_glyph_for(iid_kind_of(flow[i]->iid)), "", false });
        }
    } else if (balloon) {
        // BALLOON: nested radial clusters from each node's parent_iid (containment).
        // A node whose parent_iid is empty or off-map is a ROOT cloud; many roots
        // ring a virtual centre, a single root centres at the origin. Pinned items
        // are NOT special here — the structure owns position in this view (the hand
        // takes over by switching the master rule off / dragging in free-flow).
        std::unordered_set<std::string> idset;
        for (const MindMapItem& it : items) if (!it.iid.empty()) idset.insert(it.iid);

        const std::string kRoot = "\x01__root__";   // sentinel (never a real iid)
        std::unordered_map<std::string, std::vector<std::string>> kids;
        for (const MindMapItem& it : items) {
            if (it.iid.empty()) continue;
            const bool is_root = it.parent_iid.empty() || !idset.count(it.parent_iid);
            kids[is_root ? kRoot : it.parent_iid].push_back(it.iid);
        }
        // NOTE: children keep the order they arrive in `items` — the caller feeds
        // them in READING order (prologue ▸ c1 ▸ c2 ▸ … ▸ epilogue), so the clock
        // hands go round in story order rather than by arbitrary iid. Determinism
        // is the caller's stable order, not an internal sort.

        std::unordered_map<std::string, std::pair<double, double>> pos_xy;
        Balloon b{ kids, {}, {}, {} };
        const std::vector<std::string>& roots = kids[kRoot];
        const double kFull = 2.0 * M_PI;   // the centre rings its children fully
        if (roots.size() == 1) {
            b.radius(roots[0], kFull);
            b.place(roots[0], 0.0, 0.0, -M_PI / 2.0, kFull, pos_xy);
        } else if (!roots.empty()) {
            b.radius(kRoot, kFull);                             // sizes the virtual centre ring
            b.place(kRoot, 0.0, 0.0, -M_PI / 2.0, kFull, pos_xy);
            pos_xy.erase(kRoot);                               // the centre is virtual, not drawn
        }

        for (const MindMapItem& it : items) {
            if (it.iid.empty()) continue;
            auto p = pos_xy.find(it.iid);
            const double x = (p != pos_xy.end()) ? p->second.first  : it.x;   // unreachable → authored
            const double y = (p != pos_xy.end()) ? p->second.second : it.y;
            out.push_back({ it.iid, x, y, map_glyph_for(iid_kind_of(it.iid)), "", false });
        }
    } else {
        for (const MindMapItem& it : items) {
            if (it.iid.empty()) continue;
            if (it.pinned) pinned.insert(it.iid);
            at_authored(it);
        }
    }

    // ── Stage 2: pull (each enabled Pull rule is one pass; they stack) ──
    for (const ReflowRule& r : rules)
        if (r.enabled && r.role == RuleRole::Pull)
            apply_pull_pass(out, edges, r.param, pinned);

    return out;
}

// ── Viewport transforms + hit-testing (pure) ─────────────────────────────────
ScreenPt world_to_screen(const MapViewport& vp, double wx, double wy) {
    return { wx * vp.zoom + vp.pan_x, wy * vp.zoom + vp.pan_y };
}

WorldPt screen_to_world(const MapViewport& vp, double sx, double sy) {
    const double z = (vp.zoom != 0.0) ? vp.zoom : 1.0;   // guard a degenerate zoom
    return { (sx - vp.pan_x) / z, (sy - vp.pan_y) / z };
}

std::string hit_test(const std::vector<MindMapLayout::Placement>& placements,
                     const MapViewport& vp, double screen_x, double screen_y,
                     double radius_px) {
    const double r2 = radius_px * radius_px;
    std::string hit;                              // last (topmost) wins
    for (const MindMapLayout::Placement& p : placements) {
        if (p.field_chip || p.iid.empty()) continue;     // chips aren't navigable
        const ScreenPt s = world_to_screen(vp, p.x, p.y);
        const double dx = s.x - screen_x, dy = s.y - screen_y;
        if (dx * dx + dy * dy <= r2) hit = p.iid;
    }
    return hit;
}

}  // namespace Folio