#include "Application.hpp"
#include "FolioLog.hpp"

int main(int argc, char* argv[]) {
    // Initialise logging first — crash handler writes to the log file.
    // Pass true to also mirror DEBUG output to stderr during development.
    bool debug_console = false;
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--verbose" || std::string(argv[i]) == "-v")
            debug_console = true;

    FolioLog::init(debug_console);
    FolioLog::install_crash_handler();

    LOG_INFO("Starting Folio application");
    auto app = Folio::Application::create();
    int result = app->run(argc, argv);
    FolioLog::shutdown();
    return result;
}
