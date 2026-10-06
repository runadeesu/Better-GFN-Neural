#include "telemetry/SessionHistory.h"

#include <algorithm>
#include <cstdio>
#include <ctime>

#include <nlohmann/json.hpp>

namespace bgn {

using nlohmann::json;

void SessionRecorder::begin(const std::string& game, int64_t startUnix) {
    *this = SessionRecorder{};
    active_ = true;
    game_ = game;
    start_ = startUnix;
}

void SessionRecorder::addSample(const SessionSample& s, double dt) {
    if (!active_ || !(dt > 0) || dt > 10) return;
    seconds_ += dt;
    weight_ += dt;
    sumIn_ += s.inputFps * dt;
    sumOut_ += s.outputFps * dt;
    sumGpu_ += s.gpuMs * dt;
    sumLat_ += s.latencyMs * dt;
    if (s.quality >= 0) {
        sumQ_ += s.quality * dt;
        weightQ_ += dt;
    }
    if (s.frameGen) fgWeight_ += dt;
    tierTime_[s.tier] += dt;
    if (!s.upscaler.empty()) upscalerTime_[s.upscaler] += dt;
    if (!haveDropped_) {
        droppedFirst_ = s.droppedFrames;
        haveDropped_ = true;
    }
    droppedLast_ = std::max(droppedLast_, s.droppedFrames);
}

bool SessionRecorder::finish(SessionRecord& out, double minSeconds) {
    if (!active_) return false;
    active_ = false;
    if (seconds_ < minSeconds || weight_ <= 0) return false;
    out = SessionRecord{};
    out.game = game_;
    out.startUnix = start_;
    out.durationSec = seconds_;
    out.avgInputFps = sumIn_ / weight_;
    out.avgOutputFps = sumOut_ / weight_;
    out.avgGpuMs = sumGpu_ / weight_;
    out.avgLatencyMs = sumLat_ / weight_;
    out.avgQuality = weightQ_ > 0 ? sumQ_ / weightQ_ : -1.0;
    out.frameGenShare = fgWeight_ / weight_;
    auto longest = [](const auto& m) {
        auto it = std::max_element(m.begin(), m.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
        return it;
    };
    if (!tierTime_.empty()) out.dominantTier = longest(tierTime_)->first;
    if (!upscalerTime_.empty()) out.upscaler = longest(upscalerTime_)->first;
    out.droppedFrames = droppedLast_ >= droppedFirst_ ? droppedLast_ - droppedFirst_ : 0;
    return true;
}

static json toJson(const SessionRecord& r) {
    return json{{"game", r.game},
                {"start", r.startUnix},
                {"duration_s", r.durationSec},
                {"input_fps", r.avgInputFps},
                {"output_fps", r.avgOutputFps},
                {"gpu_ms", r.avgGpuMs},
                {"latency_ms", r.avgLatencyMs},
                {"quality", r.avgQuality},
                {"tier", r.dominantTier},
                {"upscaler", r.upscaler},
                {"frame_interpolation_share", r.frameGenShare},
                {"dropped_frames", r.droppedFrames}};
}

std::string historyToJson(const std::vector<SessionRecord>& records) {
    json arr = json::array();
    for (const auto& r : records) arr.push_back(toJson(r));
    return json{{"version", 1}, {"sessions", arr}}.dump(1);
}

bool historyFromJson(const std::string& text, std::vector<SessionRecord>& out) {
    try {
        json j = json::parse(text);
        if (!j.is_object() || !j.contains("sessions") || !j["sessions"].is_array()) return false;
        std::vector<SessionRecord> v;
        for (const auto& e : j["sessions"]) {
            if (!e.is_object()) continue;
            SessionRecord r;
            r.game = e.value("game", std::string());
            r.startUnix = e.value("start", int64_t(0));
            r.durationSec = e.value("duration_s", 0.0);
            r.avgInputFps = e.value("input_fps", 0.0);
            r.avgOutputFps = e.value("output_fps", 0.0);
            r.avgGpuMs = e.value("gpu_ms", 0.0);
            r.avgLatencyMs = e.value("latency_ms", 0.0);
            r.avgQuality = e.value("quality", -1.0);
            r.dominantTier = e.value("tier", 0);
            r.upscaler = e.value("upscaler", std::string());
            r.frameGenShare = e.value("frame_interpolation_share", 0.0);
            r.droppedFrames = e.value("dropped_frames", uint64_t(0));
            v.push_back(std::move(r));
        }
        if (v.size() > kMaxHistoryRecords) v.erase(v.begin(), v.end() - kMaxHistoryRecords);
        out = std::move(v);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

static std::string csvField(const std::string& s) {
    if (s.find_first_of(",\"\n\r") == std::string::npos) return s;
    std::string o = "\"";
    for (char c : s) {
        if (c == '"') o += '"';
        o += c;
    }
    return o + "\"";
}

std::string historyToCsv(const std::vector<SessionRecord>& records) {
    // UTF-8 BOM so that Excel opens Japanese game names correctly
    std::string out = "\xEF\xBB\xBF";
    out += "game,start_utc,duration_min,avg_input_fps,avg_output_fps,avg_gpu_ms,avg_added_latency_ms,stream_quality,tier,upscaler,frame_interpolation_pct,dropped_frames\r\n";
    for (const auto& r : records) {
        std::time_t t = std::time_t(r.startUnix);
        std::tm tm{};
#ifdef _WIN32
        gmtime_s(&tm, &t);
#else
        gmtime_r(&t, &tm);
#endif
        char start[64], nums[256];
        std::snprintf(start, sizeof(start), "%04d-%02d-%02d %02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
        std::snprintf(nums, sizeof(nums), "%.1f,%.1f,%.1f,%.2f,%.1f,%s,%d,", r.durationSec / 60.0, r.avgInputFps, r.avgOutputFps, r.avgGpuMs, r.avgLatencyMs,
                      r.avgQuality >= 0 ? std::to_string(int(r.avgQuality + 0.5)).c_str() : "", r.dominantTier);
        char tail[64];
        std::snprintf(tail, sizeof(tail), ",%.0f,%llu", r.frameGenShare * 100.0, static_cast<unsigned long long>(r.droppedFrames));
        out += csvField(r.game.empty() ? std::string("GeForce NOW") : r.game) + "," + start + "," + nums + csvField(r.upscaler) + tail + "\r\n";
    }
    return out;
}

void appendHistory(std::vector<SessionRecord>& records, const SessionRecord& r) {
    records.push_back(r);
    if (records.size() > kMaxHistoryRecords) records.erase(records.begin(), records.end() - kMaxHistoryRecords);
}

std::vector<GameSummary> summarizeHistory(const std::vector<SessionRecord>& records) {
    std::map<std::string, GameSummary> m;
    std::map<std::string, double> qWeight, qSum, fpsSum;
    for (const auto& r : records) {
        const std::string key = r.game.empty() ? std::string("GeForce NOW") : r.game;
        GameSummary& g = m[key];
        g.game = key;
        ++g.sessions;
        g.totalSec += r.durationSec;
        fpsSum[key] += r.avgOutputFps * r.durationSec;
        if (r.avgQuality >= 0) {
            qSum[key] += r.avgQuality * r.durationSec;
            qWeight[key] += r.durationSec;
        }
        g.lastPlayedUnix = std::max(g.lastPlayedUnix, r.startUnix);
    }
    std::vector<GameSummary> out;
    for (auto& [key, g] : m) {
        if (g.totalSec > 0) g.avgOutputFps = fpsSum[key] / g.totalSec;
        if (qWeight[key] > 0) g.avgQuality = qSum[key] / qWeight[key];
        out.push_back(g);
    }
    std::sort(out.begin(), out.end(), [](const GameSummary& a, const GameSummary& b) { return a.totalSec > b.totalSec; });
    return out;
}

} // namespace bgn
