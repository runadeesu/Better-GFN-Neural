#include <algorithm>
#include <cstdio>
#include <format>

#include "core/StringUtil.h"
#include "ui/I18n.h"
#include "ui/Pages.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace bgn::ui {

const char* presetName(Preset p) { return tr(toString(p)); }

std::string resolutionText(int w, int h) {
    if (w <= 0 || h <= 0) return "-";
    return std::format("{} \xC3\x97 {}", w, h);
}

static std::string shortGpuName(const std::string& n) {
    std::string s = n;
    for (const char* prefix : {"NVIDIA GeForce ", "NVIDIA ", "AMD Radeon(TM) ", "AMD ", "Intel(R) ", "(R)", "(TM)"}) s = replaceAll(s, prefix, "");
    return trim(s);
}

static ImU32 stateColor(GfnState s, bool enhancing) {
    if (enhancing) return col::Enhancing;
    switch (s) {
    case GfnState::NotRunning: return col::Waiting;
    case GfnState::Connected: return col::Connected;
    case GfnState::Streaming: return col::Accent2;
    }
    return col::Waiting;
}

void drawHomePage(PageContext& c) {
    const UiModel& m = c.model;
    const EngineStats& e = m.engine;
    Settings& s = c.settings;
    const float sc = ImGui::GetStyle().FontScaleDpi;
    const float W = ImGui::GetContentRegionAvail().x;
    const bool enhancing = e.overlayVisible;
    const ImU32 accent = stateColor(m.gfn.state, enhancing);

    // ---------------- Hero -------------------------------------------------
    // Auto height: the text column (title, subtitle, pills, action button) grows
    // with the language and DPI; the orb defines the minimum height.
    beginCard("hero", ImVec2(W, 0));
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        statusOrb(ImVec2(p.x + 58 * sc, p.y + 70 * sc), 44 * sc, accent, enhancing || m.gfn.state == GfnState::Streaming);
        ImGui::SetCursorScreenPos(ImVec2(p.x + 138 * sc, p.y + 4 * sc));
        ImGui::BeginGroup();
        std::string title, subtitle;
        if (!s.enhancementEnabled) {
            title = tr("Paused");
            subtitle = tr("Resume enhancement");
        } else if (m.gfn.state == GfnState::NotRunning) {
            title = tr("GeForce NOW is not running");
            subtitle = tr("Waiting for GeForce NOW");
        } else if (m.gfn.state == GfnState::Connected) {
            title = tr("Connected");
            subtitle = tr("GeForce NOW is running. Start a game to enhance it automatically.");
        } else if (enhancing) {
            title = std::string(tr("Enhancing")) + (m.currentGame.empty() ? "" : "  \xE2\x80\x94  " + m.currentGame);
            subtitle = std::format("{}  \xC2\xB7  {} \xE2\x86\x92 {}  \xC2\xB7  {:.0f} fps", tr(toString(e.upscaler)), resolutionText(e.procW, e.procH),
                                   resolutionText(e.outW, e.outH), e.outputFps);
        } else {
            title = m.currentGame.empty() ? std::string("GeForce NOW") : m.currentGame;
            subtitle = e.lastError.empty() ? tr("Ready - switch to GeForce NOW to see the enhanced picture") : trText(e.lastError);
        }
        textColored(col::Text, title.c_str(), kFontHero, true);
        if (const char* en = englishFor(title.c_str())) textColored(col::TextMute, en, kFontSmall);
        textColored(col::TextDim, subtitle.c_str(), kFontBody);
        ImGui::Dummy(ImVec2(0, 4 * sc));
        pill(s.autoMode ? (std::string(tr("Auto Mode")) + "  " + tr("On")).c_str() : (std::string(tr("Auto Mode")) + "  " + tr("Off")).c_str(),
             s.autoMode ? col::Accent : col::Waiting);
        ImGui::SameLine();
        pill(presetName(s.preset), col::Accent2);
        if (e.hdrOutput) {
            ImGui::SameLine();
            pill("HDR+", col::Warn);
        }
        if (e.frameGenActive) {
            ImGui::SameLine();
            pill(tr("Frame Interpolation"), col::Accent);
        }
        if (m.safeMode) {
            ImGui::SameLine();
            pill(tr("SAFE MODE"), col::Error);
        }
        if (enhancing && e.streamQualityValid) {
            ImGui::SameLine();
            const char* q = e.streamQuality >= 80 ? "Excellent" : e.streamQuality >= 60 ? "Good" : e.streamQuality >= 40 ? "Fair" : "Poor";
            pill(std::format("{} {:.0f} \xC2\xB7 {}", tr("Stream quality"), e.streamQuality, tr(q)).c_str(),
                 e.streamQuality >= 60 ? col::Accent : (e.streamQuality >= 40 ? col::Warn : col::Error));
        }
        if (e.batterySaverActive) {
            ImGui::SameLine();
            pill(tr("Battery saver"), col::Warn);
        }
        ImGui::Dummy(ImVec2(0, 6 * sc));
        if (m.gfn.state == GfnState::NotRunning) {
            if (primaryButton(tr("Launch GeForce NOW")) && c.actions.launchGfn) c.actions.launchGfn();
            if (!m.launchError.empty()) {
                ImGui::SameLine();
                ImGui::AlignTextToFramePadding();
                textColored(col::Warn, trText(m.launchError).c_str(), kFontSmall);
            }
        } else if (m.awaitingManualStart && s.enhancementEnabled) {
            if (primaryButton(tr("Start enhancement")) && c.actions.startEnhancement) c.actions.startEnhancement();
        } else {
            if (secondaryButton(s.enhancementEnabled ? tr("Pause enhancement") : tr("Resume enhancement"))) {
                s.enhancementEnabled = !s.enhancementEnabled;
                c.actions.settingsChanged();
            }
        }
        ImGui::EndGroup();
        const float bottom = std::max(ImGui::GetItemRectMax().y, p.y + 132 * sc);
        ImGui::SetCursorScreenPos(ImVec2(p.x, bottom));
        ImGui::Dummy(ImVec2(1, 1));
    }
    endCard();
    ImGui::Dummy(ImVec2(0, 2 * sc));

    // ---------------- Tiles (4 x 2) ----------------------------------------
    const float gap = 12 * sc;
    const float tileW = (W - gap * 3) / 4.0f, tileH = 112 * sc;
    auto tile = [&](int idx, const char* lbl, const std::string& value, const std::string& sub, ImU32 color, Icon icon) {
        if (idx % 4 != 0) ImGui::SameLine(0, gap);
        statTile(lbl, tr(lbl), value.c_str(), sub.c_str(), color, ImVec2(tileW, tileH), icon);
    };
    const char* gfnState = m.gfn.state == GfnState::NotRunning ? tr("Waiting") : (m.gfn.state == GfnState::Connected ? tr("Connected") : tr("Enhancing"));
    if (m.gfn.state == GfnState::Streaming && !enhancing) gfnState = tr("Connected");
    tile(0, "GFN STATUS", gfnState, m.gfn.fromBrowser ? tr("Browser") : (m.gfn.pid ? "GeForce NOW" : "-"), stateColor(m.gfn.state, enhancing), Icon::Bolt);
    tile(1, "CURRENT GAME", m.currentGame.empty() ? tr("No game") : m.currentGame, m.profileKey.empty() ? "" : std::string(tr("Game profiles")),
         col::Accent2, Icon::Gamepad);
    tile(2, "ENHANCEMENT STATUS", enhancing ? tr("Active") : (s.enhancementEnabled ? tr("Standby") : tr("Paused")),
         enhancing ? std::string(tr(toString(e.upscaler))) : trText(e.state), enhancing ? col::Accent : col::Waiting, Icon::Spark);
    tile(3, "AUTO MODE", s.autoMode ? tr("On") : tr("Off"), enhancing ? trText(e.tierName) : std::string(presetName(s.preset)), s.autoMode ? col::Accent : col::Waiting,
         Icon::Gauge);
    tile(4, "OUTPUT RESOLUTION", enhancing ? resolutionText(e.outW, e.outH) : std::string("-"),
         enhancing ? std::format("{} {}", "\xE2\x86\x90", resolutionText(e.captureW, e.captureH)) : "", col::Accent2, Icon::Monitor);
    tile(5, "OUTPUT FPS", enhancing ? std::format("{:.0f}", e.outputFps) : std::string("-"),
         enhancing ? trf("in {:.0f} fps{}", e.inputFps, e.frameGenActive ? "  \xC2\xB7  2x" : "") : "", col::Accent, Icon::Chart);
    tile(6, "LATENCY", enhancing ? std::format("+{:.1f} ms", e.addedLatencyMs) : std::string("-"), tr("Added latency"),
         e.addedLatencyMs > 14 ? col::Warn : col::Accent, Icon::Bolt);
    std::string gpu = e.gpuName.empty() ? (m.gpus.empty() ? std::string(tr("No hardware GPU")) : m.gpus.front().name) : e.gpuName;
    tile(7, "GPU", shortGpuName(gpu),
         enhancing ? std::format("{:.1f} ms  \xC2\xB7  {}", e.gpuMsAvg, m.system.gpuUsage >= 0 ? std::format("{:.0f}%", m.system.gpuUsage * 100) : "-")
                   : (m.gpus.empty() ? "" : m.gpus.front().family),
         col::Accent2, Icon::Cpu);
    ImGui::Dummy(ImVec2(0, 2 * sc));

    // ---------------- Quick settings + live graph ---------------------------
    const float leftW = (W - gap) * 0.58f, rightW = W - gap - leftW;
    beginCard("quick", ImVec2(leftW, 0));
    sectionTitle(tr("Quick settings"));
    {
        label(tr("Preset"));
        const char* presets[] = {presetName(Preset::Auto), presetName(Preset::Ultra), presetName(Preset::Quality), presetName(Preset::Balanced),
                                 presetName(Preset::Performance), presetName(Preset::LowLatency)};
        int p = int(s.preset);
        if (segmented("preset", &p, presets, 6)) {
            s.preset = Preset(p);
            c.actions.settingsChanged();
        }
        if (toggleRow(tr("Auto Mode"), &s.autoMode)) c.actions.settingsChanged();
        if (toggleRow(tr("Low Latency Mode"), &s.lowLatency)) c.actions.settingsChanged();
        label(tr("Frame Interpolation"));
        const char* fg[] = {tr("Off"), tr("Auto"), "2x"};
        int f = int(s.enhancement.frameGen);
        if (segmented("fg", &f, fg, 3)) {
            s.enhancement.frameGen = FrameGenMode(f);
            c.actions.settingsChanged();
        }
        if (linkButton((std::string(tr("Advanced settings")) + "  \xE2\x86\x92").c_str())) c.page = Page::Enhancement;
    }
    endCard();
    ImGui::SameLine(0, gap);
    beginCard("live", ImVec2(rightW, 0));
    sectionTitle(tr("Live performance"));
    {
        float gw = ImGui::GetContentRegionAvail().x;
        std::string gpuTxt = std::format("GPU {:.2f} ms", e.gpuMsAvg);
        sparkline("gpu", e.gpuMsHistory, 0.0f, 0.0f, ImVec2(gw, 70 * sc), col::Accent, gpuTxt.c_str());
        std::string fpsTxt = std::format("{} {:.0f}  /  {} {:.0f}", tr("Output FPS"), e.outputFps, tr("Input FPS"), e.inputFps);
        sparkline("fps", e.outputFpsHistory, 0.0f, 0.0f, ImVec2(gw, 70 * sc), col::Accent2, fpsTxt.c_str());
    }
    endCard();
}

} // namespace bgn::ui
