#include "platform/CrashHandler.h"

#include "platform/Win32.h"

#include <dbghelp.h>

#include <atomic>
#include <cstdio>
#include <ctime>

#include "core/Log.h"

namespace bgn {

namespace {
wchar_t gDumpDir[MAX_PATH] = {};
EmergencyCallback gCallbacks[8] = {};
std::atomic<int> gCallbackCount{0};
std::atomic<bool> gInHandler{false};

LONG WINAPI crashFilter(EXCEPTION_POINTERS* info) {
    if (gInHandler.exchange(true)) return EXCEPTION_CONTINUE_SEARCH;
    for (int i = 0; i < gCallbackCount.load(); ++i)
        if (gCallbacks[i]) gCallbacks[i]();

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t path[MAX_PATH];
    // Milliseconds + PID keep dumps from crashes in quick succession apart.
    swprintf_s(path, L"%s\\crash_%04d%02d%02d_%02d%02d%02d_%03d_%lu.dmp", gDumpDir, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
               st.wMilliseconds, GetCurrentProcessId());
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    bool dumped = false;
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{};
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = info;
        mei.ClientPointers = FALSE;
        // MiniDumpNormal: stacks + module list only (no heap memory => no personal data)
        dumped = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpNormal, &mei, nullptr, nullptr) == TRUE;
        CloseHandle(file);
    }
    char msg[256];
    std::snprintf(msg, sizeof(msg), "Unhandled exception 0x%08lX at %p; minidump %s", info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0ul,
                  info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionAddress : nullptr, dumped ? "written" : "failed");
    Log::write(LogLevel::Error, "Crash", msg);
    Log::shutdown();
    return EXCEPTION_EXECUTE_HANDLER;
}
} // namespace

void installCrashHandler(const std::filesystem::path& logsDir) {
    std::error_code ec;
    std::filesystem::create_directories(logsDir, ec);
    wcsncpy_s(gDumpDir, logsDir.wstring().c_str(), _TRUNCATE);
    SetUnhandledExceptionFilter(crashFilter);
}

void registerEmergencyCallback(EmergencyCallback cb) {
    int i = gCallbackCount.load();
    if (i < 8) {
        gCallbacks[i] = cb;
        gCallbackCount.store(i + 1);
    }
}

void triggerTestCrash() {
    volatile int* p = nullptr;
    *p = 42;
    for (;;) {
    }
}

} // namespace bgn
