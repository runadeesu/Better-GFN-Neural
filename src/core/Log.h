#pragma once
// Thread-safe logger with file rotation, privacy sanitizing and an in-memory
// ring buffer for the in-app log viewer. Portable (no Win32 dependency).

#include <cstdint>
#include <deque>
#include <filesystem>
#include <format>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace bgn {

enum class LogLevel : int { Debug = 0, Info = 1, Warning = 2, Error = 3 };

const char* toString(LogLevel level);
bool parseLogLevel(std::string_view text, LogLevel& out);

struct LogLine {
    LogLevel level;
    std::string time;   // HH:MM:SS.mmm
    std::string module;
    std::string message;
};

class Log {
public:
    // Opens logs/<prefix>_<timestamp>.log inside |directory| and deletes old files
    // beyond |keepFiles|. Safe to call once; subsequent calls re-target the file.
    static bool init(const std::filesystem::path& directory, LogLevel minLevel, int keepFiles = 10);
    static void shutdown();

    static void setLevel(LogLevel level);
    static LogLevel level();

    // Any occurrence of |secret| in log output is replaced by |replacement|.
    // Used to strip the Windows user name / profile path from messages.
    static void addRedaction(std::string secret, std::string replacement);

    static void write(LogLevel level, std::string_view module, std::string_view message);

    // Snapshot of the most recent lines (newest last).
    static std::vector<LogLine> recent(size_t maxLines = 400);
    static std::filesystem::path currentFile();
    static uint64_t lineCounter();

    // Exposed for unit tests.
    static std::string sanitize(std::string_view message);

private:
    Log() = delete;
};

template <typename... Args>
inline void logf(LogLevel level, std::string_view module, std::format_string<Args...> fmt, Args&&... args) {
    if (level < Log::level()) return;
    Log::write(level, module, std::format(fmt, std::forward<Args>(args)...));
}

} // namespace bgn

#define BGN_LOG_DEBUG(module, ...) ::bgn::logf(::bgn::LogLevel::Debug, module, __VA_ARGS__)
#define BGN_LOG_INFO(module, ...) ::bgn::logf(::bgn::LogLevel::Info, module, __VA_ARGS__)
#define BGN_LOG_WARN(module, ...) ::bgn::logf(::bgn::LogLevel::Warning, module, __VA_ARGS__)
#define BGN_LOG_ERROR(module, ...) ::bgn::logf(::bgn::LogLevel::Error, module, __VA_ARGS__)
