#include "gfn/GfnDetector.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <filesystem>
#include <iterator>

#include "core/Log.h"
#include "core/StringUtil.h"

namespace bgn {

namespace fs = std::filesystem;

namespace {

struct EnumCtx {
    GfnDetector* self;
    std::vector<HWND> windows;
};

BOOL CALLBACK enumProc(HWND hwnd, LPARAM lp) {
    reinterpret_cast<EnumCtx*>(lp)->windows.push_back(hwnd);
    return TRUE;
}

bool isCandidate(const DetectionRules& r, const std::string& name) {
    for (const auto& p : r.processNames)
        if (iequalsAscii(p, name)) return true;
    if (r.detectBrowser)
        for (const auto& p : r.browserProcessNames)
            if (iequalsAscii(p, name)) return true;
    return false;
}

} // namespace

std::string GfnDetector::processName(DWORD pid) {
    const double now = double(GetTickCount64()) / 1000.0;
    auto it = nameCache_.find(pid);
    if (it != nameCache_.end() && now - it->second.time < 30.0) return it->second.name;
    std::string name;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (h) {
        wchar_t buf[MAX_PATH * 2];
        DWORD size = DWORD(std::size(buf));
        if (QueryFullProcessImageNameW(h, 0, buf, &size)) name = toLowerAscii(narrow(fs::path(std::wstring(buf, size)).filename().wstring()));
        CloseHandle(h);
    }
    nameCache_[pid] = {name, now};
    if (nameCache_.size() > 512) nameCache_.clear();
    return name;
}

GfnStatus GfnDetector::poll() {
    EnumCtx ctx{this, {}};
    EnumWindows(enumProc, reinterpret_cast<LPARAM>(&ctx));
    HWND fg = GetForegroundWindow();
    snapshots_.clear();
    std::vector<HWND> handles;
    for (HWND hwnd : ctx.windows) {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (!pid || pid == GetCurrentProcessId()) continue;
        std::string pname = processName(pid);
        if (pname.empty() || !isCandidate(rules_, pname)) continue;
        WindowSnapshot w;
        w.handle = reinterpret_cast<uint64_t>(hwnd);
        w.pid = pid;
        w.processName = pname;
        wchar_t title[512] = {};
        GetWindowTextW(hwnd, title, 512);
        w.title = narrow(title);
        wchar_t cls[128] = {};
        GetClassNameW(hwnd, cls, 128);
        w.className = narrow(cls);
        RECT r{};
        GetWindowRect(hwnd, &r);
        w.x = r.left;
        w.y = r.top;
        w.width = r.right - r.left;
        w.height = r.bottom - r.top;
        RECT c{};
        GetClientRect(hwnd, &c);
        w.clientWidth = c.right;
        w.clientHeight = c.bottom;
        w.visible = IsWindowVisible(hwnd) != FALSE;
        w.minimized = IsIconic(hwnd) != FALSE;
        DWORD cloaked = 0;
        if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))) w.cloaked = cloaked != 0;
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) {
            w.monitorWidth = mi.rcMonitor.right - mi.rcMonitor.left;
            w.monitorHeight = mi.rcMonitor.bottom - mi.rcMonitor.top;
        }
        w.foreground = hwnd == fg;
        snapshots_.push_back(std::move(w));
        handles.push_back(hwnd);
    }
    GfnClassification c = classifyGfn(snapshots_, rules_);
    GfnStatus s;
    s.state = c.state;
    s.pid = c.pid;
    s.gameName = c.gameName;
    s.fromBrowser = c.fromBrowser;
    if (c.streamIndex >= 0) {
        s.streamWindow = handles[size_t(c.streamIndex)];
        const auto& w = snapshots_[size_t(c.streamIndex)];
        s.windowW = w.clientWidth;
        s.windowH = w.clientHeight;
        s.foreground = w.foreground;
    }
    return s;
}

static fs::path knownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    fs::path out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p)) && p) out = p;
    if (p) CoTaskMemFree(p);
    return out;
}

std::wstring findGeForceNowExecutable(const std::string& overridePath) {
    std::error_code ec;
    if (!overridePath.empty() && fs::exists(widen(overridePath), ec)) return widen(overridePath);
    std::vector<fs::path> candidates;
    for (const fs::path& base : {knownFolder(FOLDERID_LocalAppData), knownFolder(FOLDERID_ProgramFiles), knownFolder(FOLDERID_ProgramFilesX86)}) {
        if (base.empty()) continue;
        fs::path dir = base / L"NVIDIA Corporation" / L"GeForceNOW";
        candidates.push_back(dir / L"CEF" / L"GeForceNOWLauncher.exe");
        candidates.push_back(dir / L"CEF" / L"GeForceNOW.exe");
        candidates.push_back(dir / L"GeForceNOW.exe");
    }
    for (const auto& c : candidates)
        if (fs::exists(c, ec)) return c.wstring();
    // Start menu shortcut
    for (const fs::path& base : {knownFolder(FOLDERID_Programs), knownFolder(FOLDERID_CommonPrograms)}) {
        if (base.empty()) continue;
        for (auto it = fs::recursive_directory_iterator(base, fs::directory_options::skip_permission_denied, ec); !ec && it != fs::recursive_directory_iterator();
             it.increment(ec)) {
            if (it.depth() > 2) {
                it.disable_recursion_pending();
                continue;
            }
            const fs::path& p = it->path();
            if (p.extension() == L".lnk" && icontainsAscii(narrow(p.filename().wstring()), "geforce now")) return p.wstring();
        }
    }
    return {};
}

bool launchGeForceNow(const std::string& overridePath, std::string& error) {
    std::wstring exe = findGeForceNowExecutable(overridePath);
    if (exe.empty()) {
        error = "GeForce NOW was not found. Install it from nvidia.com or set its location in Settings.";
        return false;
    }
    HINSTANCE r = ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(r) <= 32) {
        error = "GeForce NOW could not be started (" + lastErrorString() + ")";
        return false;
    }
    BGN_LOG_INFO("GFN", "launched GeForce NOW");
    return true;
}

} // namespace bgn
