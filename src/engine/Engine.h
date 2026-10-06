#pragma once
// Real-time processing engine (dedicated thread, MMCSS "Games" class).
// Owns the GPU device, capture session, processing pipeline, Auto Mode
// controller and the output overlay. The UI thread only exchanges immutable
// config snapshots and reads stats, so a stuck UI never stalls the video path.

#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "platform/SystemMonitor.h"
#include "platform/Win32.h"
#include "renderer/GpuTimer.h"
#include "settings/Presets.h"
#include "settings/Settings.h"

namespace bgn {

struct EngineTarget {
    HWND hwnd = nullptr;
    DWORD pid = 0;
    std::string gameName;
    bool browser = false;
    bool operator==(const EngineTarget& o) const { return hwnd == o.hwnd && pid == o.pid && gameName == o.gameName; }
};

struct EngineConfig {
    bool enabled = true;
    bool autoMode = true;
    Preset preset = Preset::Auto;
    PerformancePriority priority = PerformancePriority::Balanced;
    bool lowLatency = true;
    EnhancementSettings enhancement;
    OutputMode outputMode = OutputMode::Auto;
    OutputResolution outputResolution = OutputResolution::Auto;
    StreamResolution streamResolution = StreamResolution::Auto;
    CaptureBackend captureBackend = CaptureBackend::Auto;
    std::string preferredMonitor;
    bool compareSplit = false;
    AccessibilitySettings accessibility;
    OsdSettings osd;
    PowerSettings power;
    int initialTier = -1; // -1 = derive from GPU / benchmark
    bool forceWarp = false;
    bool safeMode = false;
    uint64_t revision = 0; // increments on every change
};

struct EngineStats {
    std::string state = "Idle";
    std::string lastError;
    bool active = false;          // capturing + processing
    bool overlayVisible = false;
    std::string captureBackend;
    std::string presentPath;
    // GPU
    std::string gpuName, gpuFamily, driverVersion;
    double vramTotalMB = 0;
    bool halfPrecision = false, softwareGpu = false;
    // Rates / timings
    double inputFps = 0, outputFps = 0, captureFps = 0;
    int captureW = 0, captureH = 0, procW = 0, procH = 0, outW = 0, outH = 0, displayW = 0, displayH = 0;
    double gpuMsAvg = 0, gpuMsMax = 0, gpuMsP95 = 0;
    double frameTimeMs = 0;     // output frame interval
    double addedLatencyMs = 0;  // capture -> present (incl. interpolation hold-back)
    std::array<double, kGpuStageCount> stageMs{};
    uint64_t capturedFrames = 0, droppedFrames = 0, presentedFrames = 0, interpolatedFrames = 0;
    // Auto Mode
    int tier = 0;
    std::string tierName;
    UpscalerKind upscaler = UpscalerKind::None;
    bool upscalerReduced = false;
    bool frameGenActive = false, frameGenBeneficial = false;
    FrameGenMode frameGenMode = FrameGenMode::Auto;
    std::string autoReason;
    double budgetMs = 0;
    // System
    double vramUsageMB = 0, vramBudgetMB = 0, pipelineVramMB = 0;
    double gpuUtil = -1, cpuUtil = -1;
    // Display
    bool hdrOutput = false, hdrPlus = false, hdrInput = false;
    double refreshHz = 0;
    std::string monitorName;
    std::string outputModeUsed;
    int detectedStreamHeight = 0;
    double contentFactor = 0;
    bool cursorConfined = false;
    // History for graphs (oldest first)
    std::vector<float> gpuMsHistory, inputFpsHistory, outputFpsHistory;
};

class Engine {
public:
    Engine();
    ~Engine();
    void start();
    void stop();

    void setTarget(const EngineTarget& t);
    void setConfig(const EngineConfig& c);
    void setSystemSample(const SystemSample& s);
    void notifyDisplayChange();
    void setSuspended(bool suspended);

    EngineStats stats() const;
    bool running() const { return running_.load(); }

    struct Session; // render-thread state (defined in Engine.cpp)

private:
    void threadMain();
    void loopOnce(Session& s);
    void publish(const EngineStats& st);

    std::thread thread_;
    std::atomic<bool> quit_{false}, running_{false};
    UniqueHandle wake_;

    mutable std::mutex mutex_;
    EngineTarget target_;
    EngineConfig config_;
    SystemSample system_;
    bool displayChanged_ = true;
    bool suspended_ = false;
    EngineStats stats_;
};

} // namespace bgn
