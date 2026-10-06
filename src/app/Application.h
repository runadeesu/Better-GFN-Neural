#pragma once
// Application orchestrator (UI thread): settings, GeForce NOW detection, game
// profiles, engine control, tray, startup integration, benchmark, recovery.

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

#include "app/CommandLine.h"
#include "engine/Engine.h"
#include "gfn/GfnDetector.h"
#include "platform/Paths.h"
#include "platform/SystemMonitor.h"
#include "platform/Tray.h"
#include "settings/SettingsStore.h"
#include "telemetry/SessionHistory.h"
#include "ui/UiApp.h"

namespace bgn {

class Application {
public:
    explicit Application(CommandLine cmd);
    ~Application();
    int run();

private:
    bool init();
    void shutdown();
    void tick();
    void pollDetection();
    void updateEngine();
    void updateTray();
    void markDirty();
    void saveNow();
    bool onMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT& result);
    void setupActions();
    void startBenchmark();
    void applyLanguage();
    void recordAutomation();
    void writeAutomationReport();
    std::filesystem::path screenshotFolder() const;
    void takeScreenshot();
    void sampleHistory(double dt);
    void finishHistorySession();
    void saveHistory();

    CommandLine cmd_;
    AppPaths paths_;
    std::unique_ptr<SettingsStore> store_;
    std::unique_ptr<SessionGuard> guard_;
    Settings settings_;
    UiModel model_;
    UiActions actions_;
    UiApp ui_;
    TrayIcon tray_;
    Engine engine_;
    GfnDetector detector_;
    SystemMonitor sysmon_;

    bool quit_ = false;
    bool safeMode_ = false;
    bool dirty_ = false;
    double dirtySince_ = 0;
    double lastDetect_ = 0, lastSys_ = 0, lastControllers_ = 0, lastModel_ = 0;
    uint64_t engineRevision_ = 0;
    std::string currentGame_, currentProfile_;
    GfnState lastState_ = GfnState::NotRunning;
    HWND lastStream_ = nullptr;
    bool notifiedThisSession_ = false;
    bool manualStart_ = false;
    uint64_t lastScreenshotCount_ = 0;
    SessionRecorder recorder_;
    std::vector<SessionRecord> history_;
    bool automationShotTaken_ = false;
    HANDLE instanceMutex_ = nullptr;

    // Benchmark worker
    std::thread benchThread_;
    std::atomic<bool> benchCancel_{false};
    std::atomic<bool> benchDone_{false};
    std::mutex benchMutex_;
    BenchmarkUiState benchState_;
    BenchmarkResult benchResult_;
    bool benchHasResult_ = false;

    // Automation (CI integration tests)
    double startTime_ = 0;
    std::string automationLog_;
    int automationSamples_ = 0;
};

} // namespace bgn
