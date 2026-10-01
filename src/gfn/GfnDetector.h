#pragma once
// GeForce NOW detection (Win32 side): enumerates top-level windows of candidate
// processes and classifies them (gfn/GfnClassifier). Read-only: GeForce NOW is
// never injected into, hooked or modified.

#include <string>
#include <unordered_map>
#include <vector>

#include "gfn/GfnClassifier.h"
#include "platform/Win32.h"

namespace bgn {

struct GfnStatus {
    GfnState state = GfnState::NotRunning;
    HWND streamWindow = nullptr;
    DWORD pid = 0;
    std::string gameName;
    bool fromBrowser = false;
    int windowW = 0, windowH = 0;
    bool foreground = false;
};

class GfnDetector {
public:
    void setRules(const DetectionRules& rules) { rules_ = rules; }
    const DetectionRules& rules() const { return rules_; }
    GfnStatus poll();
    // Diagnostic list of candidate windows from the last poll (no titles of unrelated apps)
    const std::vector<WindowSnapshot>& lastSnapshots() const { return snapshots_; }

private:
    std::string processName(DWORD pid);
    DetectionRules rules_;
    std::vector<WindowSnapshot> snapshots_;
    struct CacheEntry {
        std::string name;
        double time;
    };
    std::unordered_map<DWORD, CacheEntry> nameCache_;
};

// Locates the GeForce NOW executable (override, default install paths, Start menu shortcut).
std::wstring findGeForceNowExecutable(const std::string& overridePath);
// Launches GeForce NOW. Returns false (with a reason) if it could not be found/started.
bool launchGeForceNow(const std::string& overridePath, std::string& error);

} // namespace bgn
