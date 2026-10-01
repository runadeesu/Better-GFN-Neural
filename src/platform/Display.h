#pragma once
// Monitor enumeration: geometry, exact refresh rate, HDR state and luminance,
// Windows SDR white level, DPI and the adapter that drives the output.

#include <string>
#include <vector>

#include "platform/Win32.h"

namespace bgn {

struct MonitorInfo {
    HMONITOR handle = nullptr;
    std::string deviceName;   // "\\.\DISPLAY1"
    std::string friendlyName; // "DELL U2723QE" (may be empty)
    RECT rect{};              // physical pixels, virtual desktop coordinates
    RECT work{};
    bool primary = false;
    int width = 0, height = 0;
    double refreshHz = 60.0;
    bool hdrSupported = false;
    bool hdrEnabled = false;
    float maxLuminance = 0, minLuminance = 0, maxFullFrameLuminance = 0;
    float sdrWhiteNits = 80.0f;
    int bitsPerColor = 8;
    unsigned dpi = 96;
    LUID adapterLuid{};
};

std::vector<MonitorInfo> enumerateMonitors();
const MonitorInfo* findMonitor(const std::vector<MonitorInfo>& monitors, HMONITOR h);
const MonitorInfo* findMonitorByName(const std::vector<MonitorInfo>& monitors, const std::string& deviceName);

// DXGI_FEATURE_PRESENT_ALLOW_TEARING (required for VRR / tearing presentation in windowed swap chains)
bool presentTearingSupported();

std::string describeMonitor(const MonitorInfo& m);

} // namespace bgn
