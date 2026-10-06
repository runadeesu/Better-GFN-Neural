#pragma once
// Command line options. Portable.

#include <string>
#include <vector>

namespace bgn {

struct CommandLine {
    bool background = false;      // start hidden in the tray (used by "Start with Windows")
    bool selfTest = false;        // headless GPU pipeline self-test, exits with status
    bool benchmark = false;       // headless benchmark, prints JSON, exits
    bool portable = false;        // force portable data directory (next to the exe)
    bool resetSettings = false;
    bool simulateCrash = false;   // test hook for crash recovery
    bool forceWarp = false;       // use the WARP software rasterizer (tests / diagnostics)
    bool noUi = false;            // run detection/engine without the main window (automation)
    std::string automation;       // automation scenario name
    std::string dataDir;          // explicit data directory
    std::string logLevel;         // override log level
    std::string outputJson;       // where self-test/benchmark/automation write results
    double automationSeconds = 0; // optional duration for automation runs
    double screenshotAt = 0;      // automation: take a screenshot after this many seconds (0 = never)
    std::vector<std::string> unknown;
};

CommandLine parseCommandLine(const std::vector<std::string>& args);

} // namespace bgn
