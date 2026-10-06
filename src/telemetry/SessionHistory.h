#pragma once
// Session history (portable): one record per enhanced game session, stored in
// history.json next to settings.json. Only performance data is stored - no
// account, user or network information.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace bgn {

struct SessionRecord {
    std::string game;          // "Cyberpunk 2077" (empty: unnamed GeForce NOW stream)
    int64_t startUnix = 0;
    double durationSec = 0;    // time the enhanced picture was shown
    double avgInputFps = 0, avgOutputFps = 0;
    double avgGpuMs = 0, avgLatencyMs = 0;
    double avgQuality = -1;    // stream quality score, -1 = not measured
    int dominantTier = 0;      // most used Auto Mode tier
    std::string upscaler;      // most used upscaler
    double frameGenShare = 0;  // share of samples with frame interpolation active (0..1)
    uint64_t droppedFrames = 0;
    bool operator==(const SessionRecord&) const = default;
};

struct SessionSample {
    double inputFps = 0, outputFps = 0, gpuMs = 0, latencyMs = 0;
    double quality = -1; // -1 = not measured
    int tier = 0;
    std::string upscaler;
    bool frameGen = false;
    uint64_t droppedFrames = 0; // cumulative counter of the capture
};

// Accumulates 1 Hz samples of an enhanced session.
class SessionRecorder {
public:
    void begin(const std::string& game, int64_t startUnix);
    bool active() const { return active_; }
    const std::string& game() const { return game_; }
    void addSample(const SessionSample& s, double dtSeconds);
    // Ends the session. Returns false (and nothing to store) for sessions
    // shorter than |minSeconds|.
    bool finish(SessionRecord& out, double minSeconds = 30.0);

private:
    bool active_ = false;
    std::string game_;
    int64_t start_ = 0;
    double seconds_ = 0;
    double sumIn_ = 0, sumOut_ = 0, sumGpu_ = 0, sumLat_ = 0, sumQ_ = 0;
    double weight_ = 0, weightQ_ = 0, fgWeight_ = 0;
    std::map<int, double> tierTime_;
    std::map<std::string, double> upscalerTime_;
    uint64_t droppedFirst_ = 0, droppedLast_ = 0;
    bool haveDropped_ = false;
};

constexpr size_t kMaxHistoryRecords = 500;

std::string historyToJson(const std::vector<SessionRecord>& records);
bool historyFromJson(const std::string& json, std::vector<SessionRecord>& out);
std::string historyToCsv(const std::vector<SessionRecord>& records);
// Appends and keeps the newest kMaxHistoryRecords.
void appendHistory(std::vector<SessionRecord>& records, const SessionRecord& r);

struct GameSummary {
    std::string game;
    int sessions = 0;
    double totalSec = 0;
    double avgOutputFps = 0;  // time weighted
    double avgQuality = -1;   // time weighted over sessions that measured it
    int64_t lastPlayedUnix = 0;
};
// Per game, most played first.
std::vector<GameSummary> summarizeHistory(const std::vector<SessionRecord>& records);

} // namespace bgn
