#include "BackupManager.hpp"
#include "FolioLog.hpp"
#include <algorithm>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <zlib.h>
#include <glib.h>

namespace fs = std::filesystem;

namespace Folio {

// ─────────────────────────────────────────────────────────────────────────────

BackupManager::BackupManager(const FolioPrefs& prefs, const DocumentModel& model)
    : m_prefs(prefs), m_model(model)
{
    // Ensure the default backup directory exists at startup so the user can
    // find it immediately even before the first backup is written.
    resolve_backup_dir(m_prefs.backup_dir);
}

// ── Public interface ──────────────────────────────────────────────────────────

void BackupManager::tick(int /*elapsed_secs*/) {
    if (!m_prefs.backup_enabled)             return;
    if (m_prefs.backup_interval_hours <= 0)  return;
    if (m_model.current_path.empty())        return;

    ++m_secs_since_last;

    const int interval = m_prefs.backup_interval_hours * 3600;
    if (m_secs_since_last >= interval) {
        m_secs_since_last = 0;
        LOG_INFO("BackupManager: timed backup firing (interval={}h)",
                 m_prefs.backup_interval_hours);
        write_backup();
    }
}

void BackupManager::on_close() {
    if (!m_prefs.backup_enabled)       return;
    if (m_model.current_path.empty())  return;

    // Skip if we made a backup very recently (e.g. timer fired just before close)
    std::time_t now = std::time(nullptr);
    if (m_last_backup_time > 0 &&
        (now - m_last_backup_time) < CLOSE_MIN_GAP_SECS) {
        LOG_INFO("BackupManager: on_close skipped — last backup was {}s ago",
                 (int)(now - m_last_backup_time));
        return;
    }

    LOG_INFO("BackupManager: close backup firing");
    write_backup();
}

void BackupManager::reset_timer() {
    m_secs_since_last = 0;
}

// ── Static helpers ────────────────────────────────────────────────────────────

std::string BackupManager::resolve_backup_dir(const std::string& pref_dir) {
    if (!pref_dir.empty())
        return pref_dir;
    // Default: ~/.local/share/folio/backups
    const char* xdg = g_get_user_data_dir();
    std::string dir = (fs::path(xdg) / "folio" / "backups").string();
    // Ensure the directory exists so the user can see it immediately
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

std::string BackupManager::project_stem(const std::string& project_path) {
    // Derive a clean filesystem name from the project file path.
    // e.g. "/home/user/novels/MyNovel.folio" → "MyNovel"
    std::string stem = fs::path(project_path).stem().string();
    // Sanitise: replace anything that isn't alphanumeric, dash, dot or
    // underscore with an underscore so the name is safe on any filesystem.
    for (char& c : stem) {
        if (!std::isalnum((unsigned char)c) && c != '-' && c != '.')
            c = '_';
    }
    if (stem.empty())
        stem = "folio_project";
    return stem;
}

std::vector<std::string> BackupManager::list_backups(const std::string& dir) {
    std::vector<std::string> files;
    std::error_code ec;
    if (!fs::is_directory(dir, ec))
        return files;
    for (auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec))
            continue;
        auto p = entry.path();
        // Match <stem>_YYYY-MM-DDTHH-MM.folio.gz
        std::string fn = p.filename().string();
        if (fn.size() > 8 && p.extension() == ".gz")
            files.push_back(p.string());
    }
    // Sort lexicographically — the timestamp in the filename makes this
    // equivalent to chronological order (oldest first).
    std::sort(files.begin(), files.end());
    return files;
}

// ── Private ───────────────────────────────────────────────────────────────────

bool BackupManager::write_backup() {
    try {
        // 1. Serialise the model to a JSON string (reuse save_to via a temp file
        //    approach — write to memory by capturing what save_to would write).
        //    save_to writes to a path; we redirect through a temp file then read
        //    it back so we can compress it without duplicating serialisation logic.
        const std::string& proj_path = m_model.current_path;
        std::string base_dir = resolve_backup_dir(m_prefs.backup_dir);
        std::string stem     = project_stem(proj_path);
        std::string proj_dir = (fs::path(base_dir) / stem).string();

        // Ensure directory exists
        std::error_code ec;
        fs::create_directories(proj_dir, ec);
        if (ec) {
            LOG_WARN("BackupManager: cannot create dir '{}': {}",
                     proj_dir, ec.message());
            return false;
        }

        // Build timestamp string: YYYY-MM-DDTHH-MM
        std::time_t now = std::time(nullptr);
        std::tm* tm_info = std::localtime(&now);
        char ts[32];
        std::strftime(ts, sizeof(ts), "%Y-%m-%dT%H-%M", tm_info);

        // s19: a v5 project is a bundle directory — back it up as a dated
        // recursive copy rather than gzipping a single file.
        std::error_code dec;
        if (fs::is_directory(proj_path, dec)) {
            bool ok = backup_bundle_copy(proj_path, proj_dir, stem, ts,
                                         m_prefs.backup_max_count);
            if (ok) { m_last_backup_time = now; m_secs_since_last = 0; }
            return ok;
        }

        std::string gz_path = (fs::path(proj_dir) /
                               (stem + "_" + ts + ".folio.gz")).string();

        // 2. Read the live project file (it was just saved by do_autosave /
        //    save_on_close before we were called, so it's current).
        std::ifstream in(proj_path, std::ios::binary);
        if (!in) {
            LOG_WARN("BackupManager: cannot open project file '{}'", proj_path);
            return false;
        }
        std::ostringstream oss;
        oss << in.rdbuf();
        std::string json_data = oss.str();

        // 3. Compress and write
        if (!gzip_write(gz_path, json_data)) {
            LOG_WARN("BackupManager: gzip_write failed for '{}'", gz_path);
            return false;
        }

        LOG_INFO("BackupManager: wrote '{}'  ({} bytes → gz)",
                 gz_path, json_data.size());

        // 4. Prune old backups
        prune(proj_dir, m_prefs.backup_max_count);

        m_last_backup_time = now;
        m_secs_since_last  = 0;
        return true;

    } catch (const std::exception& ex) {
        LOG_WARN("BackupManager: write_backup exception: {}", ex.what());
        return false;
    }
}

bool BackupManager::backup_bundle_copy(const std::string& src_bundle,
                                       const std::string& dest_dir,
                                       const std::string& stem,
                                       const std::string& ts,
                                       int max_count) {
    std::error_code ec;
    fs::path dest = fs::path(dest_dir) / (stem + "_" + ts + ".folio");

    // Recursive copy of the whole bundle (content/, snapshots/, project.json…).
    fs::remove_all(dest, ec);  // in case a same-minute copy exists
    fs::copy(src_bundle, dest,
             fs::copy_options::recursive | fs::copy_options::overwrite_existing,
             ec);
    if (ec) {
        LOG_WARN("BackupManager: bundle copy '{}' -> '{}' failed: {}",
                 src_bundle, dest.string(), ec.message());
        return false;
    }
    LOG_INFO("BackupManager: wrote bundle backup '{}'", dest.string());

    // Prune old dated bundle copies (directories named <stem>_*.folio),
    // keeping at most max_count, oldest first (timestamp sorts lexically).
    std::vector<std::string> copies;
    for (const auto& e : fs::directory_iterator(dest_dir, ec)) {
        if (!e.is_directory()) continue;
        std::string fn = e.path().filename().string();
        if (e.path().extension() == ".folio" &&
            fn.rfind(stem + "_", 0) == 0)
            copies.push_back(e.path().string());
    }
    std::sort(copies.begin(), copies.end());
    int excess = (int)copies.size() - max_count;
    for (int i = 0; i < excess; ++i) {
        std::error_code rec;
        fs::remove_all(copies[(size_t)i], rec);
        if (rec) LOG_WARN("BackupManager: could not prune '{}': {}",
                          copies[(size_t)i], rec.message());
        else     LOG_INFO("BackupManager: pruned bundle backup '{}'",
                          copies[(size_t)i]);
    }
    return true;
}

bool BackupManager::gzip_write(const std::string& dest_path,
                                const std::string& src_data) {
    gzFile gz = gzopen(dest_path.c_str(), "wb9"); // level 9 — JSON compresses very well
    if (!gz)
        return false;

    const char* ptr  = src_data.data();
    int         left = (int)src_data.size();
    while (left > 0) {
        int written = gzwrite(gz, ptr, (unsigned)left);
        if (written <= 0) {
            gzclose(gz);
            return false;
        }
        ptr  += written;
        left -= written;
    }
    return gzclose(gz) == Z_OK;
}

void BackupManager::prune(const std::string& dir, int max_count) {
    auto files = list_backups(dir);
    int  excess = (int)files.size() - max_count;
    if (excess <= 0)
        return;
    // files is sorted oldest-first; delete from the front
    for (int i = 0; i < excess; ++i) {
        std::error_code ec;
        fs::remove(files[(size_t)i], ec);
        if (ec)
            LOG_WARN("BackupManager: could not delete '{}': {}",
                     files[(size_t)i], ec.message());
        else
            LOG_INFO("BackupManager: pruned '{}'", files[(size_t)i]);
    }
}

} // namespace Folio
