#pragma once
// Application directories. Portable mode keeps settings and logs next to the
// executable; the installed version uses %LOCALAPPDATA%\BetterGFNNeural.

#include <filesystem>

namespace bgn {

struct CommandLine;

struct AppPaths {
    std::filesystem::path exePath;
    std::filesystem::path exeDir;
    std::filesystem::path dataDir;
    std::filesystem::path logsDir;
    std::filesystem::path settingsFile;
    bool portable = false;
};

AppPaths resolveAppPaths(const CommandLine& cmd);

// Registers the user name / profile path with the logger so they never end up in logs.
void registerPrivacyRedactions();

} // namespace bgn
