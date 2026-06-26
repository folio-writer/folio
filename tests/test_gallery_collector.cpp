// ─────────────────────────────────────────────────────────────────────────────
// test_gallery_collector.cpp — proves the pure foundation the s75 surface relies
// on, with emphasis on the CURRENTLY-BROKEN round-trip: a gallery node's body
// must carry BOTH its member order AND its gallery->object links across a
// save/load. GallerySurface today persists with the one-arg codec (links
// defaulted to {}) and never loads links back, so any gallery->object
// association is silently wiped on the next reorder/import/delete. These tests
// pin the contract the wired surface must honour:
//
//   • order + links survive gallery_lens_to_json -> from_json (both keys);
//   • "links" stays omitted when empty (a link-less body is unchanged);
//   • collector model: an empty body yields an EMPTY order (no whole-pool
//     fallback — that is the surface's job to NOT re-add, but the codec must
//     report empty honestly);
//   • membership ops (add dedupe / remove / move) over an iid list;
//   • reconcile drops PURGED members but RETAINS soft-deleted ones;
//   • visible hides soft-deleted and purged, preserving order.
//
// Build+run (sandbox g++, vendored nlohmann at /home/claude/sbox):
/*
g++ -std=c++20 -I ../include -I /home/claude/sbox -o /tmp/tgc test_gallery_collector.cpp ../src/Gallery.cpp ../src/ImagePool.cpp ../src/Iid.cpp && /tmp/tgc
clang++ -std=c++20 -Wall -Wextra -Werror -I ../include $(pkg-config --cflags nlohmann_json) -o /tmp/tgc test_gallery_collector.cpp ../src/Gallery.cpp ../src/ImagePool.cpp ../src/Iid.cpp && /tmp/tgc
*/

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "Gallery.hpp"
#include "ImagePool.hpp"

using namespace Folio;

namespace {
int g_pass = 0;
void ok(bool cond, const char* what) {
  if (!cond) { std::cerr << "FAIL: " << what << "\n"; std::exit(1); }
  ++g_pass;
}

ImageFragment frag(const std::string& iid, bool deleted = false) {
  ImageFragment f;
  f.iid = iid;
  f.ext = "jpg";
  f.width = 1600; f.height = 1200;
  f.deleted = deleted;
  return f;
}
}  // namespace

int main() {
  // ── 1. The broken round-trip: order AND links survive the body codec ────────
  {
    std::vector<std::string> order{"ast_a", "ast_b", "ast_c"};
    std::vector<std::string> links{"chr_jesus", "plc_jerusalem"};
    const std::string body = gallery_lens_to_json(order, links);

    auto r_order = gallery_lens_from_json(body);
    auto r_links = gallery_links_from_json(body);
    ok(r_order == order, "order survives round-trip");
    ok(r_links == links, "links survive round-trip (the wiped-association bug)");
  }

  // ── 2. Empty links → "links" key omitted; body of a link-less gallery is the
  //      same shape it always was (backward-compatible) ─────────────────────────
  {
    std::vector<std::string> order{"ast_a"};
    const std::string with_empty = gallery_lens_to_json(order, {});
    const std::string one_arg    = gallery_lens_to_json(order);   // defaulted links
    ok(with_empty == one_arg, "empty links omitted — link-less body unchanged");
    ok(with_empty.find("links") == std::string::npos, "no links key when empty");
    ok(gallery_links_from_json(with_empty).empty(), "link-less body reads no links");
  }

  // ── 3. Collector model: an empty / blank / junk body yields an EMPTY order.
  //      (The surface must treat empty as an empty wall — NOT fall back to the
  //      whole pool. The codec's job is to report empty honestly.) ─────────────
  {
    ok(gallery_lens_from_json("").empty(),            "blank body → empty order");
    ok(gallery_lens_from_json("not json").empty(),    "junk body → empty order");
    ok(gallery_lens_from_json("{}").empty(),          "no-order body → empty order");
    ok(gallery_lens_from_json(R"({"links":["chr_x"]})").empty(),
       "links-only body → empty order (a wall can be empty yet about an object)");
    // …and that links-only body still surfaces its links:
    ok(gallery_links_from_json(R"({"links":["chr_x"]})") == std::vector<std::string>{"chr_x"},
       "links-only body → its links");
  }

  // ── 4. Membership ops over an iid list (add dedupe, remove, move) ────────────
  {
    std::vector<std::string> m;
    ok(gallery_member_add(m, "ast_a"),  "add new returns true");
    ok(gallery_member_add(m, "ast_b"),  "add second");
    ok(!gallery_member_add(m, "ast_a"), "add dup returns false (same image, one wall)");
    ok(!gallery_member_add(m, ""),      "add empty returns false");
    ok((m == std::vector<std::string>{"ast_a", "ast_b"}), "membership after adds");

    ok(gallery_member_move(m, 1, 0), "move reorders");
    ok((m == std::vector<std::string>{"ast_b", "ast_a"}), "order after move");

    ok(gallery_member_remove(m, "ast_b"),  "remove present returns true");
    ok(!gallery_member_remove(m, "ast_z"), "remove absent returns false");
    ok((m == std::vector<std::string>{"ast_a"}), "membership after remove");
  }

  // ── 5. reconcile vs visible against a live pool ──────────────────────────────
  //   pool: ast_live (live), ast_soft (soft-deleted); ast_gone never added (purged)
  {
    ImagePool pool;
    pool.add(frag("ast_live"));
    pool.add(frag("ast_soft", /*deleted=*/true));

    std::vector<std::string> members{"ast_live", "ast_soft", "ast_gone"};

    auto rec = gallery_members_reconcile(members, pool);
    ok((rec == std::vector<std::string>{"ast_live", "ast_soft"}),
       "reconcile drops PURGED, retains soft-deleted (recoverable)");

    auto vis = gallery_members_visible(members, pool);
    ok((vis == std::vector<std::string>{"ast_live"}),
       "visible draws only live (hides soft-deleted + purged)");
  }

  std::cout << "ALL " << g_pass << " CHECKS PASSED\n";
  return 0;
}
