#include "settings/Presets.h"

#include <algorithm>
#include <cctype>

#include "core/StringUtil.h"

namespace bgn {

const char* tierName(int tier) {
    static const char* names[kTierCount] = {"Minimal", "Light", "Performance", "Balanced Lite", "Balanced", "Quality", "Ultra"};
    return names[std::clamp(tier, 0, kMaxTier)];
}

const char* toString(UpscalerKind k) {
    switch (k) {
    case UpscalerKind::None: return "Native (no upscaling)";
    case UpscalerKind::Bilinear: return "Bilinear";
    case UpscalerKind::LanczosAR: return "Fast Reconstruct (Lanczos-AR)";
    case UpscalerKind::NsrT: return "Neural SR (Tiny)";
    case UpscalerKind::NsrS: return "Neural SR (S)";
    case UpscalerKind::NsrL: return "Neural SR (L)";
    }
    return "?";
}

PresetPolicy policyFor(Preset preset, bool lowLatencyMode, PerformancePriority priority) {
    PresetPolicy p;
    switch (preset) {
    case Preset::Auto: p = {0, kMaxTier, 4, true, 0.50, 14.0}; break;
    case Preset::Ultra: p = {0, kMaxTier, kMaxTier, true, 0.65, 18.0}; break;
    case Preset::Quality: p = {0, 5, 5, true, 0.55, 16.0}; break;
    case Preset::Balanced: p = {0, 4, 4, true, 0.50, 14.0}; break;
    case Preset::Performance: p = {0, 2, 2, true, 0.35, 12.0}; break;
    case Preset::LowLatency: p = {0, 3, 3, false, 0.30, 6.0}; break;
    }
    if (priority == PerformancePriority::Latency) {
        p.gpuBudgetFraction = std::min(p.gpuBudgetFraction, 0.35);
        p.latencyBudgetMs = std::min(p.latencyBudgetMs, 8.0);
    } else if (priority == PerformancePriority::Quality) {
        p.gpuBudgetFraction = std::max(p.gpuBudgetFraction, 0.6);
        p.latencyBudgetMs = std::max(p.latencyBudgetMs, 18.0);
    }
    if (lowLatencyMode) {
        // Low Latency Mode: tighter latency budget; interpolation is still allowed
        // as long as its hold-back stays under this budget (Auto mode checks it).
        p.latencyBudgetMs = std::min(p.latencyBudgetMs, 12.0);
    }
    return p;
}

static float pick(const Feature& f, float autoValue) {
    if (!f.enabled) return 0.0f;
    return f.automatic ? autoValue : f.strength;
}

EffectiveConfig resolveConfig(const EnhancementSettings& e, int tier, bool autoMode) {
    tier = std::clamp(tier, 0, kMaxTier);
    EffectiveConfig c;
    c.tier = tier;

    // --- Upscaler ---------------------------------------------------------
    // Tiers 1-2 use the single-pass NSR-T so that low-end / integrated GPUs still get AI reconstruction.
    static const UpscalerKind tierUpscaler[kTierCount] = {UpscalerKind::LanczosAR, UpscalerKind::NsrT, UpscalerKind::NsrT, UpscalerKind::NsrS,
                                                          UpscalerKind::NsrS,      UpscalerKind::NsrL, UpscalerKind::NsrL};
    UpscalerKind wanted = tierUpscaler[tier];
    switch (e.upscale) {
    case UpscaleMode::Auto: wanted = tierUpscaler[tier]; break;
    case UpscaleMode::Quality: wanted = UpscalerKind::NsrL; break;
    case UpscaleMode::Balanced: wanted = UpscalerKind::NsrS; break;
    case UpscaleMode::Performance: wanted = UpscalerKind::NsrT; break;
    case UpscaleMode::Native: wanted = UpscalerKind::None; break;
    }
    if (autoMode && e.upscale != UpscaleMode::Auto && e.upscale != UpscaleMode::Native) {
        // Explicit choice is an upper bound; the tier may lower it to hold frame rate.
        UpscalerKind cap = tierUpscaler[tier];
        if (static_cast<int>(wanted) > static_cast<int>(cap)) {
            wanted = cap;
            c.upscalerReduced = true;
        }
    }
    c.upscaler = wanted;

    // --- Stream compression cleanup ----------------------------------------
    c.deband = pick(e.deband, tier >= 4 ? 0.55f : 0.45f);
    c.deblock = tier >= 1 ? pick(e.deblock, tier >= 4 ? 0.55f : 0.45f) : 0.0f;
    c.denoise = tier >= 2 ? pick(e.denoise, tier >= 5 ? 0.45f : 0.35f) : 0.0f;
    c.darkSceneCleanup = tier >= 2 && (e.denoise.enabled || e.deband.enabled);
    c.cleanupHQ = tier >= 5;

    // --- Temporal reconstruction ------------------------------------------
    c.temporal = tier >= 2 ? pick(e.temporal, tier >= 5 ? 0.6f : 0.5f) : 0.0f;

    // --- Deblur -------------------------------------------------------------
    c.deblur = tier >= 2 ? pick(e.deblur, tier >= 5 ? 0.5f : 0.4f) : 0.0f;
    c.motionDeblur = e.motionDeblur && c.deblur > 0 && tier >= 3;

    // --- Sharpen ------------------------------------------------------------
    c.sharpen = pick(e.sharpen, tier >= 4 ? 0.5f : 0.4f);
    c.adaptiveSharpen = tier >= 1;
    c.textBoost = e.textBoost && tier >= 2;
    c.skinProtect = e.skinProtect && tier >= 2;

    // --- Color / HDR+ ------------------------------------------------------
    c.color.enabled = e.colorEnabled;
    c.color.automatic = e.color.automatic;
    c.color.blackLevel = e.color.blackLevel;
    c.color.whiteLevel = e.color.whiteLevel;
    c.color.contrast = e.color.contrast;
    c.color.gamma = e.color.gamma;
    c.color.saturation = e.color.saturation;
    c.color.vibrance = e.color.vibrance;
    c.color.temperature = e.color.temperature;
    c.color.highlightRecovery = e.color.highlightRecovery;
    c.color.shadowDetail = e.color.shadowDetail;
    c.color.localContrast = tier >= 3 ? e.color.localContrast : 0.0f;
    c.color.toneMapping = e.color.toneMapping != TriState::Off;
    // --- Visual style (offsets on top of the user's color settings) ----------
    auto& k = c.color;
    switch (e.style) {
    case VisualStyle::Natural: break;
    case VisualStyle::Vivid:
        k.saturation += 0.30f;
        k.vibrance += 0.20f;
        k.contrast += 0.15f;
        if (tier >= 3) k.localContrast += 0.10f;
        break;
    case VisualStyle::Cinematic:
        k.contrast += 0.12f;
        k.saturation -= 0.08f;
        k.temperature += 0.12f;
        k.highlightRecovery += 0.20f;
        c.splitTone = 0.7f;
        break;
    case VisualStyle::Competitive:
        k.shadowDetail += 0.45f;
        k.saturation += 0.15f;
        if (tier >= 3) k.localContrast += 0.15f;
        c.sharpen = std::min(1.0f, c.sharpen * 1.2f);
        break;
    case VisualStyle::Monochrome:
        k.contrast += 0.10f;
        c.monochrome = 1.0f;
        break;
    }
    if (e.style != VisualStyle::Natural) k.enabled = true; // a style needs the color stage
    k.saturation = std::clamp(k.saturation, -1.0f, 1.0f);
    k.vibrance = std::clamp(k.vibrance, 0.0f, 1.0f);
    k.contrast = std::clamp(k.contrast, -1.0f, 1.0f);
    k.temperature = std::clamp(k.temperature, -1.0f, 1.0f);
    k.highlightRecovery = std::clamp(k.highlightRecovery, 0.0f, 1.0f);
    k.shadowDetail = std::clamp(k.shadowDetail, 0.0f, 1.0f);
    k.localContrast = std::clamp(k.localContrast, 0.0f, 1.0f);
    c.adaptiveCleanup = e.adaptiveCleanup;

    c.hdrMode = e.hdr.mode;
    c.hdrIntensity = e.hdr.intensity;
    c.peakNitsOverride = e.hdr.peakNitsOverride;
    c.paperWhiteOverride = e.hdr.paperWhiteOverride;

    // --- Optical flow / interpolation ---------------------------------------
    c.frameGenMode = e.frameGen;
    int flow = 0;
    // Temporal accumulation always gets motion vectors (without them fast motion would ghost)
    if (tier >= 2 && (c.temporal > 0 || c.motionDeblur)) flow = tier >= 5 ? 2 : 1;
    if (e.frameGen != FrameGenMode::Off) flow = std::max(flow, tier >= 5 ? 2 : 1);
    c.flowQuality = flow;
    return c;
}

void applyAdaptiveCleanup(EffectiveConfig& c, const EnhancementSettings& e, double blockiness) {
    if (!c.adaptiveCleanup || !(blockiness >= 0.0)) return;
    // blockiness ~0.15 is a clean stream, ~0.6+ heavy macroblocking (see quality.hlsl)
    const float t = float(std::clamp((blockiness - 0.2) / 0.45, 0.0, 1.0));
    auto boost = [&](float& v, const Feature& f, float maxValue) {
        if (v > 0.0f && f.enabled && f.automatic) v = std::min(maxValue, v * (1.0f + 0.6f * t));
    };
    boost(c.deblock, e.deblock, 0.9f);
    boost(c.denoise, e.denoise, 0.8f);
    boost(c.deband, e.deband, 0.85f);
    if (t > 0.5f && c.tier >= 4) c.cleanupHQ = true;
}

int batteryTierCap(bool onBattery, bool batterySaver, int batteryMaxTier) {
    if (!onBattery || !batterySaver) return kMaxTier;
    return std::clamp(batteryMaxTier, 0, kMaxTier);
}

namespace {
int modelNumberAfter(const std::string& lowerName, const std::string& token) {
    // e.g. token "rtx " in "nvidia geforce rtx 4070 ti" => 4070
    size_t p = lowerName.find(token);
    if (p == std::string::npos) return -1;
    p += token.size();
    while (p < lowerName.size() && lowerName[p] == ' ') ++p;
    // Skip letter prefixes such as the "a"/"b" in Arc A770 / B580
    int value = 0, digits = 0;
    while (p < lowerName.size() && std::isdigit(static_cast<unsigned char>(lowerName[p]))) {
        value = value * 10 + (lowerName[p] - '0');
        ++p;
        ++digits;
    }
    return digits ? value : -1;
}
} // namespace

std::string gpuFamily(unsigned vendorId, const std::string& gpuName) {
    std::string n = toLowerAscii(gpuName);
    if (vendorId == 0x1414 || n.find("basic render") != std::string::npos || n.find("warp") != std::string::npos) return "Software renderer (WARP)";
    if (vendorId == 0x10DE) {
        int m = modelNumberAfter(n, "rtx ");
        if (m >= 1000) {
            int series = m / 100;
            if (series >= 50 && series < 60) return "NVIDIA GeForce RTX 50 Series";
            if (series >= 40 && series < 50) return "NVIDIA GeForce RTX 40 Series";
            if (series >= 30 && series < 40) return "NVIDIA GeForce RTX 30 Series";
            if (series >= 20 && series < 30) return "NVIDIA GeForce RTX 20 Series";
        }
        if (n.find("rtx") != std::string::npos) return "NVIDIA RTX (Professional)";
        int g = modelNumberAfter(n, "gtx ");
        if (g >= 1600 && g < 1700) return "NVIDIA GeForce GTX 16 Series";
        if (g >= 1000 && g < 1100) return "NVIDIA GeForce GTX 10 Series";
        if (g > 0) return "NVIDIA GeForce GTX";
        return "NVIDIA GPU";
    }
    if (vendorId == 0x1002) {
        int m = modelNumberAfter(n, "rx ");
        if (m >= 9000 && m < 10000) return "AMD Radeon RX 9000 Series";
        if (m >= 7000 && m < 8000) return "AMD Radeon RX 7000 Series";
        if (m >= 6000 && m < 7000) return "AMD Radeon RX 6000 Series";
        if (m >= 5000 && m < 6000) return "AMD Radeon RX 5000 Series";
        if (m >= 400 && m < 600) return "AMD Radeon RX 400/500 Series";
        if (n.find("vega") != std::string::npos) return "AMD Radeon Vega";
        if (n.find("radeon") != std::string::npos && n.find("rx") == std::string::npos) return "AMD Radeon Graphics (integrated)";
        return "AMD Radeon";
    }
    if (vendorId == 0x8086) {
        if (n.find("arc") != std::string::npos) {
            if (n.find(" b5") != std::string::npos || n.find(" b7") != std::string::npos || n.find(" b3") != std::string::npos) return "Intel Arc B-Series";
            if (n.find(" a7") != std::string::npos || n.find(" a5") != std::string::npos || n.find(" a3") != std::string::npos) return "Intel Arc A-Series";
            return "Intel Arc Graphics (integrated)";
        }
        if (n.find("iris") != std::string::npos) return "Intel Iris Xe Graphics";
        return "Intel UHD/HD Graphics";
    }
    return "Unknown GPU";
}

int estimateTierForGpu(unsigned vendorId, const std::string& gpuName, double vramGB) {
    std::string n = toLowerAscii(gpuName);
    if (vendorId == 0x1414 || n.find("basic render") != std::string::npos) return 0;
    if (vendorId == 0x10DE) {
        int m = modelNumberAfter(n, "rtx ");
        if (m >= 1000) {
            int series = m / 100, cls = m % 100;
            if (series >= 40) return cls >= 70 ? 6 : 5;
            if (series >= 30) return cls >= 90 ? 6 : (cls >= 70 ? 5 : 4);
            if (series >= 20) return cls >= 80 ? 5 : 4;
        }
        if (n.find("rtx") != std::string::npos) return 5;
        int g = modelNumberAfter(n, "gtx ");
        if (g >= 1600 && g < 1700) return 3;
        if (g >= 1070 && g < 1100) return 3;
        if (g >= 1060 && g < 1070) return 2;
        return 2; // GTX 10/9 series and older still run NSR-T
    }
    if (vendorId == 0x1002) {
        int m = modelNumberAfter(n, "rx ");
        if (m >= 9000 && m < 10000) return (m % 1000) >= 70 ? 6 : 5;
        if (m >= 7000 && m < 8000) return (m % 1000) >= 700 ? 6 : 5;
        if (m >= 6000 && m < 7000) return (m % 1000) >= 700 ? 5 : 4;
        if (m >= 5000 && m < 6000) return (m % 1000) >= 600 ? 4 : 3;
        if (m >= 400 && m < 600) return 2;
        if (n.find("780m") != std::string::npos || n.find("890m") != std::string::npos || n.find("880m") != std::string::npos) return 3;
        return 2;
    }
    if (vendorId == 0x8086) {
        if (n.find("arc") != std::string::npos) {
            if (n.find("b580") != std::string::npos || n.find("a770") != std::string::npos || n.find("a750") != std::string::npos) return 5;
            if (n.find("b570") != std::string::npos || n.find("a580") != std::string::npos) return 4;
            if (n.find(" a3") != std::string::npos) return 2;
            return 2; // integrated Arc (Meteor/Lunar/Arrow Lake)
        }
        // Integrated graphics start with the single-pass NSR-T (tiers 1-2);
        // Auto Mode drops to tier 0 if even that does not fit the frame budget.
        if (n.find("iris") != std::string::npos) return 2;
        return 1;
    }
    if (vramGB >= 8) return 4;
    if (vramGB >= 4) return 3;
    return 1;
}

bool preferHalfPrecision(unsigned vendorId, const std::string& gpuName) {
    std::string n = toLowerAscii(gpuName);
    if (vendorId == 0x10DE) {
        // Pascal and older GeForce run FP16 at a fraction of FP32 throughput.
        if (n.find("rtx") != std::string::npos) return true;
        int g = modelNumberAfter(n, "gtx ");
        return g >= 1600 && g < 1700; // Turing GTX 16
    }
    if (vendorId == 0x1002 || vendorId == 0x8086) return true;
    return false;
}

} // namespace bgn
