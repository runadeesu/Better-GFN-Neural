#pragma once
// Data and actions exchanged between the Application and the UI pages.

#include <functional>
#include <string>
#include <vector>

#include "engine/Engine.h"
#include "gfn/GfnDetector.h"
#include "profiles/Omakase.h"
#include "platform/Controllers.h"
#include "platform/Display.h"
#include "platform/SystemMonitor.h"
#include "renderer/GpuDevice.h"
#include "settings/Settings.h"
#include "telemetry/SessionHistory.h"

namespace bgn {

struct BenchmarkUiState {
    bool running = false;
    float progress = 0;
    std::string stage;
    std::string error;
};

struct UiModel {
    GfnStatus gfn;
    std::string currentGame;
    std::string profileKey;
    EngineStats engine;
    SystemSample system;
    std::vector<MonitorInfo> monitors;
    std::vector<ControllerInfo> controllers;
    std::vector<GpuInfo> gpus;
    bool tearingSupported = false;
    bool safeMode = false;
    std::string notice;
    BenchmarkUiState bench;
    std::string version;
    bool portable = false;
    std::string dataDir;
    std::string gfnExecutable;
    std::string launchError;
    bool awaitingManualStart = false; // auto start disabled: a game is ready to be enhanced
    std::string screenshotFolder;     // resolved folder (UTF-8)
    std::vector<SessionRecord> history; // oldest first
    std::string historyExport;          // path of the last CSV export
    std::string lastExport;             // path of the last settings backup / diagnostics report
    // Omakase: what the automatic mode decided for the current game
    GameKind omakaseKind = GameKind::General;
    bool omakaseLearned = false;        // starting tier learned from earlier sessions
    int omakaseLearnedTier = -1;
};

struct UiActions {
    std::function<void()> launchGfn;
    std::function<void()> runBenchmark;
    std::function<void()> cancelBenchmark;
    std::function<void()> applyBenchmark;
    std::function<void()> openLogs;
    std::function<void()> resetSettings;
    std::function<void()> leaveSafeMode;
    std::function<void()> quit;
    std::function<void()> hideToTray;
    std::function<void()> settingsChanged;          // after any settings edit
    std::function<void(bool)> setStartWithWindows;
    std::function<void(const std::string&)> deleteProfile;
    std::function<void()> languageChanged;
    std::function<void()> firstRunCompleted;
    std::function<void()> startEnhancement;          // manual start when auto start is disabled
    std::function<void()> takeScreenshot;
    std::function<void()> openScreenshots;
    std::function<void()> exportHistory;
    std::function<void()> exportSettings;
    std::function<void()> importSettings;
    std::function<void()> createDiagnostics;
    std::function<void()> clearHistory;
};

} // namespace bgn
