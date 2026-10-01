#pragma once
// Crash-safe settings persistence: atomic writes, automatic backup and
// fall-back to the last good copy when the primary file is corrupt.

#include <filesystem>
#include <string>

#include "settings/Settings.h"

namespace bgn {

enum class SettingsLoadSource { Primary, Backup, Defaults };

struct SettingsLoadResult {
    SettingsLoadSource source = SettingsLoadSource::Defaults;
    bool primaryCorrupt = false;
    std::string message;
};

class SettingsStore {
public:
    explicit SettingsStore(std::filesystem::path file);

    SettingsLoadResult load(Settings& out) const;
    bool save(const Settings& s) const;

    const std::filesystem::path& path() const { return file_; }
    std::filesystem::path backupPath() const;

private:
    std::filesystem::path file_;
};

// Tracks clean vs. unclean shutdowns. A lock file exists while the app runs;
// if it is still present at start-up, the previous run ended abnormally.
class SessionGuard {
public:
    explicit SessionGuard(std::filesystem::path dataDir);
    // Returns the number of consecutive abnormal terminations (0 = last run was clean).
    int begin();
    void endClean();
    // Record that the user acknowledged/reset safe mode.
    void resetCrashCount();
    int consecutiveCrashes() const { return crashes_; }

private:
    std::filesystem::path lock_, counter_;
    int crashes_ = 0;
};

bool readTextFile(const std::filesystem::path& p, std::string& out);
bool writeTextFileAtomic(const std::filesystem::path& p, const std::string& text);

} // namespace bgn
