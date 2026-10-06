#pragma once
// User settings model (portable). Serialized to settings.json.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core/Log.h"

namespace bgn {

enum class Preset { Auto, Ultra, Quality, Balanced, Performance, LowLatency };
enum class UpscaleMode { Auto, Quality, Balanced, Performance, Native };
enum class FrameGenMode { Off, Auto, X2 };
enum class OutputMode { Auto, MatchWindow, Fullscreen };
enum class OutputResolution { Auto, Source, R1080p, R1440p, R2160p };
enum class StreamResolution { Auto, Native, R720p, R1080p, R1440p };
enum class TriState { Auto, On, Off };
enum class CaptureBackend { Auto, WindowsGraphicsCapture, DesktopDuplication };
enum class PerformancePriority { Balanced, Quality, Latency };
enum class VisualStyle { Natural, Vivid, Cinematic, Competitive, Monochrome };
enum class ColorVision { Off, Protanopia, Deuteranopia, Tritanopia };
enum class OsdPosition { TopLeft, TopRight, BottomLeft, BottomRight };

const char* toString(Preset v);
const char* toString(UpscaleMode v);
const char* toString(FrameGenMode v);
const char* toString(OutputMode v);
const char* toString(OutputResolution v);
const char* toString(StreamResolution v);
const char* toString(TriState v);
const char* toString(CaptureBackend v);
const char* toString(PerformancePriority v);
const char* toString(VisualStyle v);
const char* toString(ColorVision v);

// A tunable enhancement stage. |automatic| lets Auto Mode pick the strength.
struct Feature {
    bool enabled = true;
    bool automatic = true;
    float strength = 0.5f; // 0..1, used when !automatic
    bool operator==(const Feature&) const = default;
};

struct ColorSettings {
    bool automatic = true;          // Auto color: values below are offsets on top of scene-adaptive analysis
    float blackLevel = 0.0f;        // -0.5..0.5   (+ lifts, - deepens)
    float whiteLevel = 0.0f;        // -0.5..0.5   (+ brightens highlights)
    float contrast = 0.0f;          // -1..1
    float gamma = 1.0f;             // 0.7..1.4
    float saturation = 0.0f;        // -1..1
    float vibrance = 0.15f;         // 0..1
    float temperature = 0.0f;       // -1 (cool) .. 1 (warm)
    float highlightRecovery = 0.3f; // 0..1
    float shadowDetail = 0.25f;     // 0..1
    float localContrast = 0.25f;    // 0..1
    TriState toneMapping = TriState::Auto;
    bool operator==(const ColorSettings&) const = default;
};

struct HdrSettings {
    TriState mode = TriState::Auto; // Auto: HDR+ when the target display has HDR enabled, SDR Enhancement otherwise
    float intensity = 0.5f;         // highlight expansion strength 0..1
    float peakNitsOverride = 0.0f;  // 0 = use display metadata
    float paperWhiteOverride = 0.0f;// 0 = use Windows SDR content brightness
    bool operator==(const HdrSettings&) const = default;
};

struct EnhancementSettings {
    UpscaleMode upscale = UpscaleMode::Auto;
    Feature denoise{true, true, 0.4f};
    Feature deblock{true, true, 0.5f};
    Feature deband{true, true, 0.5f};
    Feature temporal{true, true, 0.5f};
    Feature deblur{true, true, 0.4f};
    Feature sharpen{true, true, 0.5f};
    bool motionDeblur = true;   // directional deblur along estimated motion
    bool textBoost = true;      // extra clarity for text / HUD
    bool skinProtect = true;    // soften sharpening on skin tones
    FrameGenMode frameGen = FrameGenMode::Auto;
    bool colorEnabled = true;
    ColorSettings color;
    HdrSettings hdr;
    VisualStyle style = VisualStyle::Natural; // look applied on top of the color settings
    bool adaptiveCleanup = true;              // raise cleanup when the stream quality monitor sees heavy compression
    bool operator==(const EnhancementSettings&) const = default;
};

// Accessibility filters (global, not per game).
struct AccessibilitySettings {
    ColorVision colorVision = ColorVision::Off; // daltonization (color vision deficiency correction)
    float colorVisionStrength = 1.0f;           // 0..1
    float nightLight = 0.0f;                    // 0..1 blue light reduction
    bool operator==(const AccessibilitySettings&) const = default;
};

// On-screen display drawn into the enhanced picture.
struct OsdSettings {
    bool enabled = false;
    OsdPosition position = OsdPosition::TopRight;
    int scale = 2; // 1..4 (pixel size of the bitmap font)
    bool operator==(const OsdSettings&) const = default;
};

// Laptop battery behaviour.
struct PowerSettings {
    bool batterySaver = true;   // limit GPU work while running on battery
    int batteryMaxTier = 2;     // highest quality tier on battery (0..6)
    bool operator==(const PowerSettings&) const = default;
};

struct GameProfile {
    std::string key;          // normalized key (see GameTitle)
    std::string displayName;  // e.g. "Cyberpunk 2077"
    bool useGlobal = false;   // true: just remembers the game, global settings apply
    bool builtin = false;     // seeded from a built-in template
    Preset preset = Preset::Auto;
    PerformancePriority priority = PerformancePriority::Balanced;
    EnhancementSettings enhancement;
    int64_t lastPlayedUnix = 0;
    int64_t sessions = 0;
};

struct BenchmarkResult {
    bool valid = false;
    std::string gpuName;
    std::string dateUtc;
    std::string inputResolution;
    std::string outputResolution;
    double avgMs = 0.0; // recommended tier average
    double maxMs = 0.0;
    std::vector<double> tierAvgMs; // per tier average GPU time
    std::vector<double> tierMaxMs;
    double frameGenMs = 0.0;
    int recommendedTier = 4;
    Preset recommendedPreset = Preset::Balanced;
    std::string recommendedOutput;
    FrameGenMode recommendedFrameGen = FrameGenMode::Auto;
    std::string backend; // e.g. "D3D11 / Hardware"
};

struct Settings {
    int schemaVersion = 1;

    // Core behaviour
    bool enhancementEnabled = true; // master switch (tray "Pause")
    bool autoMode = true;
    Preset preset = Preset::Auto;
    bool lowLatency = true;
    OutputMode outputMode = OutputMode::Auto;
    OutputResolution outputResolution = OutputResolution::Auto;
    StreamResolution streamResolution = StreamResolution::Auto;
    EnhancementSettings enhancement;

    // Startup / app behaviour
    bool startWithWindows = false;
    bool startInBackground = true; // when launched by Windows at sign-in
    bool closeToTray = true;
    bool autoStart = true;          // start enhancing automatically when a game stream is detected
    bool showNotifications = true;
    bool firstRunCompleted = false;

    // Display / capture
    std::string preferredMonitor;   // empty = monitor containing the GFN window
    CaptureBackend captureBackend = CaptureBackend::Auto;
    bool compareSplit = false;      // left half original, right half enhanced

    // GFN detection
    bool detectBrowser = false;     // also detect play.geforcenow.com in browsers
    std::vector<std::string> extraProcessNames;
    std::string gfnExecutableOverride;

    LogLevel logLevel = LogLevel::Info;
    std::string language = "auto"; // "auto", "en", "ja", "ja+en" (Japanese with English labels)

    AccessibilitySettings accessibility;
    OsdSettings osd;
    PowerSettings power;

    // Screenshots / history
    std::string screenshotFolder;   // empty = Pictures\Better GFN Neural
    bool screenshotComparison = true; // also save original and side-by-side images
    bool recordHistory = true;

    std::map<std::string, GameProfile> profiles;
    std::map<std::string, EnhancementSettings> userPresets; // "My presets" (name -> settings)
    BenchmarkResult benchmark;

    // Clamp every numeric value into its valid range. Returns true if anything changed.
    bool sanitize();
};

std::string settingsToJson(const Settings& s);
// Single enhancement settings block (used for "My presets" export/import).
std::string enhancementToJson(const EnhancementSettings& e);
bool enhancementFromJson(const std::string& json, EnhancementSettings& out);
// Parses JSON into |out|. Missing fields keep their defaults. Returns false on
// malformed JSON (|error| receives a description); |out| is untouched then.
bool settingsFromJson(const std::string& json, Settings& out, std::string* error);

} // namespace bgn
