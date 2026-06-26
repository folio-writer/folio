// ─────────────────────────────────────────────────────────────────────────────
// ProjectBundle.cpp — v5 .folio bundle seam (s19). Pure: <nlohmann/json>,
// <filesystem>, Iid only. No gtkmm/glibmm — g++-verifiable in the sandbox.
// See ProjectBundle.hpp for the model and the §3d (C)-hybrid source-of-truth
// rule this implements.
// ─────────────────────────────────────────────────────────────────────────────
#include "ProjectBundle.hpp"
#include "Iid.hpp"

#include <cstdint>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace Folio {

// The six binder trees, in a fixed order (reused for explode/implode/migrate).
static const char* kTreeKeys[] = {
    "manuscript", "characters", "places", "references", "templates", "trash"
};

// ── Path helpers ─────────────────────────────────────────────────────────────
fs::path content_path(const fs::path& root, const std::string& iid) {
    return root / bundle_dir::kContent / (iid + ".md");
}
fs::path snapshot_path(const fs::path& root, const std::string& iid) {
    return root / bundle_dir::kSnapshots / (iid + ".json");
}
fs::path meta_path(const fs::path& root, const std::string& iid) {
    return root / bundle_dir::kMeta / (iid + ".json");
}
fs::path asset_path(const fs::path& root, const std::string& iid,
                    const std::string& ext) {
    std::string name = iid + (ext.empty() ? "" : (ext[0] == '.' ? ext : "." + ext));
    return root / bundle_dir::kAssets / name;
}
fs::path thumb_path(const fs::path& root, const std::string& iid,
                    const std::string& ext) {
    std::string name = iid + (ext.empty() ? "" : (ext[0] == '.' ? ext : "." + ext));
    return root / bundle_dir::kThumbs / name;
}
fs::path manifest_path(const fs::path& root) {
    return root / bundle_dir::kManifest;
}

// ── Drift hash: FNV-1a 64-bit, lowercase hex ─────────────────────────────────
std::string content_hash(const std::string& bytes) {
    std::uint64_t h = 1469598103934665603ULL;          // FNV offset basis
    for (unsigned char c : bytes) {
        h ^= c;
        h *= 1099511628211ULL;                          // FNV prime
    }
    static const char* hex = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) { out[i] = hex[h & 0xF]; h >>= 4; }
    return out;
}

// ── ReconcileReport::summary ─────────────────────────────────────────────────
std::string ReconcileReport::summary() const {
    if (clean()) return "clean";
    std::ostringstream os;
    bool first = true;
    auto part = [&](const char* label, std::size_t n) {
        if (n == 0) return;
        if (!first) os << ", ";
        os << n << ' ' << label;
        first = false;
    };
    part("missing",  missing.size());
    part("orphaned", orphans.size());
    part("drifted",  drifted.size());
    return os.str();
}

// ── kind string (from the blob) → IidKind ────────────────────────────────────
static IidKind kind_from_blob(const json& node) {
    std::string k = node.value("kind", "scene");
    if (k == "group")     return IidKind::Group;
    if (k == "character") return IidKind::Character;
    if (k == "place")     return IidKind::Place;
    if (k == "reference") return IidKind::Reference;
    if (k == "template")  return IidKind::Template;
    return IidKind::Scene;
}

// ── ensure_node_iids ─────────────────────────────────────────────────────────
static int ensure_iids_recursive(json& node) {
    int minted = 0;
    std::string cur = node.value("iid", "");
    if (cur.empty() || !is_iid(cur)) {
        node["iid"] = make_iid(kind_from_blob(node));
        ++minted;
    }
    if (node.contains("children") && node["children"].is_array())
        for (auto& c : node["children"]) minted += ensure_iids_recursive(c);
    return minted;
}

int ensure_node_iids(json& blob) {
    int minted = 0;
    for (const char* key : kTreeKeys)
        if (blob.contains(key) && blob[key].is_array())
            for (auto& n : blob[key]) minted += ensure_iids_recursive(n);
    return minted;
}

// ── detect_format ────────────────────────────────────────────────────────────
ProjectFormat detect_format(const fs::path& path) {
    std::error_code ec;
    if (!fs::exists(path, ec)) return ProjectFormat::Unknown;
    if (fs::is_directory(path, ec)) {
        fs::path man = manifest_path(path);
        if (!fs::exists(man, ec)) return ProjectFormat::Unknown;
        std::ifstream f(man);
        if (!f) return ProjectFormat::Unknown;
        try {
            json j = json::parse(f);
            if (j.value("folio_version", 0) >= kFolioBundleVersion)
                return ProjectFormat::BundleV5;
        } catch (...) { /* fall through */ }
        return ProjectFormat::Unknown;
    }
    // A single regular file is a legacy (v4) single-JSON project.
    return ProjectFormat::LegacyFile;
}

// ── small atomic-write helper (tmp sibling + rename) ─────────────────────────
static void write_file_atomic(const fs::path& target, const std::string& data) {
    fs::path tmp = target;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) throw std::runtime_error("ProjectBundle: cannot write " + tmp.string());
        f << data;
        if (!f) throw std::runtime_error("ProjectBundle: write error on " + tmp.string());
    }
    std::error_code ec;
    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(tmp, ec);
        throw std::runtime_error("ProjectBundle: cannot finalise " + target.string());
    }
}

static std::string read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Copy a binary subdirectory (assets/ or thumbs/) forward into the freshly
// built tmp bundle. These hold bytes that are NOT reconstructed from the blob
// (unlike content/snapshots), so explode MUST carry the live bundle's copies
// across or the swap would delete every imported image. No-op if `src` is
// absent (a project with no images, or a first save). Overwrites existing.
static void copy_tree_if_exists(const fs::path& src, const fs::path& dst) {
    std::error_code ec;
    if (!fs::is_directory(src, ec)) return;
    fs::copy(src, dst,
             fs::copy_options::recursive | fs::copy_options::overwrite_existing,
             ec);
    if (ec)
        throw std::runtime_error("ProjectBundle::explode: cannot carry forward "
                                 + src.string());
}

// ─────────────────────────────────────────────────────────────────────────────
// EXPLODE — blob → bundle
//
// Strip each node's "content" → content/<iid>.md (+ content_ref{file,hash,bytes})
// and "snapshots" → snapshots/<iid>.json (+ snapshots_ref{file,hash,count}).
// We build the whole bundle under <root>.folio-tmp/ then swap it into place, so
// the live bundle is never half-written.
// ─────────────────────────────────────────────────────────────────────────────
static json strip_node_for_manifest(json node, const fs::path& tmp_root) {
    const std::string iid = node.value("iid", "");
    if (iid.empty())
        throw std::runtime_error("ProjectBundle::explode: node missing iid "
                                 "(call ensure_node_iids/migrate_v4 first)");

    // content → content/<iid>.md
    if (node.contains("content")) {
        std::string content = node.value("content", "");
        node.erase("content");
        if (!content.empty()) {
            write_file_atomic(content_path(tmp_root, iid), content);
            node["content_ref"] = {
                {"file",  std::string(bundle_dir::kContent) + "/" + iid + ".md"},
                {"hash",  content_hash(content)},
                {"bytes", content.size()},
            };
        }
    }

    // snapshots → snapshots/<iid>.json
    if (node.contains("snapshots") && node["snapshots"].is_array()
            && !node["snapshots"].empty()) {
        std::string dump = node["snapshots"].dump(2);
        write_file_atomic(snapshot_path(tmp_root, iid), dump);
        node["snapshots_ref"] = {
            {"file",  std::string(bundle_dir::kSnapshots) + "/" + iid + ".json"},
            {"hash",  content_hash(dump)},
            {"count", node["snapshots"].size()},
        };
        node.erase("snapshots");
    } else {
        node.erase("snapshots");
    }

    // recurse children (structure stays in the manifest)
    if (node.contains("children") && node["children"].is_array()) {
        json kids = json::array();
        for (auto& c : node["children"])
            kids.push_back(strip_node_for_manifest(c, tmp_root));
        node["children"] = std::move(kids);
    }
    return node;
}

void explode(const json& blob, const fs::path& root) {
    std::error_code ec;
    fs::path tmp_root = root;
    tmp_root += ".folio-tmp";
    fs::remove_all(tmp_root, ec);
    fs::create_directories(tmp_root / bundle_dir::kContent, ec);
    fs::create_directories(tmp_root / bundle_dir::kSnapshots, ec);
    fs::create_directories(tmp_root / bundle_dir::kAssets, ec);
    fs::create_directories(tmp_root / bundle_dir::kThumbs, ec);
    if (ec) throw std::runtime_error("ProjectBundle::explode: cannot create bundle dirs");

    // Build the manifest: copy the blob, replace each tree's nodes with their
    // stripped (structure-only) form. Side files are written during the walk.
    json manifest = blob;
    manifest["folio_version"] = kFolioBundleVersion;
    for (const char* key : kTreeKeys) {
        if (!manifest.contains(key) || !manifest[key].is_array()) continue;
        json tree = json::array();
        for (auto& n : manifest[key])
            tree.push_back(strip_node_for_manifest(n, tmp_root));
        manifest[key] = std::move(tree);
    }

    // project.json LAST.
    write_file_atomic(manifest_path(tmp_root), manifest.dump(2));

    // Carry binary dirs forward (assets/ + thumbs/) — their bytes are not in the
    // blob, so they would be lost in the swap otherwise. `root` still exists here.
    // NOTE: a save-AS to a different root will not see the source's binaries here
    // (they sit at the old root); copying those is the save-orchestration slice's
    // job, flagged as a known follow-on.
    copy_tree_if_exists(root / bundle_dir::kAssets, tmp_root / bundle_dir::kAssets);
    copy_tree_if_exists(root / bundle_dir::kThumbs, tmp_root / bundle_dir::kThumbs);

    // Swap tmp_root into place atomically: stash any existing bundle, move tmp
    // in, then drop the stash.
    fs::path bak = root;
    bak += ".folio-bak";
    fs::remove_all(bak, ec);
    bool had_old = fs::exists(root, ec);
    if (had_old) {
        fs::rename(root, bak, ec);
        if (ec) throw std::runtime_error("ProjectBundle::explode: cannot stash old bundle");
    }
    fs::rename(tmp_root, root, ec);
    if (ec) {
        // roll back
        if (had_old) fs::rename(bak, root, ec);
        throw std::runtime_error("ProjectBundle::explode: cannot place new bundle");
    }
    fs::remove_all(bak, ec);
}

// ─────────────────────────────────────────────────────────────────────────────
// IMPLODE — bundle → blob, applying the (C)-hybrid rule
// ─────────────────────────────────────────────────────────────────────────────
static json reinline_node(json node, const fs::path& root,
                          ReconcileReport& rep, std::set<std::string>& referenced) {
    const std::string iid = node.value("iid", "");
    if (!iid.empty()) referenced.insert(iid);

    // content_ref → content
    if (node.contains("content_ref")) {
        const auto& cr  = node["content_ref"];
        std::string rel = cr.value("file", "");
        fs::path    file = root / rel;
        std::error_code ec;
        if (!fs::exists(file, ec)) {
            rep.missing.push_back({iid, rel});
            node["content"] = "";
        } else {
            std::string bytes = read_file(file);
            std::string want  = cr.value("hash", "");
            if (!want.empty() && content_hash(bytes) != want)
                rep.drifted.push_back({iid, rel});     // file wins; flag it
            node["content"] = bytes;
        }
        node.erase("content_ref");
    } else if (!node.contains("content")) {
        node["content"] = "";
    }

    // snapshots_ref → snapshots
    if (node.contains("snapshots_ref")) {
        const auto& sr  = node["snapshots_ref"];
        std::string rel = sr.value("file", "");
        fs::path    file = root / rel;
        std::error_code ec;
        if (!fs::exists(file, ec)) {
            rep.missing.push_back({iid, rel});
            node["snapshots"] = json::array();
        } else {
            std::string bytes = read_file(file);
            std::string want  = sr.value("hash", "");
            if (!want.empty() && content_hash(bytes) != want)
                rep.drifted.push_back({iid, rel});
            try { node["snapshots"] = json::parse(bytes); }
            catch (...) { node["snapshots"] = json::array(); rep.missing.push_back({iid, rel}); }
        }
        node.erase("snapshots_ref");
    } else if (!node.contains("snapshots")) {
        node["snapshots"] = json::array();
    }

    if (node.contains("children") && node["children"].is_array()) {
        json kids = json::array();
        for (auto& c : node["children"])
            kids.push_back(reinline_node(c, root, rep, referenced));
        node["children"] = std::move(kids);
    }
    return node;
}

// Scan a content-bearing dir for files whose iid is not referenced anywhere in
// the manifest → orphans (recovered, not lost).
static void scan_orphans(const fs::path& dir, const std::string& ext,
                         const std::set<std::string>& referenced,
                         ReconcileReport& rep) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        fs::path p = entry.path();
        if (p.extension() != ext) continue;
        std::string iid = p.stem().string();
        if (referenced.find(iid) == referenced.end())
            rep.orphans.push_back({iid, p.filename().string()});
    }
}

// Ext-agnostic orphan scan for the binary dirs (assets/, thumbs/): an image is
// stored as .jpg OR .png, so orphan-ness keys on the stem (iid) regardless of
// extension. Used for both dirs.
static void scan_binary_orphans(const fs::path& dir,
                                const std::set<std::string>& referenced,
                                ReconcileReport& rep) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        fs::path p = entry.path();
        if (p.filename().string().size() && p.filename().string()[0] == '.')
            continue;  // skip dotfiles / stray .tmp
        std::string iid = p.stem().string();
        if (referenced.find(iid) == referenced.end())
            rep.orphans.push_back({iid, std::string(dir.filename().string()) +
                                            "/" + p.filename().string()});
    }
}

// §9 asset class: validate the manifest's `images` pool against the files on
// disk. Each image-fragment {iid, ext, hash} must have its assets/<iid>.<ext>;
// absent → missing, hash mismatch → drift (the file wins, as with content).
// Fragment iids are added to `referenced` so legitimate assets are not then
// flagged as orphans. Thumbnails are a regenerable cache, so a missing thumb is
// NOT an error here (the surface re-derives it); only orphan thumbs are swept.
static void reconcile_assets(const fs::path& root, const json& blob,
                             std::set<std::string>& referenced,
                             ReconcileReport& rep) {
    if (!blob.contains("images") || !blob["images"].is_array())
        return;
    for (const auto& frag : blob["images"]) {
        std::string iid = frag.value("iid", "");
        if (iid.empty()) continue;
        referenced.insert(iid);
        std::string ext = frag.value("ext", "");
        std::string rel = std::string(bundle_dir::kAssets) + "/" + iid +
                          (ext.empty() ? "" : ("." + ext));
        fs::path file = asset_path(root, iid, ext);
        std::error_code ec;
        if (!fs::exists(file, ec)) {
            rep.missing.push_back({iid, rel});       // the image itself is gone
            continue;
        }
        std::string want = frag.value("hash", "");
        if (!want.empty() && content_hash(read_file(file)) != want)
            rep.drifted.push_back({iid, rel});       // file wins; flag it
    }
}

json implode(const fs::path& root, ReconcileReport& report) {
    fs::path man = manifest_path(root);
    std::ifstream f(man);
    if (!f) throw std::runtime_error("ProjectBundle::implode: cannot open " + man.string());
    json blob = json::parse(f);   // structural failure here is fatal (throws)

    std::set<std::string> referenced;
    for (const char* key : kTreeKeys) {
        if (!blob.contains(key) || !blob[key].is_array()) continue;
        json tree = json::array();
        for (auto& n : blob[key])
            tree.push_back(reinline_node(n, root, report, referenced));
        blob[key] = std::move(tree);
    }

    scan_orphans(root / bundle_dir::kContent,   ".md",   referenced, report);
    scan_orphans(root / bundle_dir::kSnapshots, ".json", referenced, report);

    // §9 asset class: validate the image pool, then sweep orphan binaries. The
    // asset check must run BEFORE the orphan sweep so fragment iids are in
    // `referenced` and their files are not mistaken for orphans.
    reconcile_assets(root, blob, referenced, report);
    scan_binary_orphans(root / bundle_dir::kAssets, referenced, report);
    scan_binary_orphans(root / bundle_dir::kThumbs, referenced, report);

    return blob;
}

// ── migrate_v4 ───────────────────────────────────────────────────────────────
json migrate_v4(json blob) {
    ensure_node_iids(blob);
    blob["folio_version"] = kFolioBundleVersion;
    return blob;
}

}  // namespace Folio
