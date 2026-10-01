#pragma once
// Pure decision logic for GeForce NOW detection. The Win32 side
// (GfnDetector) enumerates windows and feeds them here. Portable/testable.

#include <cstdint>
#include <string>
#include <vector>

namespace bgn {

enum class GfnState { NotRunning, Connected, Streaming };
const char* toString(GfnState s);

struct WindowSnapshot {
    uint64_t handle = 0;
    uint32_t pid = 0;
    std::string processName; // lower-case file name, e.g. "geforcenow.exe"
    std::string title;       // UTF-8
    std::string className;
    int x = 0, y = 0, width = 0, height = 0;          // window rect (physical px)
    int clientWidth = 0, clientHeight = 0;
    int monitorWidth = 0, monitorHeight = 0;          // monitor containing the window
    bool visible = false;
    bool minimized = false;
    bool cloaked = false;
    bool foreground = false;
};

struct DetectionRules {
    std::vector<std::string> processNames{"geforcenow.exe", "geforcenowstreamer.exe", "geforce now.exe", "nvidia geforce now.exe"};
    std::vector<std::string> browserProcessNames{"chrome.exe", "msedge.exe", "firefox.exe", "opera.exe", "brave.exe", "vivaldi.exe"};
    bool detectBrowser = false;
    int minStreamWidth = 480;
    int minStreamHeight = 270;
    // A launcher-titled GFN window that covers its whole monitor is treated as
    // an (unnamed) stream: the launcher never runs borderless-fullscreen.
    bool fullscreenHeuristic = true;
};

struct GfnClassification {
    GfnState state = GfnState::NotRunning;
    int streamIndex = -1;       // index into the snapshot vector
    std::string gameName;       // cleaned title ("" if unknown)
    bool fromBrowser = false;
    uint32_t pid = 0;
};

GfnClassification classifyGfn(const std::vector<WindowSnapshot>& windows, const DetectionRules& rules);

} // namespace bgn
