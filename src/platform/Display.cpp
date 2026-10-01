#include "platform/Display.h"

#include <dxgi1_6.h>
#include <shellscalingapi.h>

#include <cmath>
#include <format>

#include "core/Log.h"
#include "core/StringUtil.h"

namespace bgn {

namespace {

struct DisplayConfigEntry {
    std::wstring gdiName;
    std::wstring friendly;
    double refresh = 0;
    bool acSupported = false, acEnabled = false;
    int bits = 8;
    float sdrWhite = 80.0f;
};

std::vector<DisplayConfigEntry> queryDisplayConfig() {
    std::vector<DisplayConfigEntry> out;
    UINT32 numPaths = 0, numModes = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &numPaths, &numModes) != ERROR_SUCCESS) return out;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(numPaths);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(numModes);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &numPaths, paths.data(), &numModes, modes.data(), nullptr) != ERROR_SUCCESS) return out;
    paths.resize(numPaths);
    for (const auto& p : paths) {
        DisplayConfigEntry e;
        DISPLAYCONFIG_SOURCE_DEVICE_NAME src{};
        src.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        src.header.size = sizeof(src);
        src.header.adapterId = p.sourceInfo.adapterId;
        src.header.id = p.sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&src.header) == ERROR_SUCCESS) e.gdiName = src.viewGdiDeviceName;
        DISPLAYCONFIG_TARGET_DEVICE_NAME tgt{};
        tgt.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        tgt.header.size = sizeof(tgt);
        tgt.header.adapterId = p.targetInfo.adapterId;
        tgt.header.id = p.targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&tgt.header) == ERROR_SUCCESS) e.friendly = tgt.monitorFriendlyDeviceName;
        if (p.targetInfo.refreshRate.Denominator) e.refresh = double(p.targetInfo.refreshRate.Numerator) / double(p.targetInfo.refreshRate.Denominator);
        DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO ac{};
        ac.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
        ac.header.size = sizeof(ac);
        ac.header.adapterId = p.targetInfo.adapterId;
        ac.header.id = p.targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&ac.header) == ERROR_SUCCESS) {
            e.acSupported = ac.advancedColorSupported;
            e.acEnabled = ac.advancedColorEnabled;
            e.bits = int(ac.bitsPerColorChannel);
        }
        DISPLAYCONFIG_SDR_WHITE_LEVEL wl{};
        wl.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
        wl.header.size = sizeof(wl);
        wl.header.adapterId = p.targetInfo.adapterId;
        wl.header.id = p.targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&wl.header) == ERROR_SUCCESS && wl.SDRWhiteLevel > 0) e.sdrWhite = float(wl.SDRWhiteLevel) / 1000.0f * 80.0f;
        out.push_back(e);
    }
    return out;
}

BOOL CALLBACK monitorEnumProc(HMONITOR hmon, HDC, LPRECT, LPARAM lp) {
    auto* list = reinterpret_cast<std::vector<MonitorInfo>*>(lp);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hmon, &mi)) return TRUE;
    MonitorInfo m;
    m.handle = hmon;
    m.deviceName = narrow(mi.szDevice);
    m.rect = mi.rcMonitor;
    m.work = mi.rcWork;
    m.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    m.width = mi.rcMonitor.right - mi.rcMonitor.left;
    m.height = mi.rcMonitor.bottom - mi.rcMonitor.top;
    UINT dx = 96, dy = 96;
    if (SUCCEEDED(GetDpiForMonitor(hmon, MDT_EFFECTIVE_DPI, &dx, &dy))) m.dpi = dx;
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1) m.refreshHz = dm.dmDisplayFrequency;
    list->push_back(m);
    return TRUE;
}

} // namespace

std::vector<MonitorInfo> enumerateMonitors() {
    std::vector<MonitorInfo> list;
    EnumDisplayMonitors(nullptr, nullptr, monitorEnumProc, reinterpret_cast<LPARAM>(&list));
    // Precise refresh rate, friendly name, advanced color + SDR white level
    auto dc = queryDisplayConfig();
    for (auto& m : list) {
        for (const auto& e : dc) {
            if (narrow(e.gdiName) != m.deviceName) continue;
            if (e.refresh > 1) m.refreshHz = e.refresh;
            m.friendlyName = narrow(e.friendly);
            m.hdrSupported = e.acSupported;
            m.hdrEnabled = e.acEnabled;
            m.bitsPerColor = e.bits;
            m.sdrWhiteNits = e.sdrWhite;
        }
    }
    // DXGI: HDR color space + luminance metadata + driving adapter
    ComPtr<IDXGIFactory1> factory;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT a = 0; factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a, adapter.Reset()) {
            DXGI_ADAPTER_DESC1 ad{};
            adapter->GetDesc1(&ad);
            ComPtr<IDXGIOutput> output;
            for (UINT o = 0; adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND; ++o, output.Reset()) {
                ComPtr<IDXGIOutput6> o6;
                if (FAILED(output.As(&o6))) continue;
                DXGI_OUTPUT_DESC1 d{};
                if (FAILED(o6->GetDesc1(&d))) continue;
                for (auto& m : list) {
                    if (m.handle != d.Monitor) continue;
                    m.adapterLuid = ad.AdapterLuid;
                    m.maxLuminance = d.MaxLuminance;
                    m.minLuminance = d.MinLuminance;
                    m.maxFullFrameLuminance = d.MaxFullFrameLuminance;
                    if (d.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) m.hdrEnabled = true;
                    if (d.BitsPerColor) m.bitsPerColor = int(d.BitsPerColor);
                }
            }
        }
    }
    return list;
}

const MonitorInfo* findMonitor(const std::vector<MonitorInfo>& monitors, HMONITOR h) {
    for (const auto& m : monitors)
        if (m.handle == h) return &m;
    return nullptr;
}

const MonitorInfo* findMonitorByName(const std::vector<MonitorInfo>& monitors, const std::string& name) {
    for (const auto& m : monitors)
        if (m.deviceName == name) return &m;
    return nullptr;
}

bool presentTearingSupported() {
    ComPtr<IDXGIFactory5> f5;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&f5)))) return false;
    BOOL allow = FALSE;
    if (FAILED(f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow)))) return false;
    return allow == TRUE;
}

std::string describeMonitor(const MonitorInfo& m) {
    return std::format("{} {}x{} @ {:.2f} Hz, HDR {}{}, SDR white {:.0f} nits, peak {:.0f} nits, {} bpc", m.friendlyName.empty() ? m.deviceName : m.friendlyName,
                       m.width, m.height, m.refreshHz, m.hdrEnabled ? "on" : "off", m.hdrSupported ? "" : " (unsupported)", m.sdrWhiteNits, m.maxLuminance,
                       m.bitsPerColor);
}

} // namespace bgn
