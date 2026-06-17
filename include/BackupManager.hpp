#pragma once
// BackupManager — gzip-compressed, rotating project backups
// ─────────────────────────────────────────────────────────────────────────────
// Backup files are written to:
//   <backup_dir>/<project-stem>/<stem>_YYYY-MM-DDTHH-MM.folio.gz
//
// Two trigger points (both called from MainWindow):
//   tick(elapsed_secs)  — called every second from the autosave timer tick;
//                         fires a timed backup when the interval elapses
//   on_close()          — always attempts a backup on app close (subject to
//                         minimum-gap guard)
// ─────────────────────────────────────────────────────────────────────────────
#include "FolioPrefs.hpp"
#include "DocumentModel.hpp"
#include <string>
#include <vector>
#include <ctime>

namespace Folio {

class BackupManager {
public:
    // min gap (seconds) between the last backup and a close-triggered backup
    static constexpr int CLOSE_MIN_GAP_SECS = 300; // 5 minutes

    BackupManager(const FolioPrefs& prefs, const DocumentModel& model);

    // Called every second from the autosave 1-second tick.
    // Fires a backup when the configured interval has elapsed.
    void tick(int elapsed_secs);

    // Called from the close handler after the final save has succeeded.
    void on_close();

    // Reset the elapsed counter — call after a timed backup fires so the
    // next interval starts fresh (e.g. when prefs change mid-session).
    void reset_timer();

    // Resolve the effective backup directory (XDG default if pref is empty).
    static std::string resolve_backup_dir(const std::string& pref_dir);

    // Derive a filesystem-safe stem from a project file path.
    static std::string project_stem(const std::string& project_path);

    // List existing backup files for a project, oldest first.
    static std::vector<std::string> list_backups(const std::string& dir);

private:
    const FolioPrefs&    m_prefs;
    const DocumentModel& m_model;
    int                  m_secs_since_last = 0; // counts up each tick
    std::time_t          m_last_backup_time = 0;

    // Write a single compressed backup; prune old files; return true on success.
    bool write_backup();

    // s19: a v5 project is a bundle DIRECTORY, not a single file. Back it up as
    // a dated recursive copy <dest_dir>/<stem>_<ts>.folio/ and prune old copies.
    // (Compressed .zip bundle backups would need an archive dep — follow-on.)
    static bool backup_bundle_copy(const std::string& src_bundle,
                                   const std::string& dest_dir,
                                   const std::string& stem,
                                   const std::string& ts,
                                   int max_count);

    // Compress src_data into dest_path using gzip format.
    static bool gzip_write(const std::string& dest_path,
                           const std::string& src_data);

    // Delete oldest files so at most max_count remain.
    static void prune(const std::string& dir, int max_count);
};

} // namespace Folio
