// ─────────────────────────────────────────────────────────────────────────────
// test_object_image_pure.cpp — s72 step 1: the pure seam of the portrait
// unification. Pins the DUAL-READ resolver (ast_ iid → assets/<iid>.<ext>;
// legacy path passthrough; dangling/deleted → "") and the long-edge display math
// (orientation-independent, downscale-only, aspect-preserving), before any pixbuf
// wiring touches the ObjectForm / avatar strip.
// ─────────────────────────────────────────────────────────────────────────────
/*
  # sandbox (g++)
  g++ -std=c++20 -I/home/claude/sbox -I/home/claude/work/s70/include \
      /home/claude/work/sbox/test_object_image_pure.cpp \
      /home/claude/work/s70/src/ObjectImage.cpp \
      /home/claude/work/s70/src/ImagePool.cpp \
      /home/claude/work/s70/src/ProjectBundle.cpp \
      /home/claude/work/s70/src/Iid.cpp \
      -o /home/claude/work/sbox/test_object_image_pure && \
  /home/claude/work/sbox/test_object_image_pure

  # Fedora (clang++)
  clang++ -std=c++20 -Iinclude tests/test_object_image_pure.cpp \
      src/ObjectImage.cpp src/ImagePool.cpp src/ProjectBundle.cpp src/Iid.cpp \
      -o /tmp/toip && /tmp/toip
*/
#include <cassert>
#include <iostream>
#include <string>

#include "ObjectImage.hpp"
#include "ImagePool.hpp"
#include "ProjectBundle.hpp"
#include "Iid.hpp"

using namespace Folio;

static int g_checks = 0;
static void check(bool cond, const std::string& what) {
    ++g_checks;
    if (!cond) { std::cerr << "FAIL: " << what << "\n"; std::abort(); }
}

int main() {
    const fs::path root = "/proj/My.folio";
    ImagePool pool;

    // A live JPEG fragment (recorded landscape dims).
    ImageFragment f;
    f.ext = "jpg"; f.width = 1600; f.height = 1200;
    const std::string aid = pool.add(f);           // mints + returns the ast_ iid
    check(image_value_is_asset(aid), "minted handle is an asset iid");
    check(!image_value_is_asset("/home/scott/face.png"), "external path is not an asset iid");
    check(!image_value_is_asset(""), "empty value is not an asset iid");

    // 1. Resolver — ast_ iid → assets/<iid>.<ext> (canonical asset_path).
    check(image_display_path(aid, pool, root) == asset_path(root, aid, "jpg").string(),
          "asset iid resolves to assets/<iid>.jpg");

    // 2. Resolver — legacy external path passes through verbatim (no migration).
    check(image_display_path("/home/scott/face.png", pool, root) == "/home/scott/face.png",
          "legacy external path passthrough");
    check(image_display_path("", pool, root).empty(), "unset value → empty");

    // 3. Resolver — an iid that names no fragment → "" (dangling portrait).
    const std::string ghost = make_iid(IidKind::Asset);
    check(image_display_path(ghost, pool, root).empty(), "unknown asset iid → empty");

    // 4. Resolver — a soft-deleted (hidden) fragment → "" (not a stale image).
    pool.soft_delete(aid);
    check(image_display_path(aid, pool, root).empty(), "soft-deleted fragment → empty");
    pool.restore(aid);
    check(!image_display_path(aid, pool, root).empty(), "restored fragment resolves again");

    // ── long-edge display math (§9) ──────────────────────────────────────────
    // 5. Landscape 1600×1200 at long=800 → 800×600 (downscale, aspect kept).
    {
        auto d = fit_long_edge(1600, 1200, 800);
        check(d.w == 800 && d.h == 600, "landscape 1600x1200 @800 → 800x600");
    }
    // 6. Portrait 1200×1600 at long=800 → 600×800 — SAME footprint as the
    //    landscape at the same setting (the orientation-independent guarantee).
    {
        auto d = fit_long_edge(1200, 1600, 800);
        check(d.w == 600 && d.h == 800, "portrait 1200x1600 @800 → 600x800");
        auto land = fit_long_edge(1600, 1200, 800);
        check(std::max(d.w, d.h) == std::max(land.w, land.h),
              "portrait and landscape share the long edge at one setting");
    }
    // 7. Square 1000×1000 at long=300 → 300×300.
    {
        auto d = fit_long_edge(1000, 1000, 300);
        check(d.w == 300 && d.h == 300, "square 1000 @300 → 300x300");
    }
    // 8. Downscale ONLY — requesting larger than recorded caps at recorded.
    {
        auto d = fit_long_edge(400, 300, 720);
        check(d.w == 400 && d.h == 300, "request beyond recorded → recorded (no upscale)");
    }
    // 9. Degenerate / clamp edges.
    check((fit_long_edge(0, 100, 300).w == 0) && (fit_long_edge(0, 100, 300).h == 0),
          "zero width → {0,0}");
    {
        auto d = fit_long_edge(1600, 1200, 0);   // requested clamps to 1
        check(d.w >= 1 && d.h >= 1, "requested<1 clamps, dims stay >=1");
    }

    std::cout << "OK — " << g_checks << " checks passed\n";
    return 0;
}
