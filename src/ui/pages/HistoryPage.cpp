#include <algorithm>
#include <ctime>
#include <format>

#include "ui/I18n.h"
#include "ui/Pages.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace bgn::ui {

static std::string localDate(int64_t unixTime) {
    if (unixTime <= 0) return "-";
    std::time_t t = std::time_t(unixTime);
    std::tm lt{};
    localtime_s(&lt, &t);
    return std::format("{:04}-{:02}-{:02} {:02}:{:02}", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min);
}

static std::string duration(double seconds) {
    const int m = int(seconds / 60.0 + 0.5);
    if (m < 60) return trf("{} min", m);
    return trf("{} h {} min", m / 60, m % 60);
}

static void headerCell(const char* text) {
    ImGui::TableSetupColumn(text);
}

void drawHistoryPage(PageContext& c) {
    Settings& s = c.settings;
    const UiModel& m = c.model;
    const float sc = ImGui::GetStyle().FontScaleDpi;
    const float W = ImGui::GetContentRegionAvail().x;

    beginCard("histHeader", ImVec2(W, 0));
    sectionTitle(tr("History"), tr("Performance of every enhanced session: frame rates, GPU time, added latency and stream quality. Stored only on this PC (history.json); no account or network data."));
    if (toggleRow(tr("Record session history"), &s.recordHistory)) c.actions.settingsChanged();
    ImGui::BeginDisabled(m.history.empty());
    if (secondaryButton(tr("Export CSV")) && c.actions.exportHistory) c.actions.exportHistory();
    ImGui::SameLine();
    if (secondaryButton(tr("Clear history")) && c.actions.clearHistory) c.actions.clearHistory();
    ImGui::EndDisabled();
    if (!m.historyExport.empty()) textWrappedDim(trf("Saved: {}", m.historyExport).c_str());
    endCard();
    ImGui::Dummy(ImVec2(0, 2 * sc));

    if (m.history.empty()) {
        beginCard("histEmpty", ImVec2(W, 0));
        textWrappedDim(tr("No sessions yet. A session is recorded when a game has been enhanced for at least 30 seconds."));
        endCard();
        return;
    }

    // ---- Per game ------------------------------------------------------------
    const std::vector<GameSummary> games = summarizeHistory(m.history);
    beginCard("histGames", ImVec2(W, 0));
    sectionTitle(tr("By game"));
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX;
    if (ImGui::BeginTable("games", 6, flags)) {
        headerCell(tr("Game"));
        headerCell(tr("Sessions"));
        headerCell(tr("Play time"));
        headerCell(tr("Avg output FPS"));
        headerCell(tr("Stream quality"));
        headerCell(tr("Last played"));
        ImGui::TableHeadersRow();
        for (const auto& g : games) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(g.game.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%d", g.sessions);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(duration(g.totalSec).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.0f", g.avgOutputFps);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(g.avgQuality >= 0 ? std::format("{:.0f}", g.avgQuality).c_str() : "-");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(localDate(g.lastPlayedUnix).c_str());
        }
        ImGui::EndTable();
    }
    endCard();
    ImGui::Dummy(ImVec2(0, 2 * sc));

    // ---- Recent sessions --------------------------------------------------------
    beginCard("histSessions", ImVec2(W, 0));
    sectionTitle(tr("Recent sessions"));
    if (ImGui::BeginTable("sessions", 8, flags | ImGuiTableFlags_ScrollY, ImVec2(0, 320 * sc))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        headerCell(tr("Date"));
        headerCell(tr("Game"));
        headerCell(tr("Duration"));
        headerCell(tr("Input \xE2\x86\x92 output FPS"));
        headerCell(tr("GPU time"));
        headerCell(tr("Added latency"));
        headerCell(tr("Stream quality"));
        headerCell(tr("Quality tier"));
        ImGui::TableHeadersRow();
        int shown = 0;
        for (auto it = m.history.rbegin(); it != m.history.rend() && shown < 200; ++it, ++shown) {
            const SessionRecord& r = *it;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(localDate(r.startUnix).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.game.empty() ? "GeForce NOW" : r.game.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(duration(r.durationSec).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.0f \xE2\x86\x92 %.0f%s", r.avgInputFps, r.avgOutputFps, r.frameGenShare > 0.5 ? "  (2x)" : "");
            ImGui::TableNextColumn();
            ImGui::Text("%.1f ms", r.avgGpuMs);
            ImGui::TableNextColumn();
            ImGui::Text("+%.1f ms", r.avgLatencyMs);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.avgQuality >= 0 ? std::format("{:.0f}", r.avgQuality).c_str() : "-");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(std::format("{}  \xC2\xB7  {}", tr(tierName(r.dominantTier)), tr(r.upscaler.c_str())).c_str());
        }
        ImGui::EndTable();
    }
    endCard();
}

} // namespace bgn::ui
