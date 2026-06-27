// ─────────────────────────────────────────────────────────────────────────────
// TimelineResources.cpp — the resource-rail roster (s82). See header.
//
// Build an iid→claim-count map from the shipped tracks, bucket the candidates by
// category, attach each candidate's count, then sort each bucket by label / iid
// and emit the non-empty groups in §9.6 hue order. One pass, no claim-rule
// duplication (the count comes straight from assemble_tracks' claimed sets).
// ─────────────────────────────────────────────────────────────────────────────

#include "TimelineResources.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <unordered_map>

namespace Folio {

namespace {

// case-insensitive label compare, iid as the deterministic tiebreak. ASCII fold
// only (titles are arbitrary unicode, but a stable lower of the ASCII range is
// enough to keep a roster scannable; ties fall to the iid, never ambiguous).
std::string ascii_lower(const std::string& s) {
  std::string out = s;
  for (char& c : out)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

bool item_less(const ResourceItem& a, const ResourceItem& b) {
  const std::string la = ascii_lower(a.label);
  const std::string lb = ascii_lower(b.label);
  if (la != lb) return la < lb;
  return a.iid < b.iid;
}

}  // namespace

std::vector<ResourceGroup>
assemble_resources(const std::vector<ResourceCandidate>& candidates,
                   const std::vector<TimelineTrack>& tracks) {
  // subject iid → on-spine claim count (the shipped tracks are the one source of
  // truth for "what a subject claims"; a candidate with no track counts 0).
  std::unordered_map<std::string, int> count_of;
  count_of.reserve(tracks.size() * 2);
  for (const TimelineTrack& t : tracks)
    count_of[t.iid] = static_cast<int>(t.claimed.size());

  // Bucket candidates by category (the §9.6 hue order is the enum order, so a
  // fixed-size array keyed by the enum value preserves it for free).
  constexpr std::size_t kCats = 4;  // Character, Place, Reference, Image
  std::array<std::vector<ResourceItem>, kCats> buckets;

  for (const ResourceCandidate& c : candidates) {
    const std::size_t bi = static_cast<std::size_t>(c.category);
    if (bi >= kCats) continue;  // guard (enum is closed, but stay total)
    ResourceItem it;
    it.iid = c.iid;
    it.label = c.label;
    it.category = c.category;
    auto found = count_of.find(c.iid);
    it.claim_count = (found != count_of.end()) ? found->second : 0;
    buckets[bi].push_back(std::move(it));
  }

  std::vector<ResourceGroup> groups;
  groups.reserve(kCats);
  for (std::size_t bi = 0; bi < kCats; ++bi) {
    if (buckets[bi].empty()) continue;  // omit empty categories
    std::sort(buckets[bi].begin(), buckets[bi].end(), item_less);
    ResourceGroup g;
    g.category = static_cast<TrackCategory>(bi);
    g.items = std::move(buckets[bi]);
    groups.push_back(std::move(g));
  }
  return groups;
}

}  // namespace Folio
