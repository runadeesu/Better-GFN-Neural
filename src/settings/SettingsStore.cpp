#include "settings/SettingsStore.h"

#include <fstream>
#include <sstream>

#include "core/Log.h"

namespace bgn {

bool readTextFile(const std::filesystem::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return !f.bad();
}

bool writeTextFileAtomic(const std::filesystem::path& p, const std::string& text) {
    std::error_code ec;
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    std::filesystem::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << text;
        f.flush();
        if (!f) return false;
    }
    std::filesystem::rename(tmp, p, ec); // replaces existing atomically (MoveFileEx on Windows)
    if (ec) {
        std::filesystem::remove(p, ec);
        std::filesystem::rename(tmp, p, ec);
    }
    return !ec;
}

SettingsStore::SettingsStore(std::filesystem::path file) : file_(std::move(file)) {}

std::filesystem::path SettingsStore::backupPath() const {
    std::filesystem::path b = file_;
    b += ".bak";
    return b;
}

SettingsLoadResult SettingsStore::load(Settings& out) const {
    SettingsLoadResult r;
    std::string text, err;
    std::error_code ec;
    if (std::filesystem::exists(file_, ec)) {
        if (readTextFile(file_, text) && settingsFromJson(text, out, &err)) {
            r.source = SettingsLoadSource::Primary;
            return r;
        }
        r.primaryCorrupt = true;
        BGN_LOG_WARN("Settings", "settings file is corrupt ({}), trying backup", err);
        // Keep the corrupt file for diagnostics
        std::filesystem::path bad = file_;
        bad += ".corrupt";
        std::filesystem::copy_file(file_, bad, std::filesystem::copy_options::overwrite_existing, ec);
    }
    if (std::filesystem::exists(backupPath(), ec)) {
        if (readTextFile(backupPath(), text) && settingsFromJson(text, out, &err)) {
            r.source = SettingsLoadSource::Backup;
            r.message = "Settings were restored from the last good backup.";
            BGN_LOG_WARN("Settings", "restored settings from backup");
            return r;
        }
    }
    out = Settings{};
    r.source = SettingsLoadSource::Defaults;
    if (r.primaryCorrupt) r.message = "Settings could not be read and were reset to defaults.";
    return r;
}

bool SettingsStore::save(const Settings& s) const {
    std::error_code ec;
    // Rotate the current (known-good, it was parsed or written by us) file to .bak
    if (std::filesystem::exists(file_, ec)) {
        std::string current, err;
        Settings probe;
        if (readTextFile(file_, current) && settingsFromJson(current, probe, &err))
            std::filesystem::copy_file(file_, backupPath(), std::filesystem::copy_options::overwrite_existing, ec);
    }
    bool ok = writeTextFileAtomic(file_, settingsToJson(s));
    if (!ok) BGN_LOG_ERROR("Settings", "failed to write settings file");
    return ok;
}

SessionGuard::SessionGuard(std::filesystem::path dataDir)
    : lock_(dataDir / "session.lock"), counter_(dataDir / "recovery.txt") {}

int SessionGuard::begin() {
    std::error_code ec;
    int previous = 0;
    std::string text;
    if (readTextFile(counter_, text)) {
        try {
            previous = std::stoi(text);
        } catch (...) {
            previous = 0;
        }
    }
    if (std::filesystem::exists(lock_, ec)) {
        crashes_ = previous + 1;
    } else {
        crashes_ = 0;
    }
    writeTextFileAtomic(counter_, std::to_string(crashes_));
    writeTextFileAtomic(lock_, "running");
    return crashes_;
}

void SessionGuard::endClean() {
    std::error_code ec;
    std::filesystem::remove(lock_, ec);
    writeTextFileAtomic(counter_, "0");
}

void SessionGuard::resetCrashCount() {
    crashes_ = 0;
    writeTextFileAtomic(counter_, "0");
}

} // namespace bgn
