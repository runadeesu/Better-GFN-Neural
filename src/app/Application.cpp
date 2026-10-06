#include "app/Application.h"

#include <dbt.h>
#include <shellapi.h>

#include <algorithm>
#include <ctime>
#include <format>
#include <fstream>

#include <nlohmann/json.hpp>

#include "benchmark/Benchmark.h"
#include "core/Log.h"
#include "core/StringUtil.h"
#include "core/Version.h"
#include "platform/Controllers.h"
#include "platform/CrashHandler.h"
#include "platform/CursorControl.h"
#include "platform/Display.h"
#include "platform/Startup.h"
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
    actions_.cancelBenchmark = [this] { benchCancel_ = true; };
    actions_.applyBenchmark = [this] {
        const BenchmarkResult& r = settings_.benchmark;
        if (!r.valid) return;
        settings_.preset = r.recommendedPreset;
        settings_.enhancement.frameGen = r.recommendedFrameGen;
        markDirty();
    };
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
            UINT cmd = tray_.showMenu(!settings_.enhancementEnabled, model_.gfn.state != GfnState::NotRunning);
            switch (cmd) {
            case TrayIcon::CmdOpen: ui_.show(); break;
            case TrayIcon::CmdPauseResume:
                settings_.enhancementEnabled = !settings_.enhancementEnabled;
                markDirty();
                break;
            case TrayIcon::CmdLaunchGfn: actions_.launchGfn(); break;
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

void Application::updateEngine() {
    ResolvedProfile rp = ProfileManager::resolve(settings_, currentProfile_);
    EngineConfig c;
    c.enabled = settings_.enhancementEnabled && (settings_.autoStart || manualStart_);
    c.autoMode = settings_.autoMode;
    c.preset = rp.fromProfile ? rp.preset : settings_.preset;
    c.priority = rp.priority;
    c.lowLatency = settings_.lowLatency;
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
        lastSys_ = now;
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
        const char* names[] = {"home", "enhancement", "display", "games", "performance", "benchmark", "settings"};
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
