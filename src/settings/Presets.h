#pragma once
// Quality tiers, preset policies and resolution of user settings into the
// concrete per-stage configuration consumed by the GPU pipeline. Portable.

#include <string>

#include "settings/Settings.h"

namespace bgn {

constexpr int kTierCount = 7; // 0 (minimal) .. 6 (ultra)
constexpr int kMaxTier = kTierCount - 1;

const char* tierName(int tier);

struct PresetPolicy {
    int minTier = 0;
    int maxTier = kMaxTier;
    int startTier = 4;
    bool allowFrameGen = true;
    double gpuBudgetFraction = 0.5; // share of the input frame interval our GPU work may use
    double latencyBudgetMs = 14.0;  // max added latency (incl. interpolation hold-back)
};

PresetPolicy policyFor(Preset preset, bool lowLatencyMode, PerformancePriority priority = PerformancePriority::Balanced);

enum class UpscalerKind { None, Bilinear, LanczosAR, NsrT, NsrS, NsrL };
const char* toString(UpscalerKind k);

struct ColorParams {
    bool enabled = true;
    bool automatic = true;
    float blackLevel = 0, whiteLevel = 0, contrast = 0, gamma = 1, saturation = 0, vibrance = 0.15f, temperature = 0;
    float highlightRecovery = 0.3f, shadowDetail = 0.25f, localContrast = 0.25f;
    bool toneMapping = true;
};

// Fully-resolved pipeline configuration for one tier.
struct EffectiveConfig {
    int tier = 4;
    UpscalerKind upscaler = UpscalerKind::NsrS;
    bool upscalerReduced = false; // the user's upscaler choice was lowered by Auto Mode

    float deblock = 0, deband = 0, denoise = 0; // 0 = off
    bool darkSceneCleanup = false;
    bool cleanupHQ = false;

    float temporal = 0; // 0 = off
    float deblur = 0;
    bool motionDeblur = false;

    float sharpen = 0;
    bool adaptiveSharpen = false;
    bool textBoost = false;
    bool skinProtect = false;

    ColorParams color;
    float monochrome = 0;   // visual style: 0..1 desaturation
    float splitTone = 0;    // visual style: cinematic split toning 0..1
    bool adaptiveCleanup = false; // cleanup follows the measured stream compression level
    TriState hdrMode = TriState::Auto;
    float hdrIntensity = 0.5f;
    float peakNitsOverride = 0, paperWhiteOverride = 0;

    FrameGenMode frameGenMode = FrameGenMode::Auto;
    int flowQuality = 0; // 0 = no optical flow, 1 = fast, 2 = high quality

    bool needsFlow() const { return flowQuality > 0; }
};

// Resolves enhancement settings + tier into an EffectiveConfig.
// |autoMode| true: user choices are upper bounds that the tier can lower.
EffectiveConfig resolveConfig(const EnhancementSettings& e, int tier, bool autoMode);

// Stream quality driven cleanup: scales the automatic cleanup strengths by the
// measured compression level (blockiness 0..1). Only touches features in Auto.
void applyAdaptiveCleanup(EffectiveConfig& c, const EnhancementSettings& e, double blockiness);

// Battery saver: caps the tier and disables frame interpolation on battery.
int batteryTierCap(bool onBattery, bool batterySaver, int batteryMaxTier);

// Rough initial tier guess from the GPU name/vendor before measurements exist.
int estimateTierForGpu(unsigned vendorId, const std::string& gpuName, double dedicatedVramGB);

// Classifies the GPU into a family string for the UI ("NVIDIA GeForce RTX 40 Series", ...).
std::string gpuFamily(unsigned vendorId, const std::string& gpuName);

// Whether the GPU benefits from half precision (min16float) shader variants.
bool preferHalfPrecision(unsigned vendorId, const std::string& gpuName);

} // namespace bgn
