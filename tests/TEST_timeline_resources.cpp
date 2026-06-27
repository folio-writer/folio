// ─────────────────────────────────────────────────────────────────────────────
// TEST_timeline_resources.cpp — sandbox proof for the resource-rail roster
// (s82, assemble_resources). Pure: no GTK, no JSON, no DocumentModel. Verifies
// the §3 roster — the union of linkable subjects with their live on-spine claim
// counts, grouped in §9.6 hue order, label-sorted within a group, zero-claim
// candidates INCLUDED (the whole point: the rail offers subjects with no track).
//
// Build + run (bare command lines — copy/paste the block):
/*
g++ -std=c++20 -Wall -Wextra -Wconversion -Wshadow -I include \
    src/TimelineResources.cpp tests/TEST_timeline_resources.cpp \
    -o /tmp/test_timeline_resources && /tmp/test_timeline_resources

clang++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I include \
    src/TimelineResources.cpp tests/TEST_timeline_resources.cpp \
    -o /tmp/test_timeline_resources && /tmp/test_timeline_resources
*/
// ─────────────────────────────────────────────────────────────────────────────

#include "TimelineResources.hpp"

#include <iostream>
#include <string>
#include <vector>

using namespace Folio;

static int g_pass = 0, g_fail = 0;
static void ok(bool c, const std::string& what) {
  if (c) { ++g_pass; }
  else   { ++g_fail; std::cerr << "  FAIL: " << what << "\n"; }
}

// Build a track with a given subject iid + claimed scene-set (count is what the
// rail reads). category/first_pos are irrelevant to assemble_resources.
static TimelineTrack trk(const std::string& iid,
                         const std::vector<std::string>& claimed) {
  TimelineTrack t;
  t.iid = iid;
  for (const auto& s : claimed) t.claimed.insert(s);
  return t;
}

static const ResourceGroup* group_for(const std::vector<ResourceGroup>& gs,
                                      TrackCategory cat) {
  for (const auto& g : gs) if (g.category == cat) return &g;
  return nullptr;
}

int main() {
  // ── 1. Empty roster → no groups ────────────────────────────────────────────
  {
    auto gs = assemble_resources({}, {});
    ok(gs.empty(), "empty candidates → empty groups");
  }

  // ── 2. Zero-claim candidate is INCLUDED (the builder requirement) ───────────
  {
    std::vector<ResourceCandidate> cands = {
      {"plc_a", "Place A", TrackCategory::Place},  // no track at all
    };
    auto gs = assemble_resources(cands, /*tracks*/ {});
    ok(gs.size() == 1, "one non-empty group");
    const auto* g = group_for(gs, TrackCategory::Place);
    ok(g && g->items.size() == 1, "place group has the one candidate");
    ok(g && g->items[0].iid == "plc_a", "candidate iid preserved");
    ok(g && g->items[0].claim_count == 0, "no track → claim_count 0 (still listed)");
  }

  // ── 3. Claim count comes from the matching track's claimed-set size ─────────
  {
    std::vector<ResourceCandidate> cands = {
      {"chr_boromir", "Boromir", TrackCategory::Character},
      {"chr_faramir", "Faramir", TrackCategory::Character},  // no track → 0
    };
    std::vector<TimelineTrack> tracks = {
      trk("chr_boromir", {"scn_1", "scn_2", "scn_5", "scn_30"}),
    };
    auto gs = assemble_resources(cands, tracks);
    const auto* g = group_for(gs, TrackCategory::Character);
    ok(g && g->items.size() == 2, "both characters listed");
    // sorted by label: Boromir before Faramir
    ok(g && g->items[0].label == "Boromir", "label sort: Boromir first");
    ok(g && g->items[0].claim_count == 4, "Boromir count = 4 (track claimed size)");
    ok(g && g->items[1].label == "Faramir", "Faramir second");
    ok(g && g->items[1].claim_count == 0, "Faramir count 0 (no track)");
  }

  // ── 4. Group order = §9.6 hue order (character ▸ place ▸ reference ▸ image) ──
  {
    std::vector<ResourceCandidate> cands = {
      {"ast_map",  "World Map", TrackCategory::Image},
      {"ref_lore", "The Lore",  TrackCategory::Reference},
      {"plc_moria","Moria",     TrackCategory::Place},
      {"chr_frodo","Frodo",     TrackCategory::Character},
    };
    auto gs = assemble_resources(cands, {});
    ok(gs.size() == 4, "four groups");
    ok(gs[0].category == TrackCategory::Character, "group 0 = Character");
    ok(gs[1].category == TrackCategory::Place,     "group 1 = Place");
    ok(gs[2].category == TrackCategory::Reference, "group 2 = Reference");
    ok(gs[3].category == TrackCategory::Image,     "group 3 = Image");
  }

  // ── 5. Within-group sort is case-insensitive by label, iid breaks ties ──────
  {
    std::vector<ResourceCandidate> cands = {
      {"chr_z", "zara",  TrackCategory::Character},
      {"chr_a", "Aragorn", TrackCategory::Character},
      {"chr_b", "aragorn", TrackCategory::Character},  // dup label, lower iid? no: chr_b
      {"chr_m", "Merry", TrackCategory::Character},
    };
    auto gs = assemble_resources(cands, {});
    const auto* g = group_for(gs, TrackCategory::Character);
    ok(g && g->items.size() == 4, "four characters");
    // "Aragorn"/"aragorn" fold equal → iid tiebreak: chr_a before chr_b
    ok(g && g->items[0].iid == "chr_a", "case-insensitive: Aragorn(chr_a) first");
    ok(g && g->items[1].iid == "chr_b", "tie broken by iid: aragorn(chr_b) second");
    ok(g && g->items[2].label == "Merry", "Merry third");
    ok(g && g->items[3].label == "zara", "zara last (case-insensitive)");
  }

  // ── 6. A track for a subject NOT in candidates is ignored (orphan edge) ─────
  // The rail is the roster of CURRENT resources; a stale track (deleted object
  // still referenced by an edge) must not conjure a phantom row.
  {
    std::vector<ResourceCandidate> cands = {
      {"plc_real", "Real Place", TrackCategory::Place},
    };
    std::vector<TimelineTrack> tracks = {
      trk("plc_real",  {"scn_1"}),
      trk("plc_ghost", {"scn_2", "scn_3"}),  // no candidate → not shown
    };
    auto gs = assemble_resources(cands, tracks);
    const auto* g = group_for(gs, TrackCategory::Place);
    ok(g && g->items.size() == 1, "only the candidate place is listed");
    ok(g && g->items[0].iid == "plc_real" && g->items[0].claim_count == 1,
       "real place keeps its count; ghost track ignored");
  }

  // ── 7. Blank label rides through blank (rail falls back to iid for display) ──
  {
    std::vector<ResourceCandidate> cands = {
      {"chr_named", "Named", TrackCategory::Character},
      {"chr_blank", "",      TrackCategory::Character},
    };
    auto gs = assemble_resources(cands, {});
    const auto* g = group_for(gs, TrackCategory::Character);
    ok(g && g->items.size() == 2, "blank-label candidate still listed");
    // "" sorts before "named"
    ok(g && g->items[0].iid == "chr_blank" && g->items[0].label.empty(),
       "blank label preserved (not synthesised)");
  }

  std::cout << "TEST_timeline_resources: " << g_pass << " passed, "
            << g_fail << " failed\n";
  return g_fail == 0 ? 0 : 1;
}
