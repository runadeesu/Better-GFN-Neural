#include "app/Application.h"

#include <dbt.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>

#include <algorithm>
#include <ctime>
#include <format>
#include <fstream>

#include <nlohmann/json.hpp>

#include "benchmark/Benchmark.h"
#include "core/Log.h"
#include "core/StringUtil.h"
#include "core/Version.h"
#include "media/ImageOps.h"
#include "platform/Controllers.h"
#include "platform/CrashHandler.h"
#include "platform/CursorControl.h"
#include "platform/Display.h"
#include "platform/Startup.h"
#include "profiles/Omakase.h"
#include "profiles/ProfileManager.h"
#include "renderer/GpuDevice.h"
#include "ui/I18n.h"

namespace bgn {

static constexpr UINT kMsgActivate = WM_APP + 1;

Application::Application(CommandLine cmd) : cmd_(std::move(cmd)) {}

Application::~Application() { shutdown(); }

bool Application::init() {
    startTime_ = qpcSeconds();
    paths_ = resolveAppPaths(cmd_);
    registerPrivacyRedactions();
    LogLevel level = LogLevel::Info;
    Log::init(paths_.logsDir, level);
    installCrashHandler(paths_.logsDir);
    BGN_LOG_INFO("App", "Better GFN Neural {} starting ({} mode, data: {})", kVersionString, paths_.portable ? "portable" : "installed",
                 narrow(paths_.dataDir.wstring()));

    // Single instance (automation runs are independent)
    if (cmd_.automation.empty()) {
        instanceMutex_ = CreateMutexW(nullptr, TRUE, L"Local\\BetterGFNNeural.Instance");
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            HWND other = FindWindowW(L"BetterGFNNeural.Main", nullptr);
            if (other) PostMessageW(other, kMsgActivate, 0, 0);
            BGN_LOG_INFO("App", "another instance is running; activated it");
            return false;
        }
    }

    // Crash recovery: two consecutive abnormal exits => safe mode
    guard_ = std::make_unique<SessionGuard>(paths_.dataDir);
    int crashes = guard_->begin();
    safeMode_ = crashes >= 2;
    if (crashes > 0) BGN_LOG_WARN("App", "previous session ended abnormally ({} in a row){}", crashes, safeMode_ ? " - starting in safe mode" : "");

    store_ = std::make_unique<SettingsStore>(paths_.settingsFile);
    if (cmd_.resetSettings) {
        settings_ = Settings{};
    } else {
        SettingsLoadResult lr = store_->load(settings_);
        if (!lr.message.empty()) model_.notice = lr.message;
    }
    {
        std::ifstream hf(paths_.dataDir / L"history.json", std::ios::binary);
        if (hf) {
            std::string text((std::istreambuf_iterator<char>(hf)), std::istreambuf_iterator<char>());
            if (!historyFromJson(text, history_)) BGN_LOG_WARN("App", "history.json could not be read; starting a new history");
        }
        model_.history = history_;
    }
    if (!cmd_.logLevel.empty()) parseLogLevel(cmd_.logLevel, settings_.logLevel);
    Log::setLevel(settings_.logLevel);
    applyLanguage();

    // Keep the registry in sync with the setting (e.g. after moving the portable exe)
    if (settings_.startWithWindows) setStartWithWindows(true, paths_.exePath);
    else if (isStartWithWindowsEnabled()) setStartWithWindows(false, paths_.exePath);

    // Hardware inventory
    model_.gpus = enumerateGpus();
    model_.monitors = enumerateMonitors();
    model_.controllers = enumerateControllers();
    model_.tearingSupported = presentTearingSupported();
    model_.version = kVersionString;
    model_.portable = paths_.portable;
    model_.dataDir = narrow(paths_.dataDir.wstring());
    model_.safeMode = safeMode_;
    model_.gfnExecutable = narrow(findGeForceNowExecutable(settings_.gfnExecutableOverride));
    for (const auto& g : model_.gpus) BGN_LOG_INFO("App", "GPU: {} [{}], {:.0f} MB VRAM, driver {}", g.name, g.family, g.dedicatedVramMB, g.driverVersion);
    for (const auto& m : model_.monitors) BGN_LOG_INFO("App", "Monitor: {}", describeMonitor(m));
    for (const auto& c : model_.controllers) BGN_LOG_INFO("App", "Controller: {} ({})", c.name, c.api);
    if (!model_.gpus.empty()) sysmon_.setAdapter(model_.gpus.front().luid, model_.gpus.front().dedicatedVramMB);

    setupActions();
    if (!ui_.create(&settings_, &model_, &actions_, [this](HWND h, UINT m, WPARAM w, LPARAM l, LRESULT& r) { return onMessage(h, m, w, l, r); })) {
        BGN_LOG_ERROR("App", "UI initialization failed");
        return false;
    }
    tray_.create(ui_.hwnd());
    // Device-arrival notifications (controllers, monitors)
    DEV_BROADCAST_DEVICEINTERFACE_W filter{};
    filter.dbcc_size = sizeof(filter);
    filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    RegisterDeviceNotificationW(ui_.hwnd(), &filter, DEVICE_NOTIFY_WINDOW_HANDLE | DEVICE_NOTIFY_ALL_INTERFACE_CLASSES);

    engine_.start();
    updateEngine();

    const bool background = cmd_.background && settings_.startInBackground;
    const bool automation = !cmd_.automation.empty() || cmd_.noUi;
    if (!background && !automation) {
        ui_.show();
        if (!settings_.firstRunCompleted) ui_.setFirstRun(true);
    }
    BGN_LOG_INFO("App", "ready{}", background ? " (background)" : "");
    if (cmd_.simulateCrash) {
        BGN_LOG_WARN("App", "simulating a crash (test hook)");
        triggerTestCrash();
    }
    return true;
}

void Application::applyLanguage() { setUiLanguage(resolveLanguage(settings_.language)); }

void Application::setupActions() {
    actions_.launchGfn = [this] {
        std::string err;
        if (!launchGeForceNow(settings_.gfnExecutableOverride, err)) model_.launchError = err;
        else model_.launchError.clear();
    };
    actions_.settingsChanged = [this] { markDirty(); };
    actions_.setStartWithWindows = [this](bool on) { setStartWithWindows(on, paths_.exePath); };
    actions_.openLogs = [this] { ShellExecuteW(nullptr, L"open", paths_.logsDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL); };
    actions_.quit = [this] { quit_ = true; };
    actions_.hideToTray = [this] { ui_.hide(); };
    actions_.resetSettings = [this] {
        bool sw = settings_.startWithWindows;
        settings_ = Settings{};
        settings_.firstRunCompleted = true;
        if (sw) setStartWithWindows(false, paths_.exePath);
        applyLanguage();
        markDirty();
    };
    actions_.leaveSafeMode = [this] {
        safeMode_ = false;
        model_.safeMode = false;
        guard_->resetCrashCount();
        markDirty();
    };
    actions_.deleteProfile = [this](const std::string& key) {
        settings_.profiles.erase(key);
        markDirty();
    };
    actions_.languageChanged = [this] { applyLanguage(); };
    actions_.firstRunCompleted = [this] {
        settings_.firstRunCompleted = true;
        markDirty();
    };
    actions_.runBenchmark = [this] { startBenchmark(); };
    actions_.startEnhancement = [this] {
        manualStart_ = true;
        updateEngine();
    };
    actions_.takeScreenshot = [this] { takeScreenshot(); };
    actions_.openScreenshots = [this] {
        std::error_code ec;
        std::filesystem::create_directories(screenshotFolder(), ec);
        ShellExecuteW(nullptr, L"open", screenshotFolder().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    };
    actions_.exportHistory = [this] {
        PWSTR p = nullptr;
        std::filesystem::path dir = paths_.dataDir;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &p)) && p) dir = std::filesystem::path(p) / L"Better GFN Neural";
        if (p) CoTaskMemFree(p);
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        std::time_t now = std::time(nullptr);
        std::tm lt{};
        localtime_s(&lt, &now);
        wchar_t name[64];
        swprintf_s(name, L"history_%04d%02d%02d_%02d%02d%02d.csv", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec);
        const std::filesystem::path file = dir / name;
        std::ofstream f(file, std::ios::binary | std::ios::trunc);
        f << historyToCsv(history_);
        f.close();
        if (f.fail()) {
            model_.historyExport.clear();
            return;
        }
        model_.historyExport = narrow(file.wstring());
        ShellExecuteW(nullptr, L"open", L"explorer.exe", (L"/select,\"" + file.wstring() + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
    };
    actions_.exportSettings = [this] { exportSettings(); };
    actions_.importSettings = [this] { importSettings(); };
    actions_.createDiagnostics = [this] { createDiagnostics(); };
    actions_.clearHistory = [this] {
        history_.clear();
        model_.history.clear();
        saveHistory();
    };
    actions_.cancelBenchmark = [this] { benchCancel_ = true; };
    actions_.applyBenchmark = [this] {
        const BenchmarkResult& r = settings_.benchmark;
        if (!r.valid) return;
        settings_.preset = r.recommendedPreset;
        settings_.enhancement.frameGen = r.recommendedFrameGen;
        markDirty();
    };
}

std::filesystem::path Application::screenshotFolder() const {
    if (!cmd_.automation.empty()) return paths_.dataDir / L"screenshots";
    if (!settings_.screenshotFolder.empty()) return std::filesystem::path(widen(settings_.screenshotFolder));
    PWSTR p = nullptr;
    std::filesystem::path base = paths_.dataDir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &p)) && p) base = p;
    if (p) CoTaskMemFree(p);
    return base / L"Better GFN Neural";
}

void Application::takeScreenshot() {
    if (!model_.engine.overlayVisible) return;
    ScreenshotRequest r;
    r.folder = screenshotFolder();
    std::time_t now = std::time(nullptr);
    std::tm lt{};
    localtime_s(&lt, &now);
    r.baseName = screenshotBaseName(currentGame_.empty() ? std::string("GeForce NOW") : currentGame_, lt);
    r.comparison = settings_.screenshotComparison;
    engine_.requestScreenshot(r);
}

std::filesystem::path Application::documentsFolder() const {
    PWSTR p = nullptr;
    std::filesystem::path dir = paths_.dataDir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &p)) && p) dir = std::filesystem::path(p) / L"Better GFN Neural";
    if (p) CoTaskMemFree(p);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::string Application::timeStamp() {
    std::time_t now = std::time(nullptr);
    std::tm lt{};
    localtime_s(&lt, &now);
    return std::format("{:04}{:02}{:02}_{:02}{:02}{:02}", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec);
}

static void revealInExplorer(const std::filesystem::path& file) {
    const std::wstring args = L"/select,\"" + file.wstring() + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

void Application::exportSettings() {
    const std::filesystem::path file = documentsFolder() / widen("settings_backup_" + timeStamp() + ".json");
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    f << settingsToJson(settings_);
    f.close();
    if (f.fail()) {
        model_.notice = "The settings backup could not be written.";
        return;
    }
    model_.lastExport = narrow(file.wstring());
    revealInExplorer(file);
    BGN_LOG_INFO("App", "settings exported");
}

void Application::importSettings() {
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = ui_.hwnd();
    ofn.lpstrFilter = L"Better GFN Neural settings (*.json)\0*.json\0All files\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    const std::wstring initial = documentsFolder().wstring();
    ofn.lpstrInitialDir = initial.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;
    std::ifstream f(std::filesystem::path(path), std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    Settings imported;
    std::string err;
    if (text.empty() || !settingsFromJson(text, imported, &err)) {
        model_.notice = "The selected file is not a valid Better GFN Neural settings file.";
        BGN_LOG_WARN("App", "settings import failed: {}", err);
        return;
    }
    const bool sw = settings_.startWithWindows;
    imported.firstRunCompleted = true;
    settings_ = imported;
    if (settings_.startWithWindows != sw) setStartWithWindows(settings_.startWithWindows, paths_.exePath);
    applyLanguage();
    model_.notice = "Settings imported.";
    markDirty();
    BGN_LOG_INFO("App", "settings imported");
}

void Application::createDiagnostics() {
    std::string r;
    r += std::format("Better GFN Neural {} diagnostics ({} mode)\n\n", kVersionString, paths_.portable ? "portable" : "installed");
    {
        wchar_t product[128] = {}, display[64] = {}, build[32] = {};
        DWORD n = sizeof(product);
        RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"ProductName", RRF_RT_REG_SZ, nullptr, product, &n);
        n = sizeof(display);
        RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"DisplayVersion", RRF_RT_REG_SZ, nullptr, display, &n);
        n = sizeof(build);
        RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"CurrentBuildNumber", RRF_RT_REG_SZ, nullptr, build, &n);
        r += std::format("Windows: {} {} (build {})\n", narrow(product), narrow(display), narrow(build));
    }
    for (const auto& g : model_.gpus)
        r += std::format("GPU: {} [{}], {:.0f} MB, driver {}, start tier {}\n", g.name, g.family, g.dedicatedVramMB, g.driverVersion,
                         estimateTierForGpu(g.vendorId, g.name, g.dedicatedVramMB / 1024.0));
    for (const auto& m : model_.monitors) r += "Monitor: " + describeMonitor(m) + "\n";
    r += std::format("Controllers: {}\nPower: {}{}\n", model_.controllers.size(), model_.system.onBattery ? "battery" : "AC",
                     model_.system.batteryPercent >= 0 ? std::format(" ({}%)", model_.system.batteryPercent) : std::string());
    const EngineStats& e = model_.engine;
    r += std::format("\nEngine: {} | capture {} | present {}\n", e.state, e.captureBackend, e.presentPath);
    r += std::format("Rates: input {:.1f} fps, output {:.1f} fps | GPU {:.2f} ms avg, {:.2f} p95 | added latency {:.1f} ms\n", e.inputFps, e.outputFps, e.gpuMsAvg,
                     e.gpuMsP95, e.addedLatencyMs);
    r += std::format("Tier {} ({}) | upscaler {} | frame interpolation {} | stutter smoothing {} frames | stream quality {}\n", e.tier, e.tierName,
                     toString(e.upscaler), e.frameGenActive ? "active" : "off", e.concealedFrames,
                     e.streamQualityValid ? std::format("{:.0f}", e.streamQuality) : std::string("-"));
    if (settings_.benchmark.valid)
        r += std::format("Benchmark: {} - {:.2f} ms, recommended {}\n", settings_.benchmark.gpuName, settings_.benchmark.avgMs, toString(settings_.benchmark.recommendedPreset));
    r += "\n---- settings.json ----\n" + Log::sanitize(settingsToJson(settings_)) + "\n\n---- recent log ----\n";
    for (const auto& l : Log::recent(400)) r += std::format("{} [{}] {}: {}\n", l.time, toString(l.level), l.module, l.message);
    const std::filesystem::path file = documentsFolder() / widen("diagnostics_" + timeStamp() + ".txt");
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    f << r;
    f.close();
    if (f.fail()) {
        model_.notice = "The diagnostics report could not be written.";
        return;
    }
    model_.lastExport = narrow(file.wstring());
    revealInExplorer(file);
    BGN_LOG_INFO("App", "diagnostics report written");
}

void Application::saveHistory() {
    const std::filesystem::path file = paths_.dataDir / L"history.json";
    const std::filesystem::path tmp = paths_.dataDir / L"history.json.tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << historyToJson(history_);
        if (!f) return;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, file, ec);
}

void Application::finishHistorySession() {
    SessionRecord r;
    const double minSeconds = cmd_.automation.empty() ? 30.0 : 3.0;
    if (recorder_.finish(r, minSeconds)) {
        appendHistory(history_, r);
        model_.history = history_;
        saveHistory();
        BGN_LOG_INFO("App", "session recorded: {:.0f} s, {:.0f} fps", r.durationSec, r.avgOutputFps);
    }
}

void Application::finishTierLearning() {
    const double total = [this] {
        double t = 0;
        for (const auto& [tier, sec] : learnTierTime_) t += sec;
        return t;
    }();
    const double minSeconds = cmd_.automation.empty() ? 60.0 : 3.0;
    const int tier = dominantTier(learnTierTime_);
    if (!learnGame_.empty() && total >= minSeconds && tier >= 0) {
        auto it = settings_.profiles.find(learnGame_);
        if (it != settings_.profiles.end() && learnTier(it->second, tier, gpuName())) {
            BGN_LOG_INFO("App", "omakase: {} settles at tier {} on this PC", learnGame_, tier);
            // Saved with the next settings write; the running engine config stays as it is.
            if (!dirty_) dirtySince_ = qpcSeconds();
            dirty_ = true;
        }
    }
    learnTierTime_.clear();
    learnGame_.clear();
}

void Application::sampleHistory(double dt) {
    const EngineStats& e = model_.engine;
    if (learnGame_ != currentProfile_) finishTierLearning();
    // Battery cap and safe mode limit the tier artificially: not learned.
    if (e.overlayVisible && !currentProfile_.empty() && settings_.omakase && !e.batterySaverActive && !safeMode_) {
        learnGame_ = currentProfile_;
        learnTierTime_[e.tier] += dt; // time-weighted: short ramps and dips do not decide
    }
    if (!settings_.recordHistory) {
        if (recorder_.active()) finishHistorySession();
        return;
    }
    if (!e.overlayVisible) return; // paused / not focused: the session continues later
    if (recorder_.active() && recorder_.game() != currentGame_) finishHistorySession();
    if (!recorder_.active()) recorder_.begin(currentGame_, int64_t(std::time(nullptr)));
    SessionSample s;
    s.inputFps = e.inputFps;
    s.outputFps = e.outputFps;
    s.gpuMs = e.gpuMsAvg;
    s.latencyMs = e.addedLatencyMs;
    s.quality = e.streamQualityValid ? e.streamQuality : -1.0;
    s.tier = e.tier;
    s.upscaler = toString(e.upscaler);
    s.frameGen = e.frameGenActive;
    s.droppedFrames = e.droppedFrames;
    recorder_.addSample(s, dt);
}

void Application::startBenchmark() {
    if (benchThread_.joinable()) {
        if (!benchDone_) return;
        benchThread_.join();
    }
    benchCancel_ = false;
    benchDone_ = false;
    {
        std::lock_guard lock(benchMutex_);
        benchState_ = BenchmarkUiState{true, 0.0f, "Preparing", ""};
        benchHasResult_ = false;
    }
    BenchmarkOptions opt;
    // Typical cloud stream input and the user's display as output (capped at 4K)
    int outW = 1920, outH = 1080;
    double refresh = 60;
    for (const auto& m : model_.monitors)
        if (m.primary) {
            outW = std::min(m.width, 3840);
            outH = std::min(m.height, 2160);
            refresh = m.refreshHz;
        }
    opt.outW = outW;
    opt.outH = outH;
    opt.refreshHz = refresh;
    opt.forceWarp = cmd_.forceWarp;
    opt.cancel = &benchCancel_;
    opt.progress = [this](float p, const std::string& stage) {
        std::lock_guard lock(benchMutex_);
        benchState_.progress = p;
        benchState_.stage = stage;
    };
    benchThread_ = std::thread([this, opt] {
        std::string err;
        BenchmarkResult r = runBenchmark(opt, err);
        std::lock_guard lock(benchMutex_);
        benchState_.running = false;
        benchState_.error = r.valid ? "" : err;
        if (r.valid) {
            benchResult_ = r;
            benchHasResult_ = true;
        }
        benchDone_ = true;
    });
}

bool Application::onMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) {
    if (msg == TrayIcon::kCallbackMessage) {
        UINT ev = LOWORD(lp);
        if (ev == WM_CONTEXTMENU || ev == WM_RBUTTONUP) {
            UINT cmd = tray_.showMenu(!settings_.enhancementEnabled, model_.gfn.state != GfnState::NotRunning, model_.engine.overlayVisible);
            switch (cmd) {
            case TrayIcon::CmdOpen: ui_.show(); break;
            case TrayIcon::CmdPauseResume:
                settings_.enhancementEnabled = !settings_.enhancementEnabled;
                markDirty();
                break;
            case TrayIcon::CmdLaunchGfn: actions_.launchGfn(); break;
            case TrayIcon::CmdScreenshot: takeScreenshot(); break;
            case TrayIcon::CmdExit: quit_ = true; break;
            default: break;
            }
        } else if (ev == NIN_SELECT || ev == NIN_KEYSELECT || ev == WM_LBUTTONDBLCLK || ev == WM_LBUTTONUP) {
            ui_.show();
        }
        result = 0;
        return true;
    }
    if (msg == TrayIcon::taskbarCreatedMessage()) {
        tray_.recreate();
        return false;
    }
    switch (msg) {
    case kMsgActivate:
        ui_.show();
        result = 0;
        return true;
    case WM_POWERBROADCAST:
        if (wp == PBT_APMSUSPEND) {
            BGN_LOG_INFO("App", "system suspending");
            engine_.setSuspended(true);
        } else if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND) {
            BGN_LOG_INFO("App", "system resumed");
            engine_.setSuspended(false);
            engine_.notifyDisplayChange();
            model_.monitors = enumerateMonitors();
        }
        result = TRUE;
        return true;
    case WM_DISPLAYCHANGE:
        model_.monitors = enumerateMonitors();
        engine_.notifyDisplayChange();
        BGN_LOG_INFO("App", "display change: {} monitor(s)", model_.monitors.size());
        return false;
    case WM_DEVICECHANGE:
        lastControllers_ = 0; // refresh soon
        if (wp == DBT_DEVNODES_CHANGED) engine_.notifyDisplayChange();
        return false;
    case WM_QUERYENDSESSION:
        saveNow();
        result = TRUE;
        return true;
    case WM_ENDSESSION:
        if (wp) {
            saveNow();
            quit_ = true;
        }
        result = 0;
        return true;
    default: break;
    }
    (void)hwnd;
    return false;
}

void Application::markDirty() {
    if (!dirty_) dirtySince_ = qpcSeconds();
    dirty_ = true;
    updateEngine();
}

void Application::saveNow() {
    if (store_) store_->save(settings_);
    dirty_ = false;
}

// Adapter the engine runs on (the first hardware GPU; WARP when there is none).
std::string Application::gpuName() const {
    if (!model_.engine.gpuName.empty()) return model_.engine.gpuName;
    return model_.gpus.empty() ? std::string() : model_.gpus.front().name;
}

void Application::updateEngine() {
    ResolvedProfile rp = ProfileManager::resolve(settings_, currentProfile_);
    EngineConfig c;
    OmakasePlan plan;
    if (settings_.omakase) plan = planOmakase(settings_, currentProfile_, gpuName());
    model_.omakaseKind = plan.kind;
    model_.omakaseLearned = plan.learned;
    model_.omakaseLearnedTier = plan.learned ? plan.initialTier : -1;
    c.enabled = settings_.enhancementEnabled && (settings_.autoStart || manualStart_);
    c.autoMode = settings_.autoMode;
    c.preset = rp.fromProfile ? rp.preset : settings_.preset;
    c.priority = rp.priority;
    c.lowLatency = settings_.lowLatency;
    c.stutterSmoothing = settings_.stutterSmoothing;
    c.enhancement = rp.enhancement;
    c.outputMode = settings_.outputMode;
    c.outputResolution = settings_.outputResolution;
    c.streamResolution = settings_.streamResolution;
    c.captureBackend = settings_.captureBackend;
    c.preferredMonitor = settings_.preferredMonitor;
    c.compareSplit = settings_.compareSplit;
    c.accessibility = settings_.accessibility;
    c.osd = settings_.osd;
    c.power = settings_.power;
    c.forceWarp = cmd_.forceWarp;
    c.safeMode = safeMode_;
    const BenchmarkResult& b = settings_.benchmark;
    if (b.valid && !model_.gpus.empty() && b.gpuName == model_.gpus.front().name) c.initialTier = b.recommendedTier;
    if (settings_.omakase) {
        // Everything automatic: game-type tuning, Auto Mode, low latency,
        // stutter smoothing, automatic output. Personal settings (accessibility,
        // OSD, battery, monitor) are kept.
        c.autoMode = true;
        c.preset = plan.preset;
        c.priority = plan.priority;
        c.enhancement = plan.enhancement;
        c.lowLatency = true;
        c.stutterSmoothing = true;
        c.outputMode = OutputMode::Auto;
        c.outputResolution = OutputResolution::Auto;
        c.streamResolution = StreamResolution::Auto;
        c.captureBackend = CaptureBackend::Auto;
        c.compareSplit = false;
        c.initialTier = plan.initialTier;
        c.initialTierLearned = plan.learned;
    }
    c.revision = ++engineRevision_;
    engine_.setConfig(c);
}

void Application::pollDetection() {
    DetectionRules rules;
    rules.detectBrowser = settings_.detectBrowser;
    for (const auto& n : settings_.extraProcessNames) rules.processNames.push_back(toLowerAscii(n));
    detector_.setRules(rules);
    GfnStatus st = detector_.poll();
    model_.gfn = st;

    if (st.state != lastState_) {
        BGN_LOG_INFO("GFN", "state: {} -> {}", toString(lastState_), toString(st.state));
        if (st.state == GfnState::NotRunning) model_.gfnExecutable = narrow(findGeForceNowExecutable(settings_.gfnExecutableOverride));
    }
    // Game / profile handling
    std::string game = st.state == GfnState::Streaming ? st.gameName : std::string();
    if (st.state == GfnState::Streaming && (game != currentGame_ || st.streamWindow != lastStream_)) {
        if (game != currentGame_) {
            BGN_LOG_INFO("GFN", "game detected: {}", game.empty() ? "(unnamed stream)" : game);
            notifiedThisSession_ = false;
            manualStart_ = false;
            if (!game.empty()) {
                currentProfile_ = ProfileManager::onGameDetected(settings_, game, int64_t(std::time(nullptr)));
                markDirty();
            } else {
                currentProfile_.clear();
            }
        }
        currentGame_ = game;
    } else if (st.state != GfnState::Streaming && !currentGame_.empty()) {
        BGN_LOG_INFO("GFN", "game session ended: {}", currentGame_);
        finishHistorySession();
        currentGame_.clear();
        currentProfile_.clear();
        updateEngine();
    } else if (st.state != GfnState::Streaming && lastState_ == GfnState::Streaming) {
        updateEngine();
    }
    if (st.state == GfnState::Streaming && lastState_ != GfnState::Streaming) updateEngine();
    lastState_ = st.state;
    lastStream_ = st.streamWindow;
    model_.currentGame = currentGame_.empty() && st.state == GfnState::Streaming ? std::string("GeForce NOW") : currentGame_;
    model_.awaitingManualStart = st.state == GfnState::Streaming && !settings_.autoStart && !manualStart_;
    model_.profileKey = currentProfile_;

    EngineTarget t;
    if (st.state == GfnState::Streaming && st.streamWindow) {
        t.hwnd = st.streamWindow;
        t.pid = st.pid;
        t.gameName = currentGame_;
        t.browser = st.fromBrowser;
    }
    engine_.setTarget(t);
}

void Application::updateTray() {
    TrayState ts = TrayState::Waiting;
    std::string tip = "Better GFN Neural - ";
    if (!settings_.enhancementEnabled) {
        ts = TrayState::Paused;
        tip += tr("Paused");
    } else if (model_.engine.overlayVisible) {
        ts = TrayState::Enhancing;
        tip += tr("Enhancing");
        if (!currentGame_.empty()) tip += ": " + currentGame_;
        if (!notifiedThisSession_ && settings_.showNotifications) {
            tray_.notify("Better GFN Neural", std::format("{}{}", tr("Enhancing"), currentGame_.empty() ? std::string() : " " + currentGame_));
            notifiedThisSession_ = true;
        }
    } else if (model_.gfn.state != GfnState::NotRunning) {
        ts = TrayState::Connected;
        tip += tr("Connected");
    } else {
        tip += tr("Waiting");
    }
    tray_.setState(ts, tip);
}

void Application::tick() {
    const double now = qpcSeconds();
    if (now - lastDetect_ >= 0.4) {
        lastDetect_ = now;
        pollDetection();
    }
    if (now - lastSys_ >= 1.0) {
        const double dt = lastSys_ > 0 ? now - lastSys_ : 1.0;
        lastSys_ = now;
        sampleHistory(dt);
        sysmon_.sample();
        SystemSample s = sysmon_.latest();
        engine_.setSystemSample(s);
        model_.system = s;
    }
    if (now - lastControllers_ >= 10.0 && ui_.visible()) {
        lastControllers_ = now;
        model_.controllers = enumerateControllers();
    }
    if (now - lastModel_ >= 0.1) {
        lastModel_ = now;
        model_.engine = engine_.stats();
        model_.screenshotFolder = narrow(screenshotFolder().wstring());
        if (model_.engine.screenshotsSaved != lastScreenshotCount_) {
            lastScreenshotCount_ = model_.engine.screenshotsSaved;
            if (settings_.showNotifications) tray_.notify(tr("Screenshot saved"), model_.engine.lastScreenshot);
        }
        updateTray();
        std::lock_guard lock(benchMutex_);
        model_.bench = benchState_;
        if (benchHasResult_) {
            settings_.benchmark = benchResult_;
            benchHasResult_ = false;
            markDirty();
        }
    }
    if (dirty_ && now - dirtySince_ >= 1.0) saveNow();
    if (!cmd_.automation.empty()) recordAutomation();
}

void Application::recordAutomation() {
    static double lastSample = 0;
    const double now = qpcSeconds();
    if (now - lastSample < 0.25) return;
    lastSample = now;
    const EngineStats& e = model_.engine;
    nlohmann::json s{{"t", now - startTime_},
                     {"gfn", toString(model_.gfn.state)},
                     {"game", currentGame_},
                     {"profile", currentProfile_},
                     {"engine", e.state},
                     {"overlay", e.overlayVisible},
                     {"capture_backend", e.captureBackend},
                     {"captured", e.capturedFrames},
                     {"presented", e.presentedFrames},
                     {"input_fps", e.inputFps},
                     {"output_fps", e.outputFps},
                     {"tier", e.tier},
                     {"capture", std::format("{}x{}", e.captureW, e.captureH)},
                     {"output", std::format("{}x{}", e.outW, e.outH)},
                     {"error", e.lastError}};
    automationLog_ += (automationSamples_++ ? ",\n" : "") + s.dump();
    if (cmd_.screenshotAt > 0 && !automationShotTaken_ && now - startTime_ >= cmd_.screenshotAt && e.overlayVisible) {
        automationShotTaken_ = true;
        BGN_LOG_INFO("App", "automation: taking a screenshot");
        takeScreenshot();
    }
    if (cmd_.automationSeconds > 0 && now - startTime_ >= cmd_.automationSeconds) quit_ = true;
}

void Application::writeAutomationReport() {
    if (cmd_.automation.empty() || cmd_.outputJson.empty()) return;
    nlohmann::json profiles = nlohmann::json::array();
    for (const auto& [k, p] : settings_.profiles) profiles.push_back({{"key", k}, {"name", p.displayName}, {"builtin", p.builtin}, {"sessions", p.sessions}});
    std::ofstream f(std::filesystem::path(widen(cmd_.outputJson)));
    f << "{\n\"scenario\": \"" << cmd_.automation << "\",\n\"safe_mode\": " << (safeMode_ ? "true" : "false") << ",\n\"profiles\": " << profiles.dump()
      << ",\n\"timeline\": [\n"
      << automationLog_ << "\n]\n}\n";
}

int Application::run() {
    if (!init()) {
        shutdown();
        return 1;
    }
    while (!quit_) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) quit_ = true;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (quit_) break;
        tick();
        if (ui_.visible()) ui_.frame();
        else MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
    }
    // Automation: optional UI screenshots for documentation / visual checks
    if (cmd_.automation == "screenshots" && !cmd_.outputJson.empty()) {
        std::filesystem::path dir = std::filesystem::path(widen(cmd_.outputJson)).parent_path();
        const char* names[] = {"home", "enhancement", "display", "games", "performance", "history", "benchmark", "settings"};
        // English (ui_home.png), Japanese (ui_home_ja.png) and bilingual (ui_home_ja_en.png)
        const std::pair<UiLanguage, const char*> langs[] = {{UiLanguage::English, ""}, {UiLanguage::Japanese, "_ja"}, {UiLanguage::Bilingual, "_ja_en"}};
        for (const auto& [lang, suffix] : langs) {
            setUiLanguage(lang);
            for (int i = 0; i < int(ui::Page::Count); ++i)
                ui_.renderToPng(dir / (std::string("ui_") + names[i] + suffix + ".png"), 1440, 900, ui::Page(i));
            ui_.renderToPng(dir / (std::string("ui_first_run") + suffix + ".png"), 1440, 900, ui::Page::Home, true);
        }
        applyLanguage();
    }
    writeAutomationReport();
    shutdown();
    return 0;
}

void Application::shutdown() {
    static bool done = false;
    if (done) return;
    done = true;
    BGN_LOG_INFO("App", "shutting down");
    benchCancel_ = true;
    if (benchThread_.joinable()) benchThread_.join();
    engine_.stop();
    finishHistorySession();
    finishTierLearning();
    CursorControl::emergencyRestore();
    if (store_ && (dirty_ || true)) store_->save(settings_);
    tray_.destroy();
    ui_.destroy();
    if (guard_) guard_->endClean();
    if (instanceMutex_) {
        ReleaseMutex(instanceMutex_);
        CloseHandle(instanceMutex_);
    }
    Log::shutdown();
}

} // namespace bgn
