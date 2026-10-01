#pragma once
// Unhandled exception handler: restores global state we may have changed
// (cursor clip / visibility), writes a minidump (no heap => no personal data)
// to logs/ and records the crash in the log.

#include <filesystem>

namespace bgn {

using EmergencyCallback = void (*)();

void installCrashHandler(const std::filesystem::path& logsDir);
// Up to 8 callbacks that run first inside the crash handler (must be async-safe-ish).
void registerEmergencyCallback(EmergencyCallback cb);
// Test hook: raise an access violation.
[[noreturn]] void triggerTestCrash();

} // namespace bgn
