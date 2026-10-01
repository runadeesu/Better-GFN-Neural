#pragma once
// AUTO MODE: picks the quality tier and frame-interpolation state in real time
// from measured GPU cost, frame rates, latency, VRAM and GPU load. Portable,
// deterministic (time is passed in) and unit tested.

#include <string>

#include "settings/Presets.h"
#include "telemetry/RollingStats.h"

namespace bgn {

struct AutoInputs {
    double nowSeconds = 0;
    double inputFps = 0;          // captured frames/s from GFN
    double refreshHz = 60;        // output display refresh
    double gpuMsAvg = 0;          // our pipeline GPU time per input frame (incl. interpolation)
    double gpuMsP95 = 0;
    double frameGenMs = 0;        // GPU time of the interpolation pass alone (0 if inactive)
    double gpuUtilization = -1;   // whole-GPU 3D utilization 0..1, -1 unknown
    double cpuUtilization = -1;   // 0..1, -1 unknown
    double vramUsageMB = 0;       // our process' local VRAM usage
    double vramBudgetMB = 0;      // OS budget for our process (0 unknown)
    double addedLatencyMs = 0;    // capture -> present, excluding GFN itself
    int droppedFramesDelta = 0;   // frames dropped since the previous update
    bool frameGenActive = false;
};

struct AutoDecision {
    int tier = 4;
    bool frameGen = false;
    bool frameGenBeneficial = false;  // refresh rate leaves room for interpolated frames
    double budgetMs = 0;
    std::string reason;
    bool changed = false;
};

class AutoModeController {
public:
    AutoModeController();

    // Configure for a preset/profile. Resets hysteresis when the policy changes.
    void configure(const PresetPolicy& policy, FrameGenMode fgMode, bool autoMode, int initialTier);
    void setFixedTier(int tier); // used when Auto Mode is off (still applies the safety governor)

    AutoDecision update(const AutoInputs& in);

    int tier() const { return tier_; }
    bool frameGen() const { return frameGen_; }
    const PresetPolicy& policy() const { return policy_; }

    // Budget for our GPU work per input frame (ms) at a given input fps.
    static double gpuBudgetMs(double inputFps, double fraction);
    // Interpolated frame display is only useful if the display can show them.
    static bool frameGenBeneficial(double inputFps, double refreshHz);
    // Hold-back latency added by 2x interpolation (half an input frame interval).
    static double frameGenHoldbackMs(double inputFps);

private:
    void step(int delta, double now, const char* why, AutoDecision& d);

    PresetPolicy policy_;
    FrameGenMode fgMode_ = FrameGenMode::Auto;
    bool autoMode_ = true;
    int tier_ = 4;
    bool frameGen_ = false;

    double lastChange_ = -1e9;
    double lastFgChange_ = -1e9;
    double overSince_ = -1;      // time the budget was first exceeded
    double underSince_ = -1;     // time we first had comfortable headroom
    double lastDowngradeAt_[kTierCount];
    int pendingFgOn_ = 0;
};

} // namespace bgn
