#include <algorithm>
#include <cstring>
#include <format>

#include "core/Version.h"
#include "ui/I18n.h"
#include "ui/Pages.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace bgn::ui {

void drawSettingsPage(PageContext& c) {
    Settings& s = c.settings;
    const UiModel& m = c.model;
    const float sc = ImGui::GetStyle().FontScaleDpi;
    const float W = ImGui::GetContentRegionAvail().x, gap = 12 * sc;
    const float half = (W - gap) * 0.5f;

    if (m.safeMode) {
        beginCard("safe", ImVec2(W, 0));
        textColored(col::Warn, tr("Safe mode is active after repeated abnormal shutdowns. Heavy features are limited."), kFontBody, true);
        if (secondaryButton(tr("Leave safe mode")) && c.actions.leaveSafeMode) c.actions.leaveSafeMode();
        endCard();
    }

    ImGui::BeginGroup();
    beginCard("startup", ImVec2(half, 0));
    sectionTitle(tr("Startup"));
    bool sw = s.startWithWindows;
    if (toggleRow(tr("Start with Windows"), &sw, tr("Windows sign-in \xE2\x86\x92 Better GFN Neural starts in the tray \xE2\x86\x92 waits for GeForce NOW \xE2\x86\x92 enhances automatically."))) {
        s.startWithWindows = sw;
        if (c.actions.setStartWithWindows) c.actions.setStartWithWindows(sw);
        c.actions.settingsChanged();
    }
    if (toggleRow(tr("Start in the background (tray)"), &s.startInBackground)) c.actions.settingsChanged();
    if (toggleRow(tr("Close button minimizes to tray"), &s.closeToTray)) c.actions.settingsChanged();
    if (toggleRow(tr("Start enhancing automatically when a game starts"), &s.autoStart)) c.actions.settingsChanged();
    if (toggleRow(tr("Show notifications"), &s.showNotifications)) c.actions.settingsChanged();
    label(tr("Language"));
    {
        // Language names are always shown in their own language.
        const char* items[] = {tr("Auto"), "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E", "English", "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E + English"};
        static const char* values[] = {"auto", "ja", "en", "ja+en"};
        int v = 0;
        for (int i = 0; i < 4; ++i)
            if (s.language == values[i]) v = i;
        if (segmented("lang", &v, items, 4)) {
            s.language = values[v];
            c.actions.settingsChanged();
            if (c.actions.languageChanged) c.actions.languageChanged();
        }
    }
    endCard();

    beginCard("gfn", ImVec2(half, 0));
    sectionTitle(tr("GeForce NOW detection"));
    if (toggleRow(tr("Also detect GeForce NOW in web browsers"), &s.detectBrowser)) c.actions.settingsChanged();
    label(tr("GeForce NOW location"));
    static char pathBuf[512] = {};
    static bool init = false;
    if (!init) {
        std::strncpy(pathBuf, s.gfnExecutableOverride.c_str(), sizeof(pathBuf) - 1);
        init = true;
    }
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##gfnpath", m.gfnExecutable.empty() ? tr("Not found - enter GeForceNOW.exe path") : m.gfnExecutable.c_str(), pathBuf, sizeof(pathBuf))) {
        s.gfnExecutableOverride = pathBuf;
        c.actions.settingsChanged();
    }
    textWrappedDim(tr("Detection is read-only: window titles of GeForce NOW only. Nothing is injected into GeForce NOW and no NVIDIA servers are contacted."));
    endCard();

    beginCard("controllers", ImVec2(half, 0));
    sectionTitle(tr("Controllers"), tr("Detected for information only - input always goes directly to GeForce NOW."));
    if (m.controllers.empty()) textWrappedDim(tr("No controllers detected"));
    for (const auto& ctl : m.controllers) {
        pill(ctl.family.c_str(), ctl.family == "Xbox" ? col::Accent : col::Accent2);
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s  (%s%s)", ctl.name.c_str(), ctl.api.c_str(), ctl.slot >= 0 ? std::format(" #{}", ctl.slot + 1).c_str() : "");
    }
    endCard();
    ImGui::EndGroup();
    ImGui::SameLine(0, gap);

    ImGui::BeginGroup();
    beginCard("logs", ImVec2(half, 0));
    sectionTitle(tr("Logs"), tr("Logs never contain account data; the Windows user name and profile path are redacted."));
    {
        label(tr("Log level"));
        const char* items[] = {tr("Debug"), tr("Info"), tr("Warning"), tr("Error")};
        int v = int(s.logLevel);
        if (segmented("ll", &v, items, 4)) {
            s.logLevel = LogLevel(v);
            Log::setLevel(s.logLevel);
            c.actions.settingsChanged();
        }
        if (secondaryButton(tr("Open logs folder")) && c.actions.openLogs) c.actions.openLogs();
        ImGui::BeginChild("logview", ImVec2(-1, 150 * sc), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::PushFont(fonts().regular, kFontLabel);
        for (const auto& l : Log::recent(80)) {
            ImU32 color = l.level == LogLevel::Error ? col::Error : (l.level == LogLevel::Warning ? col::Warn : col::TextDim);
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::Text("%s [%s] %s", l.time.c_str(), l.module.c_str(), l.message.c_str());
            ImGui::PopStyleColor();
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);
        ImGui::PopFont();
        ImGui::EndChild();
    }
    endCard();

    beginCard("about", ImVec2(half, 0));
    sectionTitle(tr("About"));
    textColored(col::Text, std::format("Better GFN Neural {}", kVersionString).c_str(), kFontBody, true);
    textWrappedDim(tr("Unofficial companion app. Not affiliated with or endorsed by NVIDIA."));
    textWrappedDim(tr("GeForce NOW is a trademark of NVIDIA Corporation. This app only post-processes the picture shown on your own PC; it does not modify GeForce NOW, its servers, accounts, queues, session limits or DRM."));
    textWrappedDim(trf("Data folder: {}{}", m.dataDir, m.portable ? tr("  (portable)") : "").c_str());
    textWrappedDim(tr("License: MIT. Third-party: Dear ImGui (MIT), nlohmann/json (MIT), stb (Public Domain/MIT). See THIRD_PARTY_NOTICES.md."));
    ImGui::Dummy(ImVec2(0, 4 * sc));
    if (secondaryButton(tr("Reset all settings")) && c.actions.resetSettings) c.actions.resetSettings();
    ImGui::SameLine();
    if (secondaryButton(tr("Quit Better GFN Neural")) && c.actions.quit) c.actions.quit();
    endCard();
    ImGui::EndGroup();
}

bool drawFirstRun(PageContext& c, float t) {
    const UiModel& m = c.model;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float sc = ImGui::GetStyle().FontScaleDpi;
    // Dim the app behind the wizard with a full-screen window (not the foreground
    // draw list, which would also cover the wizard). It also swallows clicks.
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, withAlpha(col::Bg0, 0.88f));
    // (No NoBringToFrontOnFocus here: Dear ImGui inserts such windows at the back,
    // below the main window. The wizard takes focus every frame to stay on top.)
    ImGui::Begin("##firstrun_dim", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);
    ImGui::End();
    ImGui::PopStyleColor();
    const ImVec2 size(560 * sc, 470 * sc);
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + (vp->Size.x - size.x) * 0.5f, vp->Pos.y + (vp->Size.y - size.y) * 0.5f));
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col::Card);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 18 * sc);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(32 * sc, 28 * sc));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::SetNextWindowFocus(); // always above the dimmer
    ImGui::Begin("##firstrun", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    textColored(col::Text, tr("Welcome to Better GFN Neural"), kFontTitle, true);
    englishHint(tr("Welcome to Better GFN Neural"), kFontTitle);
    textWrappedDim(tr("Unofficial companion app. Not affiliated with or endorsed by NVIDIA."));
    ImGui::Dummy(ImVec2(0, 10 * sc));
    std::string gpu = m.gpus.empty() ? tr("No hardware GPU found (software fallback)") : m.gpus.front().name;
    std::string mon = m.monitors.empty() ? "-" : std::format("{} \xC2\xB7 {:.0f} Hz{}", resolutionText(m.monitors.front().width, m.monitors.front().height),
                                                             m.monitors.front().refreshHz, m.monitors.front().hdrEnabled ? " \xC2\xB7 HDR" : "");
    for (const auto& mo : m.monitors)
        if (mo.primary) mon = std::format("{} \xC2\xB7 {:.0f} Hz{}", resolutionText(mo.width, mo.height), mo.refreshHz, mo.hdrEnabled ? " \xC2\xB7 HDR" : "");
    std::string gfn = m.gfn.state != GfnState::NotRunning ? tr("Connected") : (m.gfnExecutable.empty() ? tr("Not found") : tr("Installed (not running)"));
    struct Step {
        const char* title;
        std::string detail;
    } steps[] = {{tr("Checking GPU"), gpu},
                 {tr("Checking monitors"), mon},
                 {tr("Looking for GeForce NOW"), gfn},
                 {tr("Enabling Auto Mode"), std::string(tr("Auto Mode")) + " \xC2\xB7 " + presetName(Preset::Auto)},
                 {tr("All set"), tr("Everything runs automatically. Start a game in GeForce NOW and it will be enhanced.")}};
    const int done = std::min(5, int(t / 0.6f));
    for (int i = 0; i < 5; ++i) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* wdl = ImGui::GetWindowDrawList();
        ImVec2 cc(p.x + 14 * sc, p.y + 16 * sc);
        if (i < done) {
            wdl->AddCircleFilled(cc, 13 * sc, col::Accent, 32);
            drawIcon(wdl, Icon::Check, cc, 14 * sc, col::Bg0);
        } else if (i == done) {
            statusOrb(cc, 10 * sc, col::Accent2, true);
        } else {
            wdl->AddCircle(cc, 12 * sc, col::Border, 32, 2.0f);
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x + 40 * sc, p.y));
        ImGui::BeginGroup();
        textColored(i <= done ? col::Text : col::TextMute, steps[i].title, kFontBody, true);
        englishHint(steps[i].title, kFontBody);
        if (i < done) textWrappedDim(steps[i].detail.c_str());
        ImGui::EndGroup();
        ImGui::SetCursorScreenPos(ImVec2(p.x, std::max(ImGui::GetCursorScreenPos().y, p.y + 46 * sc)));
    }
    bool open = true;
    ImGui::SetCursorPosY(size.y - 76 * sc);
    if (done >= 5) {
        if (primaryButton(tr("Get started"), ImVec2(-1, 0))) open = false;
    } else if (m.gfn.state == GfnState::NotRunning && done >= 3) {
        if (secondaryButton(tr("Launch GeForce NOW"), ImVec2(-1, 0)) && c.actions.launchGfn) c.actions.launchGfn();
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
    if (!open && c.actions.firstRunCompleted) c.actions.firstRunCompleted();
    return open;
}

} // namespace bgn::ui
