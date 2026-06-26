// ─────────────────────────────────────────────────────────────────────────────
// Gallery.cpp — the Gallery lens reads (see Gallery.hpp). Pure; no GTK, no I/O.
// ─────────────────────────────────────────────────────────────────────────────

#include "Gallery.hpp"

#include <nlohmann/json.hpp>

#include "CustomMindMap.hpp"   // cmm_from_json + kMindMapTemplateId (MM subjects/anchors)
#include "Iid.hpp"

namespace Folio {

namespace {
using json = nlohmann::json;

// First-seen-preserving dedupe append.
void push_unique(std::vector<std::string>& out, const std::string& v) {
  for (const std::string& e : out)
    if (e == v)
      return;
  out.push_back(v);
}
}  // namespace

std::vector<std::string>
gallery_objects_of(const std::vector<StoryEdge>& edges,
                   const std::string& image_iid) {
  std::vector<std::string> out;
  if (image_iid.empty())
    return out;
  for (const StoryEdge& e : edges)
    if (e.from_iid == image_iid && !e.to_iid.empty())
      push_unique(out, e.to_iid);
  return out;
}

std::vector<std::string>
gallery_images_of(const std::vector<StoryEdge>& edges,
                  const std::string& object_iid) {
  std::vector<std::string> out;
  if (object_iid.empty())
    return out;
  for (const StoryEdge& e : edges)
    if (e.to_iid == object_iid && iid_kind_of(e.from_iid) == IidKind::Asset)
      push_unique(out, e.from_iid);
  return out;
}

int gallery_next_index(const std::vector<std::string>& ordered,
                       const std::string& current_iid) {
  const int n = static_cast<int>(ordered.size());
  if (n == 0)
    return -1;
  for (int i = 0; i < n; ++i)
    if (ordered[i] == current_iid)
      return (i + 1) % n;        // wrap forward
  return -1;
}

int gallery_prev_index(const std::vector<std::string>& ordered,
                       const std::string& current_iid) {
  const int n = static_cast<int>(ordered.size());
  if (n == 0)
    return -1;
  for (int i = 0; i < n; ++i)
    if (ordered[i] == current_iid)
      return (i - 1 + n) % n;    // wrap backward
  return -1;
}

std::string gallery_lens_to_json(const std::vector<std::string>& order,
                                 const std::vector<std::string>& links) {
  json arr = json::array();
  for (const std::string& iid : order)
    if (!iid.empty())
      arr.push_back(iid);
  json doc{{"order", std::move(arr)}};
  // "links" is omitted when empty so a link-less gallery's body stays as it was.
  json larr = json::array();
  for (const std::string& iid : links)
    if (!iid.empty())
      larr.push_back(iid);
  if (!larr.empty())
    doc["links"] = std::move(larr);
  return doc.dump();
}

std::vector<std::string> gallery_lens_from_json(const std::string& body) {
  std::vector<std::string> order;
  json doc;
  try {
    doc = json::parse(body);
  } catch (...) {
    return order;  // blank / unparseable → empty (caller defaults to whole pool)
  }
  if (!doc.is_object() || !doc.contains("order") || !doc["order"].is_array())
    return order;
  for (const auto& e : doc["order"])
    if (e.is_string() && !e.get<std::string>().empty())
      order.push_back(e.get<std::string>());
  return order;
}

std::vector<std::string> gallery_links_from_json(const std::string& body) {
  std::vector<std::string> links;
  json doc;
  try {
    doc = json::parse(body);
  } catch (...) {
    return links;  // blank / unparseable / no links key → empty
  }
  if (!doc.is_object() || !doc.contains("links") || !doc["links"].is_array())
    return links;
  for (const auto& e : doc["links"])
    if (e.is_string() && !e.get<std::string>().empty())
      links.push_back(e.get<std::string>());
  return links;
}

// ── §11 — collector membership operations ────────────────────────────────────

bool gallery_member_add(std::vector<std::string>& members, const std::string& iid) {
  if (iid.empty())
    return false;
  for (const std::string& e : members)
    if (e == iid)
      return false;            // already hung — no duplicate
  members.push_back(iid);
  return true;
}

bool gallery_member_remove(std::vector<std::string>& members, const std::string& iid) {
  for (std::size_t i = 0; i < members.size(); ++i)
    if (members[i] == iid) {
      members.erase(members.begin() + static_cast<std::ptrdiff_t>(i));
      return true;
    }
  return false;
}

bool gallery_member_move(std::vector<std::string>& members,
                         std::size_t from, std::size_t to) {
  const std::size_t n = members.size();
  if (n < 2)
    return false;
  if (from >= n) from = n - 1;
  if (to   >= n) to   = n - 1;
  if (from == to)
    return false;
  std::string v = std::move(members[from]);
  members.erase(members.begin() + static_cast<std::ptrdiff_t>(from));
  members.insert(members.begin() + static_cast<std::ptrdiff_t>(to), std::move(v));
  return true;
}

std::vector<std::string>
gallery_members_reconcile(const std::vector<std::string>& members,
                          const ImagePool& pool) {
  std::vector<std::string> out;
  out.reserve(members.size());
  for (const std::string& iid : members)
    if (pool.find(iid) != nullptr)   // exists (live OR soft-deleted) → retained
      out.push_back(iid);            // a PURGED fragment (not found) is dropped
  return out;
}

std::vector<std::string>
gallery_members_visible(const std::vector<std::string>& members,
                        const ImagePool& pool) {
  std::vector<std::string> out;
  out.reserve(members.size());
  for (const std::string& iid : members) {
    const ImageFragment* f = pool.find(iid);
    if (f && !f->deleted)            // exists AND not hidden → drawn
      out.push_back(iid);
  }
  return out;
}

// ── §12 — instrument→object association scan (the Map projection's 4th source) ─
std::vector<std::string>
instrument_object_links(const std::string& template_id, const std::string& body) {
  std::vector<std::string> out;

  if (template_id == kGalleryTemplateId) {
    // The gallery's own §11 links (the surface persists these in its body).
    for (const std::string& iid : gallery_links_from_json(body))
      push_unique(out, iid);

  } else if (template_id == kMindMapTemplateId) {
    // The map is ABOUT its subject_iids, and each Anchor node points at an
    // object — both are forward refs to real objects (the doc never owns truth).
    try {
      const CMMDoc d = cmm_from_json(json::parse(body));
      for (const std::string& s : d.subject_iids)
        push_unique(out, s);
      for (const CMMNode& n : d.nodes)
        if (n.kind == CMMNodeKind::Anchor && !n.iid.empty())
          push_unique(out, n.iid);
    } catch (...) {
      // a blank / unparseable map body is a map about nothing — no links.
    }
  }
  // Journal + every other Reference form: no body-stored object links (a
  // journal's entry links are prose, already in the backlink index).
  return out;
}

}  // namespace Folio
