// ─────────────────────────────────────────────────────────────────────────────
// test_object_image_strip.cpp — s70: pins the reverse read the Character/Place
// image strip stands on. No new pure logic ships in s70 (gallery_images_of is the
// shipped, 97-check-proven reverse read); this test fixes the EXACT contract the
// new ObjectForm strip provider consumes, so a future edge-source change can't
// quietly break it: asset sources only, target == the object, deduped, direction
// honoured (object→images, never image→object), and unrelated edges ignored.
// ─────────────────────────────────────────────────────────────────────────────
/*
  # sandbox (g++)
  g++ -std=c++20 -I/home/claude/sbox -I/home/claude/work/s70/include \
      /home/claude/work/sbox/test_object_image_strip.cpp \
      /home/claude/work/s70/src/Gallery.cpp \
      /home/claude/work/s70/src/Iid.cpp \
      -o /home/claude/work/sbox/test_object_image_strip && \
  /home/claude/work/sbox/test_object_image_strip

  # Fedora (clang++) — same, swap the compiler / paths:
  clang++ -std=c++20 -Iinclude tests/test_object_image_strip.cpp \
      src/Gallery.cpp src/Iid.cpp -o /tmp/tois && /tmp/tois
*/
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "Gallery.hpp"
#include "Iid.hpp"

using namespace Folio;

static int g_checks = 0;
static void check(bool cond, const std::string& what) {
    ++g_checks;
    if (!cond) { std::cerr << "FAIL: " << what << "\n"; std::abort(); }
}

// Build a real iid of a given kind (well-formed prefix) for the test fixtures.
static std::string iid(IidKind k) { return make_iid(k); }

int main() {
    const std::string chr = iid(IidKind::Character);
    const std::string plc = iid(IidKind::Place);
    const std::string a1  = iid(IidKind::Asset);
    const std::string a2  = iid(IidKind::Asset);
    const std::string a3  = iid(IidKind::Asset);
    const std::string scn = iid(IidKind::Scene);

    // Edges as edges_from_backlinks would project them: image (ast_) → object.
    std::vector<StoryEdge> edges = {
        {a1, chr, EdgeKind::Reference, "", ""},   // a1 depicts the character
        {a2, chr, EdgeKind::Reference, "", ""},   // a2 also depicts the character
        {a1, chr, EdgeKind::Reference, "", ""},   // duplicate → must dedup
        {a3, plc, EdgeKind::Reference, "", ""},   // a3 depicts the PLACE, not chr
        {chr, plc, EdgeKind::Reference, "", ""},  // object→object relation (not an image)
        {a2, scn, EdgeKind::Reference, "", ""},   // a2 also depicts a scene (other target)
    };

    // 1. The character's strip: asset sources pointing at chr, first-seen order, deduped.
    auto for_chr = gallery_images_of(edges, chr);
    check(for_chr.size() == 2, "chr sees exactly two images");
    check(for_chr[0] == a1 && for_chr[1] == a2, "chr images are a1 then a2 (first-seen, deduped)");

    // 2. The place's strip is independent — only a3 points at it.
    auto for_plc = gallery_images_of(edges, plc);
    check(for_plc.size() == 1 && for_plc[0] == a3, "plc sees only a3");

    // 3. Direction: the object→object relation (chr→plc) must NOT make plc list chr,
    //    because chr is not an asset source.
    for (const auto& s : for_plc)
        check(iid_kind_of(s) == IidKind::Asset, "every strip entry is an asset iid");
    for (const auto& s : for_chr)
        check(iid_kind_of(s) == IidKind::Asset, "every strip entry is an asset iid (chr)");

    // 4. An object nobody points at gets an empty strip (the section won't render).
    check(gallery_images_of(edges, iid(IidKind::Character)).empty(), "unlinked object → empty");
    check(gallery_images_of(edges, "").empty(), "empty iid → empty");

    // 5. Forward read is the mirror and unaffected: a2 depicts chr AND scn.
    auto a2_objs = gallery_objects_of(edges, a2);
    check(a2_objs.size() == 2, "a2 depicts two objects (forward)");
    check(a2_objs[0] == chr && a2_objs[1] == scn, "a2 → chr then scn");

    std::cout << "OK — " << g_checks << " checks passed\n";
    return 0;
}
