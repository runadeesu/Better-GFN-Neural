#include "platform/Startup.h"

#include "core/Log.h"
#include "platform/Win32.h"

namespace bgn {

static const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* kValueName = L"BetterGFNNeural";

bool isStartWithWindowsEnabled() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    DWORD type = 0, size = 0;
    bool exists = RegQueryValueExW(key, kValueName, nullptr, &type, nullptr, &size) == ERROR_SUCCESS && type == REG_SZ;
    RegCloseKey(key);
    return exists;
}

bool setStartWithWindows(bool enable, const std::filesystem::path& exePath) {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        BGN_LOG_ERROR("Startup", "cannot open Run key");
        return false;
    }
    LSTATUS st;
    if (enable) {
        std::wstring cmd = L"\"" + exePath.wstring() + L"\" --background";
        st = RegSetValueExW(key, kValueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()), DWORD((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        st = RegDeleteValueW(key, kValueName);
        if (st == ERROR_FILE_NOT_FOUND) st = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    BGN_LOG_INFO("Startup", "start with Windows {} ({})", enable ? "enabled" : "disabled", st == ERROR_SUCCESS ? "ok" : "failed");
    return st == ERROR_SUCCESS;
}

} // namespace bgn
