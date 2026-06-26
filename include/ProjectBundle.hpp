#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ProjectBundle.hpp — the v5 .folio bundle format (s19 file restructure)
//
// WHAT THIS IS: the seam that turns the single-JSON v4 project blob into a
// structured, versioned, iid-keyed BUNDLE on disk (DESIGN_s19 §3). It is the
// foundation every exporter, the publisher hand-off, and font/asset travel rest
// on — see the design spec.
//
// THE SEAM (deliberately pure, like CompileFormatIO was): this layer does NOT
// know about DocumentModel/BinderNode. It transforms the *in-memory project
// JSON* (exactly what DocumentModel::save_to already builds, and what
// load_from already parses) to and from the on-disk bundle:
//
//     DocumentModel  ──to_json()──▶  json blob  ──explode()──▶   bundle/ on disk
//     DocumentModel  ◀─from_json()── json blob  ◀──implode()──   bundle/ on disk
//
// Because it touches only <nlohmann/json>, <filesystem>, and Iid (no gtkmm, no
// glibmm), the whole core is g++-compilable and round-trip-verifiable in the
// Claude sandbox. DocumentModel's glue stays a few lines: build the blob as it
// always has, then hand it here.
//
// SOURCE-OF-TRUTH RULE — §3d option (C) HYBRID (ratified s19):
//   • The MANIFEST (project.json) is authoritative for STRUCTURE — the tree,
//     order, roles, metadata, and the references-by-iid. Hierarchy cannot live
//     in a flat folder, so the manifest owns it.
//   • The FILES are authoritative for CONTENT — each node's prose is read back
//     from content/<iid>.md by iid; the manifest stores a per-part hash so a
//     file edited outside Folio is detected as DRIFT (the file still wins; the
//     drift is reported, not silently dropped).
//   • A content file with no manifest entry is an ORPHAN → surfaced in a
//     "recovered / unlinked" bucket (never silently lost).
//   • A manifest entry whose content file is missing → a REPAIR entry (a clear
//     prompt, never a crash; the node loads with empty content).
//
// CLEAN BREAK (s19): there are no v4 projects in the wild yet, so the manifest
// carries NO v4-compat alias. migrate_v4() exists as a one-way upgrade for any
// stray legacy file, not as an ongoing compatibility contract.
//
// BUNDLE LAYOUT (DESIGN_s19 §3c):
//     MyNovel.folio/
//       project.json          ← THE SPINE: version, project metadata, the six
//                               trees as STRUCTURE-ONLY nodes (content/snapshot
//                               history stripped out and replaced by refs+hash)
//       content/<iid>.md      ← each node's prose, one file        [IMPLEMENTED]
//       snapshots/<iid>.json  ← each node's version history        [IMPLEMENTED]
//       meta/<iid>.json       ← per-part sidecar (notes/annotations/view-state)
//                               [SHAPE DEFINED — stays inline this slice; the
//                                next fill-in splits it out, no model change]
//       assets/<iid>.<ext>    ← full-res images / research binaries [IMPLEMENTED]
//       thumbs/<iid>.<ext>    ← bounded gallery-wall thumbnails       [IMPLEMENTED]
//       fonts/                ← embedded faces so the book travels  [PLANNED]
//
// What ships in this slice: structure-in-manifest + content & snapshots out to
// files by iid + hash drift + the full (C) reconcile (orphan/missing/drift).
// meta/assets/fonts have their path helpers and schema defined here so the
// shape is settled; externalising them is granular follow-on that does not
// touch the source-of-truth model.
// ─────────────────────────────────────────────────────────────────────────────

#include <filesystem>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace Folio {

namespace fs = std::filesystem;
using json = nlohmann::json;

// Current on-disk bundle version. v4 was the single-file blob.
inline constexpr int kFolioBundleVersion = 5;

// ── Bundle subdirectory names (one place; reused for read, write, log) ───────
namespace bundle_dir {
inline constexpr const char* kContent   = "content";
inline constexpr const char* kSnapshots = "snapshots";
inline constexpr const char* kMeta      = "meta";
inline constexpr const char* kAssets    = "assets";
inline constexpr const char* kThumbs    = "thumbs";
inline constexpr const char* kFonts     = "fonts";
inline constexpr const char* kManifest  = "project.json";
}  // namespace bundle_dir

// ── Per-part path helpers (keyed by iid — the cross-layer token) ─────────────
// Composed from a bundle root + an iid so disk names match GTK names and log
// tokens. e.g. content_path("/x/My.folio", "scn_k3f9a2b7")
//             -> "/x/My.folio/content/scn_k3f9a2b7.md"
fs::path content_path  (const fs::path& root, const std::string& iid);
fs::path snapshot_path (const fs::path& root, const std::string& iid);
fs::path meta_path     (const fs::path& root, const std::string& iid);
fs::path asset_path    (const fs::path& root, const std::string& iid,
                        const std::string& ext);
fs::path thumb_path    (const fs::path& root, const std::string& iid,
                        const std::string& ext);
fs::path manifest_path (const fs::path& root);

// ── Drift hash ───────────────────────────────────────────────────────────────
// Non-cryptographic content fingerprint (FNV-1a 64, lowercase hex). Drift
// detection only needs "did the bytes change," per §3d — not tamper-proofing.
std::string content_hash(const std::string& bytes);

// ── Reconcile report — the (C)-hybrid outcome of loading a bundle ────────────
// Returned by implode() so the caller (and, later, a recovery UI) can surface
// what reconciliation found. An empty report == a perfectly clean load.
struct ReconcileReport {
    struct Missing  { std::string iid; std::string file; };   // manifest ref, no file
    struct Orphan   { std::string iid; std::string file; };   // file, no manifest ref
    struct Drift    { std::string iid; std::string file; };   // hash mismatch (file won)

    std::vector<Missing> missing;     // → repair prompt; node loaded with "" content
    std::vector<Orphan>  orphans;     // → recovered / unlinked bucket
    std::vector<Drift>   drifted;     // → informational; on-disk content was used

    bool clean() const {
        return missing.empty() && orphans.empty() && drifted.empty();
    }
    // One-line summary for the log / status bar.
    std::string summary() const;
};

// ── Format detection ─────────────────────────────────────────────────────────
enum class ProjectFormat {
    BundleV5,     // a directory containing a project.json with version >= 5
    LegacyFile,   // a single-file JSON project (v4 or earlier)
    Unknown,      // path missing / unrecognised
};
ProjectFormat detect_format(const fs::path& path);

// ── The seam ─────────────────────────────────────────────────────────────────

// EXPLODE: write `blob` (a full in-memory project JSON, every node carrying an
// "iid") out as a v5 bundle rooted at `root`. Each node's "content" moves to
// content/<iid>.md and "snapshots" to snapshots/<iid>.json; the manifest keeps
// structure + a content_ref/snapshots_ref (file + hash) in their place.
// Writes content/snapshot files first, then project.json LAST, each via
// tmp+rename, so an interrupted save never leaves the manifest pointing at a
// half-written file (worst case: recoverable orphans). Throws on I/O failure.
void explode(const json& blob, const fs::path& root);

// IMPLODE: read the v5 bundle at `root` and rebuild the full in-memory project
// JSON (content re-inlined) that DocumentModel::from_json already understands.
// Applies the (C)-hybrid rule and fills `report`. Throws only on a structurally
// unreadable manifest; missing/with-drift/orphan content are reported, not
// thrown (load must not crash on a recoverable bundle).
json implode(const fs::path& root, ReconcileReport& report);

// MIGRATE v4 → v5 (in place, in memory): given a legacy blob (single-file v4),
// mint an iid for every node/snapshot that lacks one and stamp folio_version.
// The returned blob is ready to hand to explode(). One-way; no compat alias.
json migrate_v4(json blob);

// ── iid plumbing on the blob (pure; used by migrate + tests) ─────────────────
// Ensure every node in the blob's six trees has a non-empty "iid" (minting by
// the node's "kind"); returns how many were minted. Idempotent.
int ensure_node_iids(json& blob);

}  // namespace Folio
