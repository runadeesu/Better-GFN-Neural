#pragma once
// UI pages. Each page draws into the main content area.

#include "settings/Settings.h"
#include "ui/UiModel.h"

namespace bgn::ui {

enum class Page { Home, Enhancement, Display, Games, Performance, Benchmark, Settings, Count };

struct PageContext {
    Settings& settings;
    const UiModel& model;
    UiActions& actions;
    Page& page; // allows navigation from a page
};

void drawHomePage(PageContext& c);
void drawEnhancementPage(PageContext& c);
void drawDisplayPage(PageContext& c);
void drawGamesPage(PageContext& c);
void drawPerformancePage(PageContext& c);
void drawBenchmarkPage(PageContext& c);
void drawSettingsPage(PageContext& c);

// First-run overlay. Returns true while it is still shown.
bool drawFirstRun(PageContext& c, float timeSinceStart);

// Shared editor for global settings and per-game profiles. Returns true if modified.
bool drawEnhancementEditor(EnhancementSettings& e, const char* idScope);

// Helpers shared by pages
const char* presetName(Preset p);
std::string resolutionText(int w, int h);

} // namespace bgn::ui
