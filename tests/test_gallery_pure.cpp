// ─────────────────────────────────────────────────────────────────────────────
// test_gallery_pure.cpp — sandbox proof of the Gallery pure layer (s57).
//
// Covers the three GTK-free cores cut this slice, before any pixbuf / bundle
// I/O is wired:
//   • ImageNormalize  — guard (byte + megapixel decode-bomb), downscale-only
//                       long-edge fit, format-by-content, full plan.
//   • ImagePool       — add/mint, soft-delete/restore, live_view, JSON round-trip
//                       (incl. blank → empty and unparseable → empty fallback).
//   • Gallery (lens)  — link-direction reads (image→objects chips,
//                       object→images reverse strip, asset-only filter on the
//                       reverse side), and wrapping prev/next.
//
// Sandbox (g++):
/*
  g++ -std=c++20 -Wall -Wextra -I include -I /home/claude/sbox \
      tests/test_gallery_pure.cpp \
      src/ImageNormalize.cpp src/ImagePool.cpp src/Gallery.cpp src/Iid.cpp \
      -o /tmp/test_gallery_pure && /tmp/test_gallery_pure

  // Fedora (clang++), same set:
  clang++ -std=c++20 -Wall -Wextra -Werror -I include \
      tests/test_gallery_pure.cpp \
      src/ImageNormalize.cpp src/ImagePool.cpp src/Gallery.cpp src/Iid.cpp \
      -o /tmp/test_gallery_pure && /tmp/test_gallery_pure
*/
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdio>
#include <string>
#include <vector>

#include "Gallery.hpp"
#include "ImageNormalize.hpp"
#include "ImagePool.hpp"
#include "StoryGraph.hpp"

using namespace Folio;

static int g_pass = 0, g_fail = 0;
static void check(bool cond, const char* what) {
  if (cond) { ++g_pass; }
  else { ++g_fail; std::printf("  FAIL: %s\n", what); }
}

// ── ImageNormalize ────────────────────────────────────────────────────────────
static void test_normalize() {
  std::printf("ImageNormalize\n");
  NormalizePolicy pol;  // defaults: max 2048, q85, thumb 512, 40MB, lossless off

  // Guard — byte ceiling.
  {
    SourceProbe s; s.width = 100; s.height = 100;
    s.bytes = 41LL * 1024 * 1024;             // 41 MB > 40
    check(!guard_import(s, pol).ok, "rejects over byte ceiling");
    s.bytes = 39LL * 1024 * 1024;
    check(guard_import(s, pol).ok, "accepts under byte ceiling");
    check(!guard_import(s, pol).reason.empty() ||
              guard_import(s, pol).ok, "ok verdict has empty reason");
  }
  // Guard — megapixel decode-bomb, independent of bytes.
  {
    SourceProbe s; s.bytes = 200 * 1024;       // tiny file…
    s.width = 30000; s.height = 30000;         // …claiming 900 MP
    GuardResult g = guard_import(s, pol);
    check(!g.ok, "rejects decode-bomb by pixel count");
    check(!g.reason.empty(), "decode-bomb rejection has a reason");
    s.width = 7000; s.height = 7000;           // 49 MP — under 50
    check(guard_import(s, pol).ok, "accepts just under the MP cap");
  }
  // Guard — width*height must not overflow int (computed in 64-bit).
  {
    SourceProbe s; s.bytes = 1024; s.width = 60000; s.height = 60000; // 3.6 GP
    check(!guard_import(s, pol).ok, "no int overflow in MP guard");
  }

  // Format by content.
  check(choose_format(false, pol) == OutFormat::Jpeg, "opaque → JPEG");
  check(choose_format(true,  pol) == OutFormat::Png,  "alpha → PNG");
  {
    NormalizePolicy ll = pol; ll.prefer_lossless = true;
    check(choose_format(false, ll) == OutFormat::Png, "prefer_lossless → PNG");
  }
  check(std::string(out_format_ext(OutFormat::Jpeg)) == "jpg", "jpeg ext");
  check(std::string(out_format_ext(OutFormat::Png)) == "png", "png ext");
  check(std::string(out_format_name(OutFormat::Jpeg)) == "jpeg", "jpeg name");

  // Downscale (long-edge, downscale-only, aspect-preserving).
  {
    Dim d = fit_long_edge(4000, 3000, 2048);   // long edge 4000 → 2048
    check(d.w == 2048, "landscape long edge capped to 2048");
    check(d.h == 1536, "landscape short edge scaled with aspect");
    Dim e = fit_long_edge(3000, 4000, 2048);   // portrait
    check(e.h == 2048 && e.w == 1536, "portrait capped on the long (height) edge");
    Dim f = fit_long_edge(1000, 800, 2048);    // already small
    check(f.w == 1000 && f.h == 800, "never upscales below cap");
    Dim g = fit_long_edge(5000, 1, 2048);      // extreme aspect
    check(g.w == 2048 && g.h == 1, "clamps short edge to >= 1px");
    Dim h = fit_long_edge(4000, 3000, 0);      // no cap
    check(h.w == 4000 && h.h == 3000, "cap<=0 means no scaling");
  }

  // Full plan: asset capped to max_dim, thumb derived from the NORMALIZED dims.
  {
    SourceProbe s; s.bytes = 5 * 1024 * 1024;
    s.width = 4000; s.height = 3000; s.has_alpha = false;
    NormalizePlan p = plan_normalize(s, pol);
    check(p.format == OutFormat::Jpeg, "plan: opaque photo → JPEG");
    check(p.asset_dim.w == 2048 && p.asset_dim.h == 1536, "plan: asset capped");
    // thumb cap 512 applied to 2048×1536 → 512×384
    check(p.thumb_dim.w == 512 && p.thumb_dim.h == 384, "plan: thumb from normalized");
    check(p.quality == 85, "plan: carries JPEG quality");
  }
  {
    SourceProbe s; s.bytes = 1024;
    s.width = 300; s.height = 300; s.has_alpha = true; s.is_animated = true;
    NormalizePlan p = plan_normalize(s, pol);
    check(p.format == OutFormat::Png, "plan: alpha → PNG");
    check(p.asset_dim.w == 300 && p.asset_dim.h == 300, "plan: small image untouched");
    check(p.flatten_animation, "plan: animated source flagged for first-frame");
  }

  // caption from filename
  check(caption_from_filename("/x/y/tavern_exterior.jpg") == "Tavern exterior",
        "underscores → spaces, capitalised");
  check(caption_from_filename("the-old-bridge.png") == "The old bridge",
        "dashes → spaces");
  check(caption_from_filename("/a/b/Anna Portrait.JPG") == "Anna Portrait",
        "spaces kept, extension stripped");
  check(caption_from_filename("a__b--c.png") == "A b c", "separator runs collapse");
  check(caption_from_filename("IMG_2024.jpg").empty(), "camera IMG_ → no caption");
  check(caption_from_filename("DSC00123.JPG").empty(), "camera DSC → no caption");
  check(caption_from_filename("PXL_20240615.jpg").empty(), "phone PXL_ → no caption");
  check(caption_from_filename("20240615_120000.png").empty(), "timestamp → no caption");
  check(caption_from_filename("Screenshot 2026-06-25.png").empty(), "screenshot → no caption");
  check(caption_from_filename("P1010101.jpg").empty(), "too few letters → no caption");
  check(caption_from_filename("") == "", "empty path → empty caption");
  check(caption_from_filename("just_a_name") == "Just a name", "no extension is fine");
}

// ── ImagePool ───────────────────────────────────────────────────────────────
static void test_pool() {
  std::printf("ImagePool\n");
  ImagePool pool;

  ImageFragment a;
  a.ext = "jpg"; a.caption = "the tavern"; a.format = "jpeg";
  a.width = 2048; a.height = 1365; a.bytes = 312000; a.hash = "abc123";
  std::string ia = pool.add(a);                 // iid minted
  check(!ia.empty() && iid_kind_of(ia) == IidKind::Asset, "add mints an ast_ iid");

  ImageFragment b; b.iid = "ast_fixed001"; b.ext = "png"; b.format = "png";
  b.width = 512; b.height = 512;
  std::string ib = pool.add(b);
  check(ib == "ast_fixed001", "add honours a supplied iid");
  check(pool.size() == 2, "pool holds two fragments");

  check(pool.find(ia) != nullptr, "find by iid");
  check(pool.find("ast_nope") == nullptr, "find unknown → null");

  check(pool.live_view().size() == 2, "both live initially");
  pool.soft_delete(ia);
  check(pool.find(ia)->deleted, "soft-delete marks, retains");
  check(pool.live_view().size() == 1, "soft-deleted drops from live view");
  check(pool.size() == 2, "soft-delete does not drop the record");
  pool.restore(ia);
  check(!pool.find(ia)->deleted && pool.live_view().size() == 2, "restore recovers");

  // purge = complete removal of the record (the caller deletes the files)
  ImageFragment c; c.iid = "ast_temp99"; c.ext = "jpg";
  pool.add(c);
  check(pool.size() == 3 && pool.find("ast_temp99"), "added throwaway for purge");
  check(pool.purge("ast_temp99"), "purge erases an existing fragment");
  check(pool.find("ast_temp99") == nullptr, "purged fragment is gone");
  check(pool.size() == 2, "purge shrinks the pool (unlike soft-delete)");
  check(!pool.purge("ast_nope"), "purge of unknown iid → false");

  // object links: helpers + dedup + round-trip + edge resolution both ways
  ImageFragment lk; lk.iid = "ast_link01"; lk.ext = "jpg";
  pool.add(lk);
  check(pool.link_object("ast_link01", "chr_anna"), "link_object adds a link");
  check(!pool.link_object("ast_link01", "chr_anna"), "duplicate link → false (dedup)");
  check(pool.link_object("ast_link01", "plc_keep"), "link_object adds a second");
  check(!pool.link_object("ast_nope", "chr_anna"), "link on unknown image → false");
  check(!pool.link_object("ast_link01", ""), "link with empty object → false");
  check(pool.find("ast_link01")->links.size() == 2, "two links recorded");
  check(pool.unlink_object("ast_link01", "chr_anna"), "unlink removes a link");
  check(!pool.unlink_object("ast_link01", "chr_anna"), "unlink of absent → false");
  check(pool.find("ast_link01")->links.size() == 1, "one link remains");
  {
    auto rt = pool.to_json();
    ImagePool back = ImagePool::from_json(rt);
    const ImageFragment* f = back.find("ast_link01");
    check(f && f->links.size() == 1 && f->links[0] == "plc_keep",
          "links survive JSON round-trip");
  }

  // forward (objects of an image) + reverse (images of an object) over edges
  std::vector<StoryEdge> img_edges = {
      {"ast_link01", "chr_anna", EdgeKind::Involves, "", ""},
      {"ast_link01", "plc_keep", EdgeKind::SetIn, "", ""},
      {"scn_001",    "chr_anna", EdgeKind::Involves, "", ""}};  // a prose link, not an image
  auto objs = gallery_objects_of(img_edges, "ast_link01");
  check(objs.size() == 2, "gallery_objects_of: an image's two linked objects");
  auto imgs = gallery_images_of(img_edges, "chr_anna");
  check(imgs.size() == 1 && imgs[0] == "ast_link01",
        "gallery_images_of: only the asset source counts (prose link excluded)");
  pool.purge("ast_link01");  // restore the 2-fragment state the next checks expect

  // JSON round-trip (bare array; soft-deleted fragments ARE written).
  pool.soft_delete(ib);
  nlohmann::json body = pool.to_json();
  check(body.is_array() && body.size() == 2, "to_json is a bare fragment array");
  ImagePool rt = ImagePool::from_json(body);
  check(rt.size() == 2, "round-trip keeps both (incl. soft-deleted)");
  const ImageFragment* ra = rt.find(ia);
  check(ra && ra->caption == "the tavern" && ra->width == 2048 &&
            ra->hash == "abc123" && !ra->deleted,
        "round-trip preserves recorded fields");
  const ImageFragment* rb = rt.find(ib);
  check(rb && rb->deleted && rb->format == "png", "round-trip preserves soft-delete + format");

  // size = long edge (orientation-independent — the unification knob)
  ImageFragment land; land.width = 2048; land.height = 1365;
  check(land.long_edge() == 2048 && land.short_edge() == 1365 && land.is_landscape(),
        "landscape: long edge = width");
  ImageFragment port; port.width = 1365; port.height = 2048;
  check(port.long_edge() == 2048 && port.short_edge() == 1365 && !port.is_landscape(),
        "portrait: long edge = height");
  ImageFragment sq; sq.width = 800; sq.height = 800;
  check(sq.long_edge() == 800 && sq.is_landscape(), "square: long edge well-defined, treated landscape");
  check(ImagePool::from_json(nlohmann::json::array()).size() == 0, "empty array → empty pool");
  check(ImagePool::from_json(nlohmann::json()).size() == 0, "null → empty pool");
  check(ImagePool::from_json(nlohmann::json::object()).size() == 0, "object (non-array) → empty");
  check(ImagePool::from_json(nlohmann::json("nope")).size() == 0, "string (non-array) → empty");

  // Tolerant read: a fragment missing newer keys still loads.
  nlohmann::json legacy = nlohmann::json::array(
      {{{"iid", "ast_old"}, {"ext", "jpg"}}});
  ImagePool lp = ImagePool::from_json(legacy);
  check(lp.size() == 1 && lp.find("ast_old") &&
            lp.find("ast_old")->width == 0 && !lp.find("ast_old")->deleted,
        "tolerant read of a minimal fragment");
  // An entry with no iid references no asset → skipped. A non-object element too.
  nlohmann::json noid = nlohmann::json::array(
      {{{"ext", "jpg"}}, "garbage", {{"iid", "ast_keep"}}});
  check(ImagePool::from_json(noid).size() == 1, "fragment without iid / non-object skipped");
}

// ── Gallery lens (link-direction reads + prev/next) ──────────────────────────
static void test_lens() {
  std::printf("Gallery lens\n");

  // Edges: image ast_face → two characters and a place; an unrelated edge; and a
  // non-asset source pointing at chr_anna (must NOT count as one of her images).
  std::vector<StoryEdge> edges;
  edges.push_back({"ast_face", "chr_anna", EdgeKind::Involves, "", ""});
  edges.push_back({"ast_face", "chr_boris", EdgeKind::Involves, "", ""});
  edges.push_back({"ast_face", "plc_inn",  EdgeKind::SetIn,    "", ""});
  edges.push_back({"ast_face", "chr_anna", EdgeKind::Reference, "", ""}); // dup target
  edges.push_back({"ast_door", "chr_anna", EdgeKind::Involves, "", ""});
  edges.push_back({"scn_open", "chr_anna", EdgeKind::Reference, "", ""}); // non-asset src

  // image → its objects (lightbox chips), deduped, first-seen order.
  auto objs = gallery_objects_of(edges, "ast_face");
  check(objs.size() == 3, "image links to 3 distinct objects (dup collapsed)");
  check(objs[0] == "chr_anna" && objs[1] == "chr_boris" && objs[2] == "plc_inn",
        "chips preserve first-seen order");
  check(gallery_objects_of(edges, "ast_none").empty(), "image with no edges → no chips");
  check(gallery_objects_of(edges, "").empty(), "empty image iid → no chips");

  // object → its images (reverse strip): asset sources only, deduped.
  auto imgs = gallery_images_of(edges, "chr_anna");
  check(imgs.size() == 2, "anna's images = the two asset sources (scn_ excluded)");
  check(imgs[0] == "ast_face" && imgs[1] == "ast_door", "reverse strip order");
  check(gallery_images_of(edges, "plc_inn").size() == 1, "place reverse strip");
  check(gallery_images_of(edges, "chr_nobody").empty(), "object with no images");

  // prev/next wandering (wrapping).
  std::vector<std::string> wall = {"ast_a", "ast_b", "ast_c"};
  check(gallery_next_index(wall, "ast_a") == 1, "next: a→b");
  check(gallery_next_index(wall, "ast_c") == 0, "next wraps: c→a");
  check(gallery_prev_index(wall, "ast_a") == 2, "prev wraps: a→c");
  check(gallery_prev_index(wall, "ast_b") == 0, "prev: b→a");
  check(gallery_next_index(wall, "ast_x") == -1, "absent current → -1");
  check(gallery_next_index({}, "ast_a") == -1, "empty wall → -1");

  // lens-def codec (the gallery node's body)
  std::vector<std::string> order = {"ast_a", "ast_b", "ast_c"};
  std::string lbody = gallery_lens_to_json(order);
  check(gallery_lens_from_json(lbody) == order, "lens-def order round-trips");
  check(gallery_lens_from_json("").empty(), "blank lens body → empty order");
  check(gallery_lens_from_json("garbage").empty(), "unparseable lens body → empty");
  check(gallery_lens_from_json("{}").empty(), "missing order key → empty");
  check(gallery_lens_from_json("{\"order\":[\"ast_x\",\"\",\"ast_y\"]}").size() == 2,
        "blank iids dropped from lens order");
}

int main() {
  test_normalize();
  test_pool();
  test_lens();
  std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
