#include "settings/Settings.h"

#include <algorithm>
#include <cmath>

#include <nlohmann/json.hpp>

namespace bgn {

using nlohmann::json;

NLOHMANN_JSON_SERIALIZE_ENUM(Preset, {{Preset::Auto, "auto"},
                                      {Preset::Ultra, "ultra"},
                                      {Preset::Quality, "quality"},
                                      {Preset::Balanced, "balanced"},
                                      {Preset::Performance, "performance"},
                                      {Preset::LowLatency, "low_latency"}})
NLOHMANN_JSON_SERIALIZE_ENUM(UpscaleMode, {{UpscaleMode::Auto, "auto"},
                                           {UpscaleMode::Quality, "quality"},
                                           {UpscaleMode::Balanced, "balanced"},
                                           {UpscaleMode::Performance, "performance"},
                                           {UpscaleMode::Native, "native"}})
NLOHMANN_JSON_SERIALIZE_ENUM(FrameGenMode, {{FrameGenMode::Auto, "auto"}, {FrameGenMode::Off, "off"}, {FrameGenMode::X2, "2x"}})
NLOHMANN_JSON_SERIALIZE_ENUM(OutputMode, {{OutputMode::Auto, "auto"}, {OutputMode::MatchWindow, "match_window"}, {OutputMode::Fullscreen, "fullscreen"}})
NLOHMANN_JSON_SERIALIZE_ENUM(OutputResolution, {{OutputResolution::Auto, "auto"},
                                                {OutputResolution::Source, "source"},
                                                {OutputResolution::R1080p, "1080p"},
                                                {OutputResolution::R1440p, "1440p"},
                                                {OutputResolution::R2160p, "2160p"}})
NLOHMANN_JSON_SERIALIZE_ENUM(StreamResolution, {{StreamResolution::Auto, "auto"},
                                                {StreamResolution::Native, "native"},
                                                {StreamResolution::R720p, "720p"},
                                                {StreamResolution::R1080p, "1080p"},
                                                {StreamResolution::R1440p, "1440p"}})
NLOHMANN_JSON_SERIALIZE_ENUM(TriState, {{TriState::Auto, "auto"}, {TriState::On, "on"}, {TriState::Off, "off"}})
NLOHMANN_JSON_SERIALIZE_ENUM(CaptureBackend, {{CaptureBackend::Auto, "auto"},
                                              {CaptureBackend::WindowsGraphicsCapture, "wgc"},
                                              {CaptureBackend::DesktopDuplication, "dxgi_duplication"}})
NLOHMANN_JSON_SERIALIZE_ENUM(PerformancePriority, {{PerformancePriority::Balanced, "balanced"},
                                                   {PerformancePriority::Quality, "quality"},
                                                   {PerformancePriority::Latency, "latency"}})
NLOHMANN_JSON_SERIALIZE_ENUM(LogLevel, {{LogLevel::Info, "info"}, {LogLevel::Debug, "debug"}, {LogLevel::Warning, "warning"}, {LogLevel::Error, "error"}})

const char* toString(Preset v) {
    switch (v) {
    case Preset::Auto: return "Auto";
    case Preset::Ultra: return "Ultra";
    case Preset::Quality: return "Quality";
    case Preset::Balanced: return "Balanced";
    case Preset::Performance: return "Performance";
    case Preset::LowLatency: return "Low Latency";
    }
    return "?";
}
const char* toString(UpscaleMode v) {
    switch (v) {
    case UpscaleMode::Auto: return "Auto";
    case UpscaleMode::Quality: return "Quality";
    case UpscaleMode::Balanced: return "Balanced";
    case UpscaleMode::Performance: return "Performance";
    case UpscaleMode::Native: return "Native";
    }
    return "?";
}
const char* toString(FrameGenMode v) {
    switch (v) {
    case FrameGenMode::Off: return "Off";
    case FrameGenMode::Auto: return "Auto";
    case FrameGenMode::X2: return "2x";
    }
    return "?";
}
const char* toString(OutputMode v) {
    switch (v) {
    case OutputMode::Auto: return "Auto";
    case OutputMode::MatchWindow: return "Match GFN window";
    case OutputMode::Fullscreen: return "Fullscreen";
    }
    return "?";
}
const char* toString(OutputResolution v) {
    switch (v) {
    case OutputResolution::Auto: return "Auto (monitor)";
    case OutputResolution::Source: return "Same as source";
    case OutputResolution::R1080p: return "1920x1080";
    case OutputResolution::R1440p: return "2560x1440";
    case OutputResolution::R2160p: return "3840x2160";
    }
    return "?";
}
const char* toString(StreamResolution v) {
    switch (v) {
    case StreamResolution::Auto: return "Auto detect";
    case StreamResolution::Native: return "Native (no in-place upscaling)";
    case StreamResolution::R720p: return "720p";
    case StreamResolution::R1080p: return "1080p";
    case StreamResolution::R1440p: return "1440p";
    }
    return "?";
}
const char* toString(TriState v) {
    switch (v) {
    case TriState::Auto: return "Auto";
    case TriState::On: return "On";
    case TriState::Off: return "Off";
    }
    return "?";
}
const char* toString(CaptureBackend v) {
    switch (v) {
    case CaptureBackend::Auto: return "Auto";
    case CaptureBackend::WindowsGraphicsCapture: return "Windows Graphics Capture";
    case CaptureBackend::DesktopDuplication: return "DXGI Desktop Duplication";
    }
    return "?";
}
const char* toString(PerformancePriority v) {
    switch (v) {
    case PerformancePriority::Balanced: return "Balanced";
    case PerformancePriority::Quality: return "Quality first";
    case PerformancePriority::Latency: return "Latency first";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// JSON mapping (tolerant: every field optional)
// ---------------------------------------------------------------------------
template <typename T>
static void getOpt(const json& j, const char* key, T& v) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return;
    try {
        v = it->get<T>();
    } catch (const std::exception&) {
        // keep default on type mismatch
    }
}

static json toJson(const Feature& f) { return json{{"enabled", f.enabled}, {"auto", f.automatic}, {"strength", f.strength}}; }
static void fromJson(const json& j, Feature& f) {
    if (!j.is_object()) return;
    getOpt(j, "enabled", f.enabled);
    getOpt(j, "auto", f.automatic);
    getOpt(j, "strength", f.strength);
}

static json toJson(const ColorSettings& c) {
    return json{{"auto", c.automatic},           {"black_level", c.blackLevel},
                {"white_level", c.whiteLevel},   {"contrast", c.contrast},
                {"gamma", c.gamma},              {"saturation", c.saturation},
                {"vibrance", c.vibrance},        {"temperature", c.temperature},
                {"highlight_recovery", c.highlightRecovery},
                {"shadow_detail", c.shadowDetail},
                {"local_contrast", c.localContrast},
                {"tone_mapping", c.toneMapping}};
}
static void fromJson(const json& j, ColorSettings& c) {
    if (!j.is_object()) return;
    getOpt(j, "auto", c.automatic);
    getOpt(j, "black_level", c.blackLevel);
    getOpt(j, "white_level", c.whiteLevel);
    getOpt(j, "contrast", c.contrast);
    getOpt(j, "gamma", c.gamma);
    getOpt(j, "saturation", c.saturation);
    getOpt(j, "vibrance", c.vibrance);
    getOpt(j, "temperature", c.temperature);
    getOpt(j, "highlight_recovery", c.highlightRecovery);
    getOpt(j, "shadow_detail", c.shadowDetail);
    getOpt(j, "local_contrast", c.localContrast);
    getOpt(j, "tone_mapping", c.toneMapping);
}

static json toJson(const HdrSettings& h) {
    return json{{"mode", h.mode}, {"intensity", h.intensity}, {"peak_nits_override", h.peakNitsOverride}, {"paper_white_override", h.paperWhiteOverride}};
}
static void fromJson(const json& j, HdrSettings& h) {
    if (!j.is_object()) return;
    getOpt(j, "mode", h.mode);
    getOpt(j, "intensity", h.intensity);
    getOpt(j, "peak_nits_override", h.peakNitsOverride);
    getOpt(j, "paper_white_override", h.paperWhiteOverride);
}

static json toJson(const EnhancementSettings& e) {
    return json{{"upscale", e.upscale},
                {"denoise", toJson(e.denoise)},
                {"deblock", toJson(e.deblock)},
                {"deband", toJson(e.deband)},
                {"temporal", toJson(e.temporal)},
                {"deblur", toJson(e.deblur)},
                {"sharpen", toJson(e.sharpen)},
                {"motion_deblur", e.motionDeblur},
                {"text_boost", e.textBoost},
                {"skin_protect", e.skinProtect},
                {"frame_interpolation", e.frameGen},
                {"color_enabled", e.colorEnabled},
                {"color", toJson(e.color)},
                {"hdr", toJson(e.hdr)}};
}
static void fromJson(const json& j, EnhancementSettings& e) {
    if (!j.is_object()) return;
    getOpt(j, "upscale", e.upscale);
    if (j.contains("denoise")) fromJson(j["denoise"], e.denoise);
    if (j.contains("deblock")) fromJson(j["deblock"], e.deblock);
    if (j.contains("deband")) fromJson(j["deband"], e.deband);
    if (j.contains("temporal")) fromJson(j["temporal"], e.temporal);
    if (j.contains("deblur")) fromJson(j["deblur"], e.deblur);
    if (j.contains("sharpen")) fromJson(j["sharpen"], e.sharpen);
    getOpt(j, "motion_deblur", e.motionDeblur);
    getOpt(j, "text_boost", e.textBoost);
    getOpt(j, "skin_protect", e.skinProtect);
    getOpt(j, "frame_interpolation", e.frameGen);
    getOpt(j, "color_enabled", e.colorEnabled);
    if (j.contains("color")) fromJson(j["color"], e.color);
    if (j.contains("hdr")) fromJson(j["hdr"], e.hdr);
}

static json toJson(const GameProfile& p) {
    return json{{"key", p.key},
                {"name", p.displayName},
                {"use_global", p.useGlobal},
                {"builtin", p.builtin},
                {"preset", p.preset},
                {"performance", p.priority},
                {"enhancement", toJson(p.enhancement)},
                {"last_played", p.lastPlayedUnix},
                {"sessions", p.sessions}};
}
static void fromJson(const json& j, GameProfile& p) {
    if (!j.is_object()) return;
    getOpt(j, "key", p.key);
    getOpt(j, "name", p.displayName);
    getOpt(j, "use_global", p.useGlobal);
    getOpt(j, "builtin", p.builtin);
    getOpt(j, "preset", p.preset);
    getOpt(j, "performance", p.priority);
    if (j.contains("enhancement")) fromJson(j["enhancement"], p.enhancement);
    getOpt(j, "last_played", p.lastPlayedUnix);
    getOpt(j, "sessions", p.sessions);
}

static json toJson(const BenchmarkResult& b) {
    return json{{"valid", b.valid},
                {"gpu", b.gpuName},
                {"date_utc", b.dateUtc},
                {"input", b.inputResolution},
                {"output", b.outputResolution},
                {"avg_ms", b.avgMs},
                {"max_ms", b.maxMs},
                {"tier_avg_ms", b.tierAvgMs},
                {"tier_max_ms", b.tierMaxMs},
                {"frame_interpolation_ms", b.frameGenMs},
                {"recommended_tier", b.recommendedTier},
                {"recommended_preset", b.recommendedPreset},
                {"recommended_output", b.recommendedOutput},
                {"recommended_frame_interpolation", b.recommendedFrameGen},
                {"backend", b.backend}};
}
static void fromJson(const json& j, BenchmarkResult& b) {
    if (!j.is_object()) return;
    getOpt(j, "valid", b.valid);
    getOpt(j, "gpu", b.gpuName);
    getOpt(j, "date_utc", b.dateUtc);
    getOpt(j, "input", b.inputResolution);
    getOpt(j, "output", b.outputResolution);
    getOpt(j, "avg_ms", b.avgMs);
    getOpt(j, "max_ms", b.maxMs);
    getOpt(j, "tier_avg_ms", b.tierAvgMs);
    getOpt(j, "tier_max_ms", b.tierMaxMs);
    getOpt(j, "frame_interpolation_ms", b.frameGenMs);
    getOpt(j, "recommended_tier", b.recommendedTier);
    getOpt(j, "recommended_preset", b.recommendedPreset);
    getOpt(j, "recommended_output", b.recommendedOutput);
    getOpt(j, "recommended_frame_interpolation", b.recommendedFrameGen);
    getOpt(j, "backend", b.backend);
}

std::string settingsToJson(const Settings& s) {
    json profiles = json::object();
    for (const auto& [key, p] : s.profiles) profiles[key] = toJson(p);
    json j{{"schema_version", s.schemaVersion},
           {"enhancement_enabled", s.enhancementEnabled},
           {"auto_mode", s.autoMode},
           {"preset", s.preset},
           {"low_latency", s.lowLatency},
           {"output_mode", s.outputMode},
           {"output_resolution", s.outputResolution},
           {"stream_resolution", s.streamResolution},
           {"enhancement", toJson(s.enhancement)},
           {"startup",
            {{"start_with_windows", s.startWithWindows},
             {"start_in_background", s.startInBackground},
             {"close_to_tray", s.closeToTray},
             {"auto_start", s.autoStart},
             {"show_notifications", s.showNotifications}}},
           {"first_run_completed", s.firstRunCompleted},
           {"preferred_monitor", s.preferredMonitor},
           {"capture_backend", s.captureBackend},
           {"compare_split", s.compareSplit},
           {"gfn",
            {{"detect_browser", s.detectBrowser},
             {"extra_process_names", s.extraProcessNames},
             {"executable_override", s.gfnExecutableOverride}}},
           {"log_level", s.logLevel},
           {"language", s.language},
           {"game_profiles", profiles},
           {"benchmark", toJson(s.benchmark)}};
    return j.dump(2);
}

bool settingsFromJson(const std::string& text, Settings& out, std::string* error) {
    json j;
    try {
        j = json::parse(text);
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
    if (!j.is_object()) {
        if (error) *error = "root is not an object";
        return false;
    }
    Settings s;
    getOpt(j, "schema_version", s.schemaVersion);
    getOpt(j, "enhancement_enabled", s.enhancementEnabled);
    getOpt(j, "auto_mode", s.autoMode);
    getOpt(j, "preset", s.preset);
    getOpt(j, "low_latency", s.lowLatency);
    getOpt(j, "output_mode", s.outputMode);
    getOpt(j, "output_resolution", s.outputResolution);
    getOpt(j, "stream_resolution", s.streamResolution);
    if (j.contains("enhancement")) fromJson(j["enhancement"], s.enhancement);
    if (j.contains("startup") && j["startup"].is_object()) {
        const json& st = j["startup"];
        getOpt(st, "start_with_windows", s.startWithWindows);
        getOpt(st, "start_in_background", s.startInBackground);
        getOpt(st, "close_to_tray", s.closeToTray);
        getOpt(st, "auto_start", s.autoStart);
        getOpt(st, "show_notifications", s.showNotifications);
    }
    getOpt(j, "first_run_completed", s.firstRunCompleted);
    getOpt(j, "preferred_monitor", s.preferredMonitor);
    getOpt(j, "capture_backend", s.captureBackend);
    getOpt(j, "compare_split", s.compareSplit);
    if (j.contains("gfn") && j["gfn"].is_object()) {
        const json& g = j["gfn"];
        getOpt(g, "detect_browser", s.detectBrowser);
        getOpt(g, "extra_process_names", s.extraProcessNames);
        getOpt(g, "executable_override", s.gfnExecutableOverride);
    }
    getOpt(j, "log_level", s.logLevel);
    getOpt(j, "language", s.language);
    if (j.contains("game_profiles") && j["game_profiles"].is_object()) {
        for (auto it = j["game_profiles"].begin(); it != j["game_profiles"].end(); ++it) {
            GameProfile p;
            fromJson(it.value(), p);
            if (p.key.empty()) p.key = it.key();
            if (p.displayName.empty()) p.displayName = p.key;
            s.profiles[p.key] = p;
        }
    }
    if (j.contains("benchmark")) fromJson(j["benchmark"], s.benchmark);
    s.sanitize();
    out = std::move(s);
    return true;
}

// ---------------------------------------------------------------------------
// Sanitizing
// ---------------------------------------------------------------------------
static bool clampf(float& v, float lo, float hi, float fallback) {
    float old = v;
    if (!std::isfinite(v)) v = fallback;
    v = std::clamp(v, lo, hi);
    return old != v;
}

static bool sanitizeEnhancement(EnhancementSettings& e) {
    bool c = false;
    for (Feature* f : {&e.denoise, &e.deblock, &e.deband, &e.temporal, &e.deblur, &e.sharpen}) c |= clampf(f->strength, 0.0f, 1.0f, 0.5f);
    auto& k = e.color;
    c |= clampf(k.blackLevel, -0.5f, 0.5f, 0.0f);
    c |= clampf(k.whiteLevel, -0.5f, 0.5f, 0.0f);
    c |= clampf(k.contrast, -1.0f, 1.0f, 0.0f);
    c |= clampf(k.gamma, 0.7f, 1.4f, 1.0f);
    c |= clampf(k.saturation, -1.0f, 1.0f, 0.0f);
    c |= clampf(k.vibrance, 0.0f, 1.0f, 0.15f);
    c |= clampf(k.temperature, -1.0f, 1.0f, 0.0f);
    c |= clampf(k.highlightRecovery, 0.0f, 1.0f, 0.3f);
    c |= clampf(k.shadowDetail, 0.0f, 1.0f, 0.25f);
    c |= clampf(k.localContrast, 0.0f, 1.0f, 0.25f);
    c |= clampf(e.hdr.intensity, 0.0f, 1.0f, 0.5f);
    c |= clampf(e.hdr.peakNitsOverride, 0.0f, 10000.0f, 0.0f);
    c |= clampf(e.hdr.paperWhiteOverride, 0.0f, 1000.0f, 0.0f);
    return c;
}

bool Settings::sanitize() {
    bool changed = sanitizeEnhancement(enhancement);
    for (auto& [key, p] : profiles) changed |= sanitizeEnhancement(p.enhancement);
    // drop profiles with empty keys
    for (auto it = profiles.begin(); it != profiles.end();) {
        if (it->first.empty()) {
            it = profiles.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (language != "auto" && language != "en" && language != "ja" && language != "ja+en") {
        language = "auto";
        changed = true;
    }
    if (extraProcessNames.size() > 32) {
        extraProcessNames.resize(32);
        changed = true;
    }
    return changed;
}

} // namespace bgn
