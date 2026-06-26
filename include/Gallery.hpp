#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Gallery.hpp — the Gallery LENS over the image pool (DESIGN_gallery.md).
//
// The pool (ImagePool) owns the image data; the Gallery owns NOTHING — it is a
// read-only projection: "give me the pool's fragments, arranged as a wall,
// optionally filtered by what they link to" (§1). A gallery node carries only a
// lens definition (order + filter); deleting it deletes a view, not an image.
//
// Routing mirrors the Mind Map / Journal: a Reference node whose template_id is
// the reserved marker below routes to the owned Gallery surface in Write mode
// (kMindMapTemplateId / kJournalTemplateId are the siblings).
//
// WHAT THIS SLICE BUILDS (pure, fork-free, sandbox-proven):
//   • the routing constant;
//   • the two LINK-direction reads that make "one fragment, surfaced from both
//     sides" literal — the lightbox's object chips (image → its objects) and the
//     reverse strip on a character/place (object → its images). Both are pure
//     reads over the typed edges StoryGraph::edges_from_backlinks already
//     produces from the EXISTING backlink index — no new edge store (§2);
//   • prev/next wandering over an ordered fragment set (the lightbox).
//
// DESIGN BLOCK-IN — NOT YET WIRED (the one open structural fork):
//   WHERE the pool's JSON persists in the .folio bundle. Because the Gallery is
//   a lens and the fragments are shared across lenses, the pool CANNOT live in
//   one gallery node's content body (that would make a node own the data and
//   break "delete the view, not the image"). The recommendation is a single
//   project-level pool section (mirroring ObjectStore), with each gallery node's
//   body holding only its lens definition (manual wall order + optional
//   object-filter). The wall-ordering / filter query is blocked in below and
//   lands once that home is chosen — it is the only piece gated on the fork.
//
// Pure: <string>, <vector>, StoryGraph (pure StoryEdge), ImagePool. GTK-free.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstddef>
#include <string>
#include <vector>

#include "ImagePool.hpp"
#include "StoryGraph.hpp"   // StoryEdge — the typed edge already over m_backlinks

namespace Folio {

// Front-door sentinel: a Reference whose template_id is this reserved marker
// routes to the owned Gallery surface. Like the Mind Map's, it is NOT a
// registered built-in floor type.
inline constexpr const char* kGalleryTemplateId = "gallery";

// ── Link-direction reads (pure; over the existing typed edges) ───────────────
// An image-fragment is the SOURCE of an edge to an object it depicts
// (ast_… → chr_…/plc_…/scn_…), exactly as the conversation framed it
// ("this face → these 3 characters"). Both reads dedupe and preserve first-seen
// order, and ignore edges that do not touch the queried iid.

// Lightbox chips: the objects this image links to (forward edges from the image).
std::vector<std::string>
gallery_objects_of(const std::vector<StoryEdge>& edges,
                   const std::string& image_iid);

// Reverse strip on a character/place: the images that link to this object
// (edges whose target is the object AND whose source is an asset iid). This is
// the "images-that-point-at-me" view — computed, never stored (§3).
std::vector<std::string>
gallery_images_of(const std::vector<StoryEdge>& edges,
                  const std::string& object_iid);

// ── Lightbox wandering (pure; over an already-ordered fragment set) ──────────
// `ordered` is the wall's iid order (live fragments). Returns the index after /
// before `current`, wrapping, or -1 if the set is empty / current is absent.
// Wrapping makes prev/next a closed loop the writer can wander (§3).
int gallery_next_index(const std::vector<std::string>& ordered,
                       const std::string& current_iid);
int gallery_prev_index(const std::vector<std::string>& ordered,
                       const std::string& current_iid);

// ── Lens definition (the gallery node's BODY) ────────────────────────────────
// A gallery node owns only its lens-def — the wall ORDER (a list of fragment
// iids), with filter fields to be appended later. Stored as {"order":[...]}.
// Tolerant parse: a blank / unparseable / order-less body yields an empty order,
// which the surface reads as "default to the whole pool" (a fresh gallery shows
// everything until the writer reorders or filters). Pure; sandbox-tested.
std::string gallery_lens_to_json(const std::vector<std::string>& order,
                                 const std::vector<std::string>& links = {});
std::vector<std::string> gallery_lens_from_json(const std::string& body);

// ── §11 — gallery→object association (the gallery points at objects) ─────────
// A gallery can be ABOUT objects — "this Christianity wall is associated with the
// Jesus character." This mirrors the image→object link exactly, one level up: the
// gallery NODE owns a list of object iids, persisted in its body alongside the
// member order ({"order":[...], "links":[...]} — the "links" key is omitted when
// empty, so a link-less gallery's body is unchanged). Mutating the list reuses the
// generic gallery_member_add / gallery_member_remove (an iid list is an iid list).
//
// Forward read = this list (the objects a gallery is about). The reverse —
// "galleries about this object" — is a binder scan over gallery nodes (Editor-
// side: only the node layer knows which References are galleries), not a pure read
// here, so no edge-graph projection is needed for the association.
std::vector<std::string> gallery_links_from_json(const std::string& body);

// ── §12 — instrument→object association (the Map edge projection's 4th source) ─
// An owned instrument (Gallery, Mind Map) can be ABOUT project objects, with the
// association stored in its node BODY as structured JSON — NOT prose, so the
// prose-link backlink index never sees it, and the Map therefore never drew it.
// This pure read takes a node's template_id + body and returns the object iids it
// is about, letting StoryGraph::edges_from_backlinks project instrument-node →
// object edges as a fourth source. Both endpoints are real binder nodes, so the
// Map renders the edge and its hover highlight/dim light it up with NO view-side
// change — the Map stays a view that scans.
//   • Gallery  → its body `links` (the §11 association the surface writes).
//   • Mind Map → its CMMDoc `subject_iids` PLUS every Anchor node's target iid.
//   • Journal / anything else → none: a journal's entry links are prose
//     folio-links, already covered by the backlink index (source 1).
// Deduped, first-seen order; a blank / unparseable body yields no links.
std::vector<std::string>
instrument_object_links(const std::string& template_id, const std::string& body);

// ── §11 — the gallery as a COLLECTOR (membership operations) ─────────────────
// A gallery's body is now an EXPLICIT member list: the fragment iids hung on that
// wall, in wall order (the same {"order":[...]} cell the codec above persists —
// the SHAPE is unchanged; what changes is the meaning, "empty = an empty wall,"
// not "show the whole pool"). These pure operations are the only mutators the
// collector needs; the GTK doors (add-existing picker, drag-reorder, remove) call
// them, then re-persist via gallery_lens_to_json.
//
// An image hangs on many walls at once — adding the same iid to a second wall is
// the same image on two walls, never a copy (DESIGN_gallery §11b). So these are
// list ops over iids; no bytes, no pool mutation.

// Append `iid` to the wall (dedupe, first-seen order preserved). No-op on an empty
// iid or one already hung. Returns true iff the member list changed.
bool gallery_member_add(std::vector<std::string>& members, const std::string& iid);

// Remove `iid` from the wall. Returns true iff a member was removed.
bool gallery_member_remove(std::vector<std::string>& members, const std::string& iid);

// Move the member at index `from` to index `to` (drag-to-reorder). Both indices
// are clamped into range; a no-op (equal, or an empty list) returns false.
// Returns true iff the order changed.
bool gallery_member_move(std::vector<std::string>& members,
                         std::size_t from, std::size_t to);

// Reconcile a persisted member list against the live pool: DROP iids whose
// fragment no longer exists (purged), preserving order. A SOFT-DELETED fragment
// is RETAINED as a member (its iid stays, hidden from the rendered wall but
// restored to its place when the fragment is) — §11b/§11d. Use on load/save so a
// purge can't leave a dangling member, while soft-delete stays recoverable.
std::vector<std::string>
gallery_members_reconcile(const std::vector<std::string>& members,
                          const ImagePool& pool);

// The VISIBLE wall: members that exist in the pool AND are not soft-deleted, in
// member order. This is what the surface renders (reconcile keeps the stored
// membership honest; visible is the drawn subset).
std::vector<std::string>
gallery_members_visible(const std::vector<std::string>& members,
                        const ImagePool& pool);

}  // namespace Folio
