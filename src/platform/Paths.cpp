#include "platform/Paths.h"

#include "platform/Win32.h"

#include <shlobj.h>

#include "app/CommandLine.h"
#include "core/Log.h"
#include "core/StringUtil.h"

namespace bgn {

namespace fs = std::filesystem;

static fs::path modulePath() {
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, buf.data(), DWORD(buf.size()));
        if (n == 0) return {};
        if (n < buf.size()) {
            buf.resize(n);
            break;
        }
        buf.resize(buf.size() * 2);
    }
    return fs::path(buf);
}

static fs::path knownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    fs::path out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &p)) && p) out = p;
    if (p) CoTaskMemFree(p);
    return out;
}

AppPaths resolveAppPaths(const CommandLine& cmd) {
    AppPaths a;
    a.exePath = modulePath();
    a.exeDir = a.exePath.parent_path();
    std::error_code ec;
    if (!cmd.dataDir.empty()) {
        a.dataDir = fs::path(widen(cmd.dataDir));
        a.portable = true;
    } else if (cmd.portable || fs::exists(a.exeDir / "portable.dat", ec)) {
        a.dataDir = a.exeDir;
        a.portable = true;
    } else {
        fs::path local = knownFolder(FOLDERID_LocalAppData);
        if (local.empty()) local = a.exeDir;
        a.dataDir = local / L"BetterGFNNeural";
    }
    fs::create_directories(a.dataDir, ec);
    a.logsDir = a.dataDir / L"logs";
    a.settingsFile = a.dataDir / L"settings.json";
    return a;
}

void registerPrivacyRedactions() {
    wchar_t buf[512];
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", buf, 512);
    if (n > 0 && n < 512) Log::addRedaction(narrow(buf), "%USERPROFILE%");
    n = GetEnvironmentVariableW(L"USERNAME", buf, 512);
    if (n > 0 && n < 512) Log::addRedaction(narrow(buf), "<user>");
    n = GetEnvironmentVariableW(L"COMPUTERNAME", buf, 512);
    if (n > 0 && n < 512) Log::addRedaction(narrow(buf), "<computer>");
}

} // namespace bgn
