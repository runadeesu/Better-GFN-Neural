// Unit tests for the portable core (runs on Windows CI and on Linux dev boxes).

#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

#include "../TestFramework.h"
#include "app/CommandLine.h"
#include "automode/AutoModeController.h"
#include "core/Log.h"
#include "core/StringUtil.h"
#include "filters/ContentResolution.h"
#include "framegen/FramePacing.h"
#include "neural/NsrReference.h"
#include "gfn/GfnClassifier.h"
#include "profiles/GameTitle.h"
#include "profiles/ProfileManager.h"
#include "settings/Presets.h"
#include "settings/Settings.h"
#include "settings/SettingsStore.h"
#include "telemetry/RollingStats.h"
#include "ui/I18n.h"

using namespace bgn;
namespace fs = std::filesystem;

static fs::path tempDir(const char* name) {
    fs::path p = fs::temp_directory_path() / (std::string("bgn_test_") + name);
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p);
    return p;
}

// ---------------------------------------------------------------------------
TEST_CASE("utf8_utf16_roundtrip") {
    std::string s = "Cyberpunk 2077\xC2\xAE \xE3\x83\x9F\xE3\x83\x83\xE3\x82\xB7\xE3\x83\xA7\xE3\x83\xB3 \xF0\x9F\x8E\xAE";
    std::u16string w = utf8ToUtf16(s);
    CHECK_EQ(utf16ToUtf8(w), s);
    CHECK_EQ(utf8ToUtf16("A").size(), size_t(1));
    CHECK_EQ(utf8ToUtf16("\xF0\x9F\x8E\xAE").size(), size_t(2)); // surrogate pair
}

TEST_CASE("string_helpers") {
    CHECK_EQ(trim("  a b  "), std::string("a b"));
    CHECK(iequalsAscii("GeForceNOW.exe", "geforcenow.EXE"));
    CHECK(icontainsAscii("Fortnite on GeForce NOW", "geforce now"));
    CHECK_EQ(replaceAll("a-b-c", "-", "+"), std::string("a+b+c"));
    CHECK_EQ(split("a,b,,c", ',').size(), size_t(4));
}

// ---------------------------------------------------------------------------
TEST_CASE("game_title_cleanup") {
    CHECK_EQ(cleanGameTitle("Cyberpunk 2077\xC2\xAE on GeForce NOW"), std::string("Cyberpunk 2077"));
    CHECK_EQ(cleanGameTitle("Fortnite\xC2\xAE on GeForce NOW"), std::string("Fortnite"));
    CHECK_EQ(cleanGameTitle("Call of Duty\xC2\xAE: Black Ops 6 - GeForce NOW"), std::string("Call of Duty: Black Ops 6"));
    CHECK_EQ(cleanGameTitle("Apex Legends\xE2\x84\xA2 on GeForce NOW - Google Chrome"), std::string("Apex Legends"));
    CHECK_EQ(cleanGameTitle("  Minecraft   Java\xC2\xA0" "Edition  "), std::string("Minecraft Java Edition"));
    CHECK(isLauncherTitle("GeForce NOW"));
    CHECK(isLauncherTitle("NVIDIA GeForce NOW"));
    CHECK(!isLauncherTitle("Forza Horizon 5 on GeForce NOW"));
    CHECK_EQ(gameKey("Forza Horizon 5"), std::string("forzahorizon5"));
    CHECK_EQ(gameKey("Baldur's Gate 3"), std::string("baldursgate3"));
}

// ---------------------------------------------------------------------------
static WindowSnapshot win(const char* proc, const char* title, int w, int h, bool fg = false) {
    WindowSnapshot s;
    s.handle = 0x1000 + w;
    s.pid = 42;
    s.processName = proc;
    s.title = title;
    s.width = s.clientWidth = w;
    s.height = s.clientHeight = h;
    s.monitorWidth = 1920;
    s.monitorHeight = 1080;
    s.visible = true;
    s.foreground = fg;
    return s;
}

TEST_CASE("gfn_classifier_states") {
    DetectionRules rules;
    CHECK(classifyGfn({}, rules).state == GfnState::NotRunning);
    CHECK(classifyGfn({win("notepad.exe", "Untitled - Notepad", 800, 600)}, rules).state == GfnState::NotRunning);

    auto launcher = classifyGfn({win("geforcenow.exe", "GeForce NOW", 1280, 720)}, rules);
    CHECK(launcher.state == GfnState::Connected);

    auto stream = classifyGfn({win("geforcenow.exe", "GeForce NOW", 1280, 720), win("geforcenow.exe", "Cyberpunk 2077\xC2\xAE on GeForce NOW", 1920, 1080, true)}, rules);
    CHECK(stream.state == GfnState::Streaming);
    CHECK_EQ(stream.gameName, std::string("Cyberpunk 2077"));
    CHECK_EQ(stream.streamIndex, 1);

    // Tiny windows are not streams
    auto tiny = classifyGfn({win("geforcenow.exe", "Some Popup", 300, 200)}, rules);
    CHECK(tiny.state == GfnState::Connected);

    // Fullscreen launcher-titled window counts as an unnamed stream
    auto fs = classifyGfn({win("geforcenow.exe", "GeForce NOW", 1920, 1080)}, rules);
    CHECK(fs.state == GfnState::Streaming);
    CHECK(fs.gameName.empty());
    rules.fullscreenHeuristic = false;
    CHECK(classifyGfn({win("geforcenow.exe", "GeForce NOW", 1920, 1080)}, rules).state == GfnState::Connected);
}

TEST_CASE("gfn_classifier_browser") {
    DetectionRules rules;
    auto w = win("chrome.exe", "Fortnite on GeForce NOW - Google Chrome", 1920, 1080);
    CHECK(classifyGfn({w}, rules).state == GfnState::NotRunning);
    rules.detectBrowser = true;
    auto r = classifyGfn({w}, rules);
    CHECK(r.state == GfnState::Streaming);
    CHECK(r.fromBrowser);
    CHECK_EQ(r.gameName, std::string("Fortnite"));
}

TEST_CASE("gfn_classifier_hidden_windows_ignored") {
    DetectionRules rules;
    auto w = win("geforcenow.exe", "Apex Legends on GeForce NOW", 1920, 1080);
    w.visible = false;
    auto r = classifyGfn({w}, rules);
    CHECK(r.state == GfnState::Connected); // process is running, but no visible stream
}

// ---------------------------------------------------------------------------
TEST_CASE("settings_json_roundtrip") {
    Settings s;
    s.preset = Preset::Ultra;
    s.autoMode = false;
    s.enhancement.sharpen = {true, false, 0.77f};
    s.enhancement.frameGen = FrameGenMode::X2;
    s.enhancement.color.temperature = -0.3f;
    s.startWithWindows = true;
    s.preferredMonitor = "\\\\.\\DISPLAY2";
    GameProfile p;
    p.key = "fortnite";
    p.displayName = "Fortnite";
    p.preset = Preset::LowLatency;
    s.profiles[p.key] = p;
    s.benchmark.valid = true;
    s.benchmark.tierAvgMs = {0.5, 0.7, 1.0};
    std::string json = settingsToJson(s);
    Settings r;
    std::string err;
    CHECK(settingsFromJson(json, r, &err));
    CHECK(r.preset == Preset::Ultra);
    CHECK(!r.autoMode);
    CHECK(r.enhancement.sharpen == s.enhancement.sharpen);
    CHECK(r.enhancement.frameGen == FrameGenMode::X2);
    CHECK_NEAR(r.enhancement.color.temperature, -0.3, 1e-6);
    CHECK(r.startWithWindows);
    CHECK_EQ(r.preferredMonitor, s.preferredMonitor);
    CHECK_EQ(r.profiles.size(), size_t(1));
    CHECK(r.profiles["fortnite"].preset == Preset::LowLatency);
    CHECK_EQ(r.benchmark.tierAvgMs.size(), size_t(3));
    CHECK(r.enhancement == s.enhancement);
}

TEST_CASE("settings_json_tolerant") {
    Settings r;
    std::string err;
    CHECK(!settingsFromJson("{ not json", r, &err));
    CHECK(!err.empty());
    CHECK(settingsFromJson("{\"preset\":\"no_such_preset\",\"auto_mode\":\"yes\",\"enhancement\":{\"sharpen\":{\"strength\":7}}}", r, &err));
    CHECK(r.preset == Preset::Auto);   // unknown enum => first entry
    CHECK(r.autoMode);                 // wrong type => default kept
    CHECK_NEAR(r.enhancement.sharpen.strength, 1.0, 1e-6); // clamped
}

TEST_CASE("settings_store_backup_recovery") {
    fs::path dir = tempDir("store");
    SettingsStore store(dir / "settings.json");
    Settings s, r;
    auto res = store.load(r);
    CHECK(res.source == SettingsLoadSource::Defaults);
    s.preset = Preset::Quality;
    CHECK(store.save(s));
    s.preset = Preset::Performance;
    CHECK(store.save(s)); // previous good file rotated to .bak
    CHECK(fs::exists(store.backupPath()));
    res = store.load(r);
    CHECK(res.source == SettingsLoadSource::Primary);
    CHECK(r.preset == Preset::Performance);
    // Corrupt the primary file => backup is used
    {
        std::ofstream f(store.path(), std::ios::trunc);
        f << "{ corrupted";
    }
    res = store.load(r);
    CHECK(res.source == SettingsLoadSource::Backup);
    CHECK(res.primaryCorrupt);
    CHECK(r.preset == Preset::Quality);
    // Corrupt both => defaults
    {
        std::ofstream f(store.backupPath(), std::ios::trunc);
        f << "garbage";
    }
    res = store.load(r);
    CHECK(res.source == SettingsLoadSource::Defaults);
    CHECK(r.preset == Preset::Auto);
}

TEST_CASE("session_guard_crash_detection") {
    fs::path dir = tempDir("guard");
    {
        SessionGuard g(dir);
        CHECK_EQ(g.begin(), 0);
        g.endClean();
    }
    {
        SessionGuard g(dir);
        CHECK_EQ(g.begin(), 0);
        // simulate crash: no endClean
    }
    {
        SessionGuard g(dir);
        CHECK_EQ(g.begin(), 1);
    }
    {
        SessionGuard g(dir);
        CHECK_EQ(g.begin(), 2); // two consecutive crashes => safe mode threshold
        g.endClean();
    }
    {
        SessionGuard g(dir);
        CHECK_EQ(g.begin(), 0);
        g.endClean();
    }
}

// ---------------------------------------------------------------------------
TEST_CASE("presets_resolve_tiers") {
    EnhancementSettings e;
    auto t0 = resolveConfig(e, 0, true);
    CHECK(t0.upscaler == UpscalerKind::LanczosAR);
    CHECK(t0.temporal == 0.0f);
    CHECK(!t0.needsFlow() || t0.frameGenMode != FrameGenMode::Off);
    CHECK(t0.sharpen > 0.0f); // even the minimal tier still enhances
    CHECK(t0.deband > 0.0f);
    auto t4 = resolveConfig(e, 4, true);
    CHECK(t4.upscaler == UpscalerKind::NsrS);
    CHECK(t4.temporal > 0.0f);
    CHECK(t4.motionDeblur);
    CHECK(t4.needsFlow());
    auto t6 = resolveConfig(e, 6, true);
    CHECK(t6.upscaler == UpscalerKind::NsrL);
    CHECK_EQ(t6.flowQuality, 2);
    CHECK(t6.cleanupHQ);

    e.upscale = UpscaleMode::Quality;
    auto reduced = resolveConfig(e, 2, true);
    CHECK(reduced.upscaler == UpscalerKind::LanczosAR);
    CHECK(reduced.upscalerReduced);
    auto manual = resolveConfig(e, 2, false);
    CHECK(manual.upscaler == UpscalerKind::NsrL);
    e.upscale = UpscaleMode::Native;
    CHECK(resolveConfig(e, 6, true).upscaler == UpscalerKind::None);

    e = EnhancementSettings{};
    e.deblur.enabled = false;
    e.frameGen = FrameGenMode::Off;
    e.temporal.enabled = false;
    auto noFlow = resolveConfig(e, 6, true);
    CHECK(noFlow.deblur == 0.0f);
    CHECK(!noFlow.needsFlow());
    e.sharpen = {true, false, 0.9f};
    CHECK_NEAR(resolveConfig(e, 3, true).sharpen, 0.9, 1e-6);
}

TEST_CASE("preset_policies") {
    CHECK_EQ(policyFor(Preset::Ultra, false).startTier, kMaxTier);
    CHECK_EQ(policyFor(Preset::Performance, false).maxTier, 2);
    CHECK(!policyFor(Preset::LowLatency, true).allowFrameGen);
    CHECK(policyFor(Preset::Auto, true).latencyBudgetMs <= 12.0);
    CHECK(policyFor(Preset::Balanced, false, PerformancePriority::Latency).gpuBudgetFraction <= 0.35);
}

TEST_CASE("gpu_classification") {
    CHECK_EQ(gpuFamily(0x10DE, "NVIDIA GeForce RTX 4070 Ti"), std::string("NVIDIA GeForce RTX 40 Series"));
    CHECK_EQ(gpuFamily(0x10DE, "NVIDIA GeForce RTX 5090"), std::string("NVIDIA GeForce RTX 50 Series"));
    CHECK_EQ(gpuFamily(0x10DE, "NVIDIA GeForce RTX 2060 SUPER"), std::string("NVIDIA GeForce RTX 20 Series"));
    CHECK_EQ(gpuFamily(0x10DE, "NVIDIA GeForce GTX 1660 Ti"), std::string("NVIDIA GeForce GTX 16 Series"));
    CHECK_EQ(gpuFamily(0x10DE, "NVIDIA GeForce GTX 1060 6GB"), std::string("NVIDIA GeForce GTX 10 Series"));
    CHECK_EQ(gpuFamily(0x1002, "AMD Radeon RX 7800 XT"), std::string("AMD Radeon RX 7000 Series"));
    CHECK_EQ(gpuFamily(0x1002, "AMD Radeon RX 9070 XT"), std::string("AMD Radeon RX 9000 Series"));
    CHECK_EQ(gpuFamily(0x8086, "Intel(R) Arc(TM) B580 Graphics"), std::string("Intel Arc B-Series"));
    CHECK_EQ(gpuFamily(0x8086, "Intel(R) Arc(TM) A770 Graphics"), std::string("Intel Arc A-Series"));
    CHECK_EQ(gpuFamily(0x1414, "Microsoft Basic Render Driver"), std::string("Software renderer (WARP)"));
    CHECK_EQ(estimateTierForGpu(0x10DE, "NVIDIA GeForce RTX 4090", 24), 6);
    CHECK_EQ(estimateTierForGpu(0x10DE, "NVIDIA GeForce RTX 3060", 12), 4);
    CHECK_EQ(estimateTierForGpu(0x10DE, "NVIDIA GeForce GTX 1060 6GB", 6), 2);
    CHECK_EQ(estimateTierForGpu(0x1414, "Microsoft Basic Render Driver", 0), 0);
    CHECK(preferHalfPrecision(0x10DE, "NVIDIA GeForce RTX 3080"));
    CHECK(!preferHalfPrecision(0x10DE, "NVIDIA GeForce GTX 1080"));
    CHECK(preferHalfPrecision(0x1002, "AMD Radeon RX 6700 XT"));
}

// ---------------------------------------------------------------------------
TEST_CASE("profiles_builtin_and_aliases") {
    Settings s;
    std::string k = ProfileManager::onGameDetected(s, "Cyberpunk 2077", 1000);
    CHECK_EQ(k, std::string("cyberpunk2077"));
    CHECK(s.profiles[k].builtin);
    CHECK(s.profiles[k].preset == Preset::Quality);
    CHECK_EQ(s.profiles[k].sessions, int64_t(1));
    std::string cod = ProfileManager::onGameDetected(s, "Call of Duty: Black Ops 6", 1001);
    CHECK_EQ(cod, std::string("callofduty"));
    CHECK(s.profiles[cod].enhancement.frameGen == FrameGenMode::Off);
    // Same franchise later maps to the same profile
    CHECK_EQ(ProfileManager::onGameDetected(s, "Call of Duty: Warzone", 1002), std::string("callofduty"));
    CHECK_EQ(s.profiles["callofduty"].sessions, int64_t(2));
    // Forza Horizon 5 -> forzahorizon template
    CHECK_EQ(ProfileManager::onGameDetected(s, "Forza Horizon 5", 1003), std::string("forzahorizon"));

    std::string unk = ProfileManager::onGameDetected(s, "Some Indie Game", 1004);
    CHECK_EQ(unk, std::string("someindiegame"));
    CHECK(s.profiles[unk].useGlobal);
    s.enhancement.sharpen.strength = 0.11f;
    auto r = ProfileManager::resolve(s, unk);
    CHECK(!r.fromProfile);
    CHECK_NEAR(r.enhancement.sharpen.strength, 0.11, 1e-6);
    auto rc = ProfileManager::resolve(s, "cyberpunk2077");
    CHECK(rc.fromProfile);
    CHECK(rc.preset == Preset::Quality);
    CHECK(ProfileManager::builtinNames().size() >= 6);
}

// ---------------------------------------------------------------------------
// Simulated GPU: cost(tier) = base * relative cost
static double simCost(int tier, double base) {
    static const double rel[kTierCount] = {1.0, 1.35, 2.1, 3.3, 4.1, 7.2, 8.6};
    return base * rel[tier];
}

TEST_CASE("automode_downgrades_on_slow_gpu") {
    AutoModeController c;
    c.configure(policyFor(Preset::Ultra, false), FrameGenMode::Off, true, kMaxTier);
    // 60 fps input, budget 0.65*16.67 = 10.8ms. base 2ms => tier6 = 17.2ms (over), tier4 = 8.2 fits.
    double t = 0;
    for (int i = 0; i < 60; ++i, t += 0.5) {
        AutoInputs in;
        in.nowSeconds = t;
        in.inputFps = 60;
        in.refreshHz = 60;
        in.gpuMsAvg = in.gpuMsP95 = simCost(c.tier(), 2.0);
        c.update(in);
    }
    CHECK(c.tier() <= 4);
    CHECK(c.tier() >= 3);
    CHECK(simCost(c.tier(), 2.0) <= AutoModeController::gpuBudgetMs(60, policyFor(Preset::Ultra, false).gpuBudgetFraction));
}

TEST_CASE("automode_upgrades_with_headroom_and_is_stable") {
    AutoModeController c;
    c.configure(policyFor(Preset::Auto, false), FrameGenMode::Off, true, 1);
    double t = 0;
    int changes = 0;
    for (int i = 0; i < 400; ++i, t += 0.5) { // 200 simulated seconds
        AutoInputs in;
        in.nowSeconds = t;
        in.inputFps = 60;
        in.refreshHz = 60;
        in.gpuMsAvg = in.gpuMsP95 = simCost(c.tier(), 1.4); // tier5 = 10.1 > 8.33 budget, tier4 = 5.74 fits
        auto d = c.update(in);
        if (d.changed) ++changes;
    }
    CHECK_EQ(c.tier(), 4);
    CHECK(changes <= 6); // climbs 1->4 then stays (no oscillation)
}

TEST_CASE("automode_minimum_tier_still_enhances") {
    AutoModeController c;
    c.configure(policyFor(Preset::Auto, false), FrameGenMode::Off, true, 4);
    double t = 0;
    for (int i = 0; i < 100; ++i, t += 0.5) {
        AutoInputs in;
        in.nowSeconds = t;
        in.inputFps = 120;
        in.refreshHz = 120;
        in.gpuMsAvg = in.gpuMsP95 = simCost(c.tier(), 30.0); // hopelessly slow GPU
        c.update(in);
    }
    CHECK_EQ(c.tier(), 0);
    auto cfg = resolveConfig(EnhancementSettings{}, c.tier(), true);
    CHECK(cfg.sharpen > 0.0f); // degrade, never switch everything off
}

TEST_CASE("automode_frame_interpolation_rules") {
    // 60fps on 144Hz with cheap processing => enabled after hysteresis
    AutoModeController c;
    c.configure(policyFor(Preset::Auto, true), FrameGenMode::Auto, true, 4);
    double t = 0;
    bool on = false;
    for (int i = 0; i < 20; ++i, t += 0.5) {
        AutoInputs in;
        in.nowSeconds = t;
        in.inputFps = 60;
        in.refreshHz = 144;
        in.gpuMsAvg = in.gpuMsP95 = 1.5;
        in.frameGenActive = c.frameGen();
        in.frameGenMs = c.frameGen() ? 0.4 : 0;
        in.addedLatencyMs = c.frameGen() ? 10.5 : 2.0;
        on = c.update(in).frameGen;
    }
    CHECK(on);

    // 60fps on a 60Hz display => not beneficial
    AutoModeController c2;
    c2.configure(policyFor(Preset::Auto, true), FrameGenMode::Auto, true, 4);
    AutoDecision d;
    for (int i = 0; i < 20; ++i) {
        AutoInputs in;
        in.nowSeconds = i * 0.5;
        in.inputFps = 60;
        in.refreshHz = 60;
        in.gpuMsAvg = in.gpuMsP95 = 1.0;
        d = c2.update(in);
    }
    CHECK(!d.frameGen);
    CHECK(!d.frameGenBeneficial);

    // 30fps input in Low Latency Mode => hold-back (16.7ms) exceeds the latency budget
    AutoModeController c3;
    c3.configure(policyFor(Preset::Auto, true), FrameGenMode::Auto, true, 4);
    for (int i = 0; i < 20; ++i) {
        AutoInputs in;
        in.nowSeconds = i * 0.5;
        in.inputFps = 30;
        in.refreshHz = 144;
        in.gpuMsAvg = in.gpuMsP95 = 1.0;
        d = c3.update(in);
    }
    CHECK(!d.frameGen);

    // ...but forced 2x enables it
    AutoModeController c4;
    c4.configure(policyFor(Preset::Auto, true), FrameGenMode::X2, true, 4);
    for (int i = 0; i < 20; ++i) {
        AutoInputs in;
        in.nowSeconds = i * 0.5;
        in.inputFps = 30;
        in.refreshHz = 144;
        in.gpuMsAvg = in.gpuMsP95 = 1.0;
        in.frameGenActive = c4.frameGen();
        in.addedLatencyMs = 18;
        d = c4.update(in);
    }
    CHECK(d.frameGen);

    // Low Latency preset never interpolates in Auto
    AutoModeController c5;
    c5.configure(policyFor(Preset::LowLatency, true), FrameGenMode::Auto, true, 3);
    for (int i = 0; i < 20; ++i) {
        AutoInputs in;
        in.nowSeconds = i * 0.5;
        in.inputFps = 60;
        in.refreshHz = 240;
        in.gpuMsAvg = in.gpuMsP95 = 0.5;
        d = c5.update(in);
    }
    CHECK(!d.frameGen);
}

TEST_CASE("automode_interpolation_paused_before_tier_drop") {
    AutoModeController c;
    c.configure(policyFor(Preset::Auto, false), FrameGenMode::Auto, true, 4);
    double t = 0;
    for (int i = 0; i < 20; ++i, t += 0.5) {
        AutoInputs in;
        in.nowSeconds = t;
        in.inputFps = 60;
        in.refreshHz = 144;
        in.gpuMsAvg = in.gpuMsP95 = 2.0;
        in.frameGenActive = c.frameGen();
        in.frameGenMs = 0.5;
        in.addedLatencyMs = 11;
        c.update(in);
    }
    CHECK(c.frameGen());
    int tierBefore = c.tier();
    // Sudden load: over budget (8.3ms) but not catastrophic
    for (int i = 0; i < 12; ++i, t += 0.5) {
        AutoInputs in;
        in.nowSeconds = t;
        in.inputFps = 60;
        in.refreshHz = 144;
        // 8.0ms base + 0.5ms interpolation: over the 8.33ms budget only while interpolating
        in.gpuMsAvg = in.gpuMsP95 = c.frameGen() ? 8.5 : 8.0;
        in.frameGenActive = c.frameGen();
        in.frameGenMs = 0.5;
        in.addedLatencyMs = 11;
        c.update(in);
    }
    CHECK(!c.frameGen());
    CHECK_EQ(c.tier(), tierBefore);
}

TEST_CASE("automode_vram_pressure") {
    AutoModeController c;
    c.configure(policyFor(Preset::Auto, false), FrameGenMode::Off, true, 5);
    AutoInputs in;
    in.nowSeconds = 10;
    in.inputFps = 60;
    in.refreshHz = 60;
    in.gpuMsAvg = in.gpuMsP95 = 1;
    in.vramUsageMB = 3900;
    in.vramBudgetMB = 4000;
    c.update(in);
    CHECK_EQ(c.tier(), 4);
}

TEST_CASE("automode_fixed_tier_safety_governor") {
    AutoModeController c;
    c.configure(policyFor(Preset::Quality, false), FrameGenMode::Off, false, 5);
    c.setFixedTier(5);
    double t = 0;
    for (int i = 0; i < 10; ++i, t += 0.5) {
        AutoInputs in;
        in.nowSeconds = t;
        in.inputFps = 60;
        in.refreshHz = 60;
        in.gpuMsAvg = in.gpuMsP95 = 40; // way over
        c.update(in);
    }
    CHECK(c.tier() < 5);
    for (int i = 0; i < 200; ++i, t += 0.5) {
        AutoInputs in;
        in.nowSeconds = t;
        in.inputFps = 60;
        in.refreshHz = 60;
        in.gpuMsAvg = in.gpuMsP95 = 0.5;
        c.update(in);
    }
    CHECK_EQ(c.tier(), 5); // recovers, but never above the fixed tier
}

// ---------------------------------------------------------------------------
TEST_CASE("rolling_stats") {
    RollingStats r(5);
    for (int i = 1; i <= 10; ++i) r.add(i);
    CHECK_EQ(r.count(), size_t(5));
    CHECK_NEAR(r.mean(), 8.0, 1e-9);
    CHECK_NEAR(r.max(), 10.0, 1e-9);
    CHECK_NEAR(r.percentile(0.0), 6.0, 1e-9);
    CHECK_NEAR(r.percentile(1.0), 10.0, 1e-9);
    RateCounter rc(1.0);
    for (int i = 0; i < 61; ++i) rc.tick(i / 60.0);
    CHECK_NEAR(rc.rate(1.0), 60.0, 3.0);
}

TEST_CASE("log_redaction") {
    Log::addRedaction("C:\\Users\\Alice", "%USERPROFILE%");
    Log::addRedaction("Alice", "<user>");
    std::string s = Log::sanitize("Loaded c:\\users\\alice\\AppData\\x.json for Alice");
    CHECK(s.find("Alice") == std::string::npos);
    CHECK(s.find("alice") == std::string::npos);
    CHECK(s.find("%USERPROFILE%") != std::string::npos);
    CHECK(s.find("<user>") != std::string::npos);
}

TEST_CASE("log_files_not_overwritten_by_quick_restart") {
    fs::path dir = tempDir("logs");
    CHECK(Log::init(dir, LogLevel::Info, 10));
    Log::write(LogLevel::Info, "Test", "first run");
    CHECK(Log::init(dir, LogLevel::Info, 10)); // same second in practice
    Log::write(LogLevel::Info, "Test", "second run");
    Log::shutdown();
    int files = 0, withFirst = 0;
    for (const auto& e : fs::directory_iterator(dir)) {
        if (e.path().extension() != ".log") continue;
        ++files;
        std::ifstream in(e.path(), std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (text.find("first run") != std::string::npos) ++withFirst;
    }
    CHECK_EQ(files, 2);
    CHECK_EQ(withFirst, 1);
}

// Placeholders ("{}", "{:.2f}", ...) in order; a translation must keep them
// identical or std::vformat would throw at runtime.
static std::vector<std::string> placeholders(std::string_view s) {
    std::vector<std::string> out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '{') continue;
        size_t j = s.find('}', i);
        if (j == std::string_view::npos) break;
        out.emplace_back(s.substr(i, j - i + 1));
        i = j;
    }
    return out;
}

TEST_CASE("i18n_japanese_table_is_consistent") {
    int checked = 0;
    for (const auto& [en, ja] : japaneseTable()) {
        CHECK(placeholders(en) == placeholders(ja));
        CHECK(std::string_view(ja).size() > 0);
        ++checked;
    }
    CHECK(checked > 250);
    // Every English string the UI needs most must exist in the table.
    for (const char* key : {"Home", "Enhancement", "Display", "Games", "Performance", "Benchmark", "Settings", "Launch GeForce NOW", "Auto Mode",
                            "Frame Interpolation", "Low Latency Mode", "Minimal", "Light", "Balanced Lite", "Quality", "Ultra",
                            "Fast Reconstruct (Lanczos-AR)", "Native (no upscaling)", "Waiting for a GeForce NOW game"})
        CHECK(japaneseTable().count(key) == 1);
}

TEST_CASE("i18n_languages") {
    CHECK(resolveLanguage("en") == UiLanguage::English);
    CHECK(resolveLanguage("ja") == UiLanguage::Japanese);
    CHECK(resolveLanguage("ja+en") == UiLanguage::Bilingual);

    setUiLanguage(UiLanguage::English);
    CHECK_EQ(std::string(tr("Home")), std::string("Home"));
    CHECK(englishFor(tr("Home")) == nullptr);
    CHECK_EQ(trText("VRAM pressure (Balanced -> Quality)"), std::string("VRAM pressure (Balanced -> Quality)"));

    setUiLanguage(UiLanguage::Japanese);
    CHECK_EQ(std::string(tr("Home")), std::string("ホーム"));
    CHECK(englishFor(tr("Home")) == nullptr); // English hints only in bilingual mode
    CHECK_EQ(std::string(tr("Unknown text stays")), std::string("Unknown text stays"));
    CHECK_EQ(trText("VRAM pressure (Balanced -> Quality)"), std::string("VRAM 逼迫（バランス → クオリティ）"));
    CHECK_EQ(trText("Measuring Balanced Lite quality"), std::string("バランス（軽量） を測定中"));
    CHECK_EQ(trText("Windows Graphics Capture failed: Access denied (0x80070005)"),
             std::string("Windows Graphics Capture が失敗しました: Access denied (0x80070005)"));
    CHECK_EQ(trText("2560x1440 (monitor)"), std::string("2560x1440（モニター）"));
    CHECK_EQ(trText("Waiting for a GeForce NOW game"), std::string("GeForce NOW のゲームを待機中"));
    CHECK_EQ(trf("stream {}p detected", 720), std::string("ストリーム 720p を検出"));
    CHECK_EQ(trf("{:.2f} ms avg  \xC2\xB7  {:.2f} p95  \xC2\xB7  {:.2f} max", 1.0, 2.0, 3.0),
             std::string("平均 1.00 ms  \xC2\xB7  p95 2.00  \xC2\xB7  最大 3.00"));

    setUiLanguage(UiLanguage::Bilingual);
    CHECK_EQ(std::string(tr("Settings")), std::string("設定"));
    CHECK(englishFor(tr("Settings")) != nullptr);
    CHECK_EQ(std::string(englishFor(tr("Settings"))), std::string("Settings"));
    CHECK(englishFor("not a translation") == nullptr);
    CHECK(englishFor(tr("Neural Super Resolution")) == nullptr); // same in both languages

    setUiLanguage(UiLanguage::English);
}

TEST_CASE("settings_language_values") {
    Settings s;
    for (const char* v : {"auto", "en", "ja", "ja+en"}) {
        s.language = v;
        s.sanitize();
        CHECK_EQ(s.language, std::string(v));
    }
    s.language = "fr";
    s.sanitize();
    CHECK_EQ(s.language, std::string("auto"));
}

TEST_CASE("command_line") {
    auto c = parseCommandLine({"--background", "--data-dir", "D:\\x", "--seconds", "12.5", "--bogus"});
    CHECK(c.background);
    CHECK_EQ(c.dataDir, std::string("D:\\x"));
    CHECK_NEAR(c.automationSeconds, 12.5, 1e-9);
    CHECK_EQ(c.unknown.size(), size_t(1));
}


// ---------------------------------------------------------------------------
TEST_CASE("frame_pacing") {
    auto p = computePacing(60, 120, true);
    CHECK(p.useInterpolation);
    CHECK_EQ(p.syncIntervalMid, 1);
    p = computePacing(60, 240, true);
    CHECK_EQ(p.syncIntervalMid, 2);
    p = computePacing(30, 144, true);
    CHECK_EQ(p.syncIntervalMid, 2);
    CHECK(!computePacing(60, 60, true).useInterpolation);
    CHECK(!computePacing(60, 144, false).useInterpolation);
    p = computePacing(120, 240, true);
    CHECK(p.useInterpolation);
    CHECK_EQ(p.syncIntervalReal, 1);
}

// Fractal (1/f-like) value noise: approximates the spectrum of natural / game imagery.
static std::vector<float> noiseImage(int w, int h, unsigned seed) {
    std::vector<float> v(size_t(w) * h, 0.0f);
    unsigned s = seed;
    auto rnd = [&]() {
        s = s * 1664525u + 1013904223u;
        return float((s >> 8) & 0xFFFF) / 65535.0f - 0.5f;
    };
    float amp = 0.5f;
    for (int cell = 32; cell >= 1; cell /= 2, amp *= 0.62f) {
        int gw = w / cell + 2, gh = h / cell + 2;
        std::vector<float> g(size_t(gw) * gh);
        for (auto& x : g) x = rnd();
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                float u = float(x) / cell, t = float(y) / cell;
                int x0 = int(u), y0 = int(t);
                float fx = u - x0, fy = t - y0;
                float a = g[size_t(y0) * gw + x0], b = g[size_t(y0) * gw + x0 + 1], c = g[size_t(y0 + 1) * gw + x0], d = g[size_t(y0 + 1) * gw + x0 + 1];
                v[size_t(y) * w + x] += amp * ((a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy);
            }
    }
    for (auto& x : v) x = std::clamp(0.5f + x, 0.0f, 1.0f);
    return v;
}

static std::vector<float> upscaleBilinear2x(const std::vector<float>& lo, int w, int h) {
    std::vector<float> out(size_t(w) * h * 4);
    for (int y = 0; y < h * 2; ++y)
        for (int x = 0; x < w * 2; ++x) {
            float u = (x + 0.5f) * 0.5f - 0.5f, v = (y + 0.5f) * 0.5f - 0.5f;
            int x0 = int(std::floor(u)), y0 = int(std::floor(v));
            float fx = u - x0, fy = v - y0;
            auto at = [&](int xx, int yy) { return lo[size_t(std::clamp(yy, 0, h - 1)) * w + std::clamp(xx, 0, w - 1)]; };
            out[size_t(y) * w * 2 + x] = (at(x0, y0) * (1 - fx) + at(x0 + 1, y0) * fx) * (1 - fy) + (at(x0, y0 + 1) * (1 - fx) + at(x0 + 1, y0 + 1) * fx) * fy;
        }
    return out;
}

TEST_CASE("content_resolution_detection") {
    // Smooth-ish natural-like content: blurred noise
    const int w = 192, h = 128;
    std::vector<float> base = noiseImage(w, h, 3);
    auto native = measureContentResolutionCpu(base, w, h);
    double fNative = estimateUpscaleFactor(native);
    CHECK(fNative < 1.3);
    std::vector<float> lo = noiseImage(w / 2, h / 2, 5);
    std::vector<float> up = upscaleBilinear2x(lo, w / 2, h / 2);
    auto m2 = measureContentResolutionCpu(up, w, h);
    double f2 = estimateUpscaleFactor(m2);
    CHECK(f2 > 1.7);
    CHECK_EQ(streamHeightForFactor(2160, 2.0), 1080);
    CHECK_EQ(streamHeightForFactor(1440, 2.0), 720);
    CHECK_EQ(streamHeightForFactor(2160, 1.2), 0);
    // flat frames are undecidable
    std::vector<float> flat(size_t(w) * h, 0.5f);
    CHECK_EQ(estimateUpscaleFactor(measureContentResolutionCpu(flat, w, h)), 0.0);
    ContentResTracker tr(3);
    CHECK_EQ(tr.push(1080), 0);
    CHECK_EQ(tr.push(1080), 0);
    CHECK_EQ(tr.push(1080), 1080);
    CHECK_EQ(tr.push(0), 1080);
}

TEST_CASE("nsr_reference_models") {
    for (const NsrModelData* m : {&nsrModelS(), &nsrModelL()}) {
        CHECK(m->layers.size() >= 3);
        CHECK_EQ(m->layers.front().cin, 1);
        CHECK_EQ(m->layers.back().cout, 4);
        for (const auto& l : m->layers) CHECK_EQ(l.w.size(), size_t(l.cin) * size_t(l.cout) * 9);
        // Run on a small gradient + edge image
        const int w = 12, h = 10;
        std::vector<float> rgb(size_t(w) * h * 3);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                for (int c = 0; c < 3; ++c) rgb[(size_t(y) * w + x) * 3 + c] = x < w / 2 ? 0.2f + 0.02f * y : 0.8f;
        std::vector<float> out;
        nsrUpscaleReference(*m, rgb, w, h, out);
        CHECK_EQ(out.size(), size_t(w) * h * 4 * 3);
        bool finite = true;
        double mean = 0;
        for (float v : out) {
            finite &= std::isfinite(v);
            mean += v;
        }
        CHECK(finite);
        mean /= double(out.size());
        CHECK(mean > 0.3 && mean < 0.7);
    }
    CHECK_EQ(nsrModelS().channels(), 8);
    CHECK_EQ(nsrModelL().channels(), 16);
}

int main(int argc, char** argv) { return bgntest::runAll(argc > 1 ? argv[1] : nullptr); }
