// ─────────────────────────────────────────────────────────────────────────────
// test_bundle_assets.cpp — sandbox proof of the Gallery bundle layer (s58).
//
// Verifies the asset-class extensions to ProjectBundle, with real temp dirs:
//   • thumb_path mirrors asset_path;
//   • explode CARRIES assets/ + thumbs/ forward across the rebuild-and-swap, so
//     a second save does NOT destroy imported images (the latent-bug fix);
//   • the top-level `images` pool section rides into project.json untouched and
//     round-trips through implode;
//   • the §9 asset reconcile reports missing (fragment → absent asset), drift
//     (hash mismatch, file wins), and orphan binaries (asset/thumb files no
//     fragment references), while a clean fragment stays out of every bucket;
//   • a missing THUMB is NOT an error (regenerable cache).
//
// Sandbox (g++):
/*
  g++ -std=c++20 -Wall -Wextra -I include -I /home/claude/sbox \
      tests/test_bundle_assets.cpp src/ProjectBundle.cpp src/Iid.cpp \
      -o /tmp/test_bundle_assets && /tmp/test_bundle_assets

  // Fedora (clang++), same set:
  clang++ -std=c++20 -Wall -Wextra -Werror -I include \
      tests/test_bundle_assets.cpp src/ProjectBundle.cpp src/Iid.cpp \
      -o /tmp/test_bundle_assets && /tmp/test_bundle_assets
*/
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "ProjectBundle.hpp"

using namespace Folio;
namespace fs = std::filesystem;
using json = nlohmann::json;

static int g_pass = 0, g_fail = 0;
static void check(bool cond, const char* what) {
  if (cond) { ++g_pass; }
  else { ++g_fail; std::printf("  FAIL: %s\n", what); }
}

static void write_bin(const fs::path& p, const std::string& bytes) {
  fs::create_directories(p.parent_path());
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  f << bytes;
}
static std::string read_bin(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}
static bool in_bucket(const std::vector<ReconcileReport::Missing>& v,
                      const std::string& iid) {
  for (const auto& e : v)
    if (e.iid == iid) return true;
  return false;
}
static bool in_bucket(const std::vector<ReconcileReport::Orphan>& v,
                      const std::string& iid) {
  for (const auto& e : v)
    if (e.iid == iid) return true;
  return false;
}
static bool in_bucket(const std::vector<ReconcileReport::Drift>& v,
                      const std::string& iid) {
  for (const auto& e : v)
    if (e.iid == iid) return true;
  return false;
}

int main() {
  fs::path base = fs::temp_directory_path() / ("folio_sbox_" + std::to_string(::getpid()));
  fs::remove_all(base);
  fs::path root = base / "Novel.folio";

  // The bytes for the one clean asset; its recorded hash must match on disk.
  const std::string keep_bytes  = "\x89PNG\r\n fake-but-stable image bytes";
  const std::string keep_hash   = content_hash(keep_bytes);

  std::printf("path helpers\n");
  check(thumb_path(root, "ast_x", "jpg") == root / "thumbs" / "ast_x.jpg",
        "thumb_path composes thumbs/<iid>.<ext>");
  check(asset_path(root, "ast_x", ".png") == root / "assets" / "ast_x.png",
        "asset_path tolerates a leading dot in ext");

  // ── Build a blob with a scene + a 3-fragment image pool ────────────────────
  json blob;
  blob["folio_version"] = 5;
  blob["manuscript"] = json::array({
      {{"iid", "scn_aaa"}, {"kind", "scene"}, {"content", "hello world"}}});
  blob["images"] = json::array({
      {{"iid", "ast_keep"},  {"ext", "png"}, {"hash", keep_hash}, {"caption", "tavern"}},
      {{"iid", "ast_gone"},  {"ext", "png"}, {"hash", "feedface00000000"}},
      {{"iid", "ast_drift"}, {"ext", "jpg"}, {"hash", "0000000000000000"}}});

  std::printf("explode → images section + dirs\n");
  explode(blob, root);
  check(fs::is_directory(root / "assets"), "explode creates assets/");
  check(fs::is_directory(root / "thumbs"), "explode creates thumbs/");
  {
    json man = json::parse(read_bin(root / "project.json"));
    check(man.contains("images") && man["images"].is_array() &&
              man["images"].size() == 3,
          "images pool rides into project.json untouched");
    check(man["images"][0].value("caption", "") == "tavern",
          "fragment fields preserved in manifest");
  }

  // Simulate the import seam having written asset/thumb bytes into the LIVE
  // bundle (this is what the gdk-pixbuf slice will do).
  write_bin(root / "assets" / "ast_keep.png",  keep_bytes);          // clean
  write_bin(root / "assets" / "ast_drift.jpg", "different bytes!!");  // hash≠record
  write_bin(root / "thumbs" / "ast_keep.png",  "tiny-thumb-bytes");   // clean thumb
  // ast_gone.png deliberately NOT written → missing.
  write_bin(root / "assets" / "ast_orphan.jpg", "no fragment owns me"); // orphan asset
  write_bin(root / "thumbs" / "ast_stray.png",  "no fragment owns me"); // orphan thumb

  std::printf("re-explode → carry-forward (the latent-bug fix)\n");
  // Re-save: rebuild blob from a fresh implode-less in-memory copy is overkill;
  // we just re-explode the same blob. Without carry-forward this swap would wipe
  // every asset/thumb written above.
  explode(blob, root);
  check(fs::exists(root / "assets" / "ast_keep.png"),  "asset survives re-save");
  check(read_bin(root / "assets" / "ast_keep.png") == keep_bytes,
        "carried-forward asset bytes are intact");
  check(fs::exists(root / "thumbs" / "ast_keep.png"),  "thumb survives re-save");
  check(fs::exists(root / "assets" / "ast_orphan.jpg"),
        "even an orphan asset is carried (recovered, not silently dropped)");

  std::printf("implode → §9 asset reconcile\n");
  ReconcileReport rep;
  json back = implode(root, rep);

  check(back.contains("images") && back["images"].size() == 3,
        "images pool round-trips through implode");
  check(back["manuscript"][0].value("content", "") == "hello world",
        "scene content still reconstructed");

  check(in_bucket(rep.missing, "ast_gone"),   "missing asset reported");
  check(!in_bucket(rep.missing, "ast_keep"),  "clean asset not missing");
  check(in_bucket(rep.drifted, "ast_drift"),  "hash mismatch → drift");
  check(!in_bucket(rep.drifted, "ast_keep"),  "clean asset does not drift");
  check(in_bucket(rep.orphans, "ast_orphan"), "orphan asset swept");
  check(in_bucket(rep.orphans, "ast_stray"),  "orphan thumb swept");
  check(!in_bucket(rep.orphans, "ast_keep"),  "referenced asset is not an orphan");
  // A clean fragment's missing THUMB must NOT be an error: remove it and re-check.
  fs::remove(root / "thumbs" / "ast_keep.png");
  ReconcileReport rep2;
  implode(root, rep2);
  check(!in_bucket(rep2.missing, "ast_keep"),
        "a missing thumbnail is not a 'missing' error (regenerable)");

  check(!rep.clean(), "report is dirty (we planted faults)");

  fs::remove_all(base);
  std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
