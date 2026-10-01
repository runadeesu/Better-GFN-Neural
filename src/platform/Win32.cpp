#include "platform/Win32.h"

#include <cstdio>

#include "core/StringUtil.h"

namespace bgn {

std::string hrToString(HRESULT hr) {
    wchar_t* buf = nullptr;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, DWORD(hr), 0,
                             reinterpret_cast<wchar_t*>(&buf), 0, nullptr);
    std::string msg;
    if (n && buf) {
        msg = trim(narrow(buf));
        LocalFree(buf);
    }
    char code[32];
    std::snprintf(code, sizeof(code), "0x%08X", unsigned(hr));
    return msg.empty() ? std::string(code) : std::string(code) + " (" + msg + ")";
}

std::string lastErrorString(DWORD err) { return hrToString(HRESULT_FROM_WIN32(err)); }

static double qpcFrequency() {
    static const double freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return double(f.QuadPart);
    }();
    return freq;
}

double qpcSeconds() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return double(c.QuadPart) / qpcFrequency();
}

double qpcToSeconds(int64_t qpc) { return double(qpc) / qpcFrequency(); }

} // namespace bgn
