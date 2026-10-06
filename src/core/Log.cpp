#include "core/Log.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace bgn {
namespace {

struct LogState {
    std::mutex mutex;
    std::ofstream file;
    std::filesystem::path filePath;
    std::deque<LogLine> ring;
    std::vector<std::pair<std::string, std::string>> redactions;
    std::atomic<int> level{static_cast<int>(LogLevel::Info)};
    std::atomic<uint64_t> counter{0};
};

LogState& state() {
    static LogState s;
    return s;
}

std::tm localTime(std::time_t t) {
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

std::string timeOfDay() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm tm = localTime(system_clock::to_time_t(now));
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min, tm.tm_sec, int(ms));
    return buf;
}

std::string fileStamp() {
    std::tm tm = localTime(std::time(nullptr));
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d_%02d%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

constexpr size_t kRingSize = 600;

} // namespace

const char* toString(LogLevel level) {
    switch (level) {
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO";
    case LogLevel::Warning: return "WARN";
    case LogLevel::Error: return "ERROR";
    }
    return "?";
}

bool parseLogLevel(std::string_view text, LogLevel& out) {
    std::string t(text);
    std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (t == "debug") out = LogLevel::Debug;
    else if (t == "info") out = LogLevel::Info;
    else if (t == "warning" || t == "warn") out = LogLevel::Warning;
    else if (t == "error") out = LogLevel::Error;
    else return false;
    return true;
}

bool Log::init(const std::filesystem::path& directory, LogLevel minLevel, int keepFiles) {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    s.level = static_cast<int>(minLevel);
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    // Rotate: keep the newest (keepFiles - 1) existing logs, the new one makes keepFiles.
    std::vector<std::filesystem::path> existing;
    for (auto it = std::filesystem::directory_iterator(directory, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        const auto& p = it->path();
        if (p.extension() == ".log" && p.filename().string().rfind("BetterGFNNeural_", 0) == 0) existing.push_back(p);
    }
    std::sort(existing.begin(), existing.end());
    while (!existing.empty() && int(existing.size()) >= keepFiles) {
        std::filesystem::remove(existing.front(), ec);
        existing.erase(existing.begin());
    }
    if (s.file.is_open()) s.file.close();
    // Two launches within the same second (e.g. a crash and the automatic
    // restart) must not overwrite each other's log.
    const std::string stamp = fileStamp();
    s.filePath = directory / ("BetterGFNNeural_" + stamp + ".log");
    for (int n = 2; std::filesystem::exists(s.filePath, ec) && n < 100; ++n)
        s.filePath = directory / ("BetterGFNNeural_" + stamp + "_" + std::to_string(n) + ".log");
    s.file.open(s.filePath, std::ios::out | std::ios::trunc | std::ios::binary);
    return s.file.is_open();
}

void Log::shutdown() {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    if (s.file.is_open()) {
        s.file.flush();
        s.file.close();
    }
}

void Log::setLevel(LogLevel level) { state().level = static_cast<int>(level); }
LogLevel Log::level() { return static_cast<LogLevel>(state().level.load()); }

void Log::addRedaction(std::string secret, std::string replacement) {
    if (secret.size() < 2) return; // avoid redacting trivial strings
    auto& s = state();
    std::lock_guard lock(s.mutex);
    for (auto& r : s.redactions)
        if (r.first == secret) return;
    s.redactions.emplace_back(std::move(secret), std::move(replacement));
    // Longest first so that a full profile path is replaced before the bare user name.
    std::sort(s.redactions.begin(), s.redactions.end(), [](auto& a, auto& b) { return a.first.size() > b.first.size(); });
}

static std::string applyRedactions(std::string_view message, const std::vector<std::pair<std::string, std::string>>& redactions) {
    std::string out(message);
    for (const auto& [secret, repl] : redactions) {
        size_t pos = 0;
        // case-insensitive ASCII search (Windows paths are case-insensitive)
        auto lower = [](std::string v) {
            std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            return v;
        };
        std::string lo = lower(out), ls = lower(secret);
        std::string result;
        size_t last = 0;
        while ((pos = lo.find(ls, last)) != std::string::npos) {
            result.append(out, last, pos - last);
            result += repl;
            last = pos + ls.size();
        }
        result.append(out, last, std::string::npos);
        out.swap(result);
    }
    return out;
}

std::string Log::sanitize(std::string_view message) {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    return applyRedactions(message, s.redactions);
}

void Log::write(LogLevel level, std::string_view module, std::string_view message) {
    auto& s = state();
    if (static_cast<int>(level) < s.level.load()) return;
    LogLine line{level, timeOfDay(), std::string(module), {}};
    std::lock_guard lock(s.mutex);
    line.message = applyRedactions(message, s.redactions);
    std::string text = std::format("{} [{:<5}] [{}] {}\n", line.time, toString(level), line.module, line.message);
    if (s.file.is_open()) {
        // Log volume is low (no per-frame logging), so flush every line to keep
        // the file useful after a crash.
        s.file << text;
        s.file.flush();
    }
#ifdef _WIN32
    OutputDebugStringA(text.c_str());
#endif
    s.ring.push_back(std::move(line));
    while (s.ring.size() > kRingSize) s.ring.pop_front();
    s.counter.fetch_add(1);
}

std::vector<LogLine> Log::recent(size_t maxLines) {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    size_t n = std::min(maxLines, s.ring.size());
    return std::vector<LogLine>(s.ring.end() - static_cast<std::ptrdiff_t>(n), s.ring.end());
}

std::filesystem::path Log::currentFile() {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    return s.filePath;
}

uint64_t Log::lineCounter() { return state().counter.load(); }

} // namespace bgn
