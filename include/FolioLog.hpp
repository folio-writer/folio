// ─────────────────────────────────────────────────────────────────────────────
// FolioLog.hpp — spdlog wrapper for Folio
//
// Initialise once at startup with FolioLog::init().
// Use the LOG_* macros anywhere in the codebase.
//
// Log file:  ~/.local/share/folio/folio.log
// Rotation:  5 MB max, 3 files kept
// Pattern:   [YYYY-MM-DD HH:MM:SS.mmm] [LEVEL] [file:line] message
//
// Crash handler: call FolioLog::install_crash_handler() from main().
// On SIGSEGV / SIGABRT / SIGFPE it flushes the log and re-raises.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <csignal>
#include <cstdlib>
#include <string>
#include <memory>
#include <sys/stat.h>

namespace FolioLog {

inline void init(bool debug_to_console = false) {
    // Use POSIX — GLib is not yet initialised when main() calls this.
    const char* home = std::getenv("HOME");
    if (!home) home = "/tmp";

    std::string log_dir  = std::string(home) + "/.local/share/folio";
    std::string log_path = log_dir + "/folio.log";

    // Create directories if needed
    ::mkdir((std::string(home) + "/.local").c_str(),        0755);
    ::mkdir((std::string(home) + "/.local/share").c_str(),  0755);
    ::mkdir(log_dir.c_str(),                                0755);

    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        log_path, 5 * 1024 * 1024, 3);
    file_sink->set_level(spdlog::level::debug);

    std::vector<spdlog::sink_ptr> sinks { file_sink };

    if (debug_to_console) {
        auto console = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
        console->set_level(spdlog::level::debug);
        sinks.push_back(console);
    }

    auto logger = std::make_shared<spdlog::logger>(
        "folio", sinks.begin(), sinks.end());
    logger->set_level(spdlog::level::debug);
    // Flush on every message so nothing is lost on a crash
    logger->flush_on(spdlog::level::debug);

    spdlog::set_default_logger(logger);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%s:%#] %v");

    SPDLOG_INFO("── Folio started ──────────────────");
    SPDLOG_INFO("Log: {}", log_path);
    spdlog::default_logger()->flush();
}

inline void install_crash_handler() {
    auto handler = [](int sig) {
        const char* name = (sig == SIGSEGV) ? "SIGSEGV" :
                           (sig == SIGABRT) ? "SIGABRT" :
                           (sig == SIGFPE)  ? "SIGFPE"  : "SIGNAL";
        spdlog::default_logger_raw()->critical("CRASH: {} received", name);
        spdlog::default_logger_raw()->flush();
        std::signal(sig, SIG_DFL);
        std::raise(sig);
    };
    std::signal(SIGSEGV, handler);
    std::signal(SIGABRT, handler);
    std::signal(SIGFPE,  handler);
}

inline void shutdown() {
    SPDLOG_INFO("── Folio shutdown ─────────────────");
    spdlog::shutdown();
}

} // namespace FolioLog

// Convenience macros — include source file + line automatically
#define LOG_DEBUG(...)    SPDLOG_DEBUG(__VA_ARGS__)
#define LOG_INFO(...)     SPDLOG_INFO(__VA_ARGS__)
#define LOG_WARN(...)     SPDLOG_WARN(__VA_ARGS__)
#define LOG_ERROR(...)    SPDLOG_ERROR(__VA_ARGS__)
#define LOG_CRITICAL(...) SPDLOG_CRITICAL(__VA_ARGS__)
