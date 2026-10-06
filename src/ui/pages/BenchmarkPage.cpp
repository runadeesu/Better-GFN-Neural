#include <algorithm>
#include <format>

#include "settings/Presets.h"
#include "ui/I18n.h"
#include "ui/Pages.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace bgn::ui {

void drawBenchmarkPage(PageContext& c) {
    const BenchmarkResult& r = c.settings.benchmark;
    const BenchmarkUiState& b = c.model.bench;
    const float sc = ImGui::GetStyle().FontScaleDpi;
    const float W = ImGui::GetContentRegionAvail().x;

    beginCard("bench", ImVec2(W, 0));
    sectionTitle(tr("Benchmark"),
                 tr("Runs the complete enhancement pipeline on a synthetic cloud-game scene at every quality tier and measures real GPU time. Takes about 20-60 seconds; GeForce NOW can stay open."));
    if (b.running) {
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, col::Accent);
        ImGui::ProgressBar(b.progress, ImVec2(-1, 10 * sc), "");
        ImGui::PopStyleColor();
        textColored(col::TextDim, trText(b.stage).c_str(), kFontSmall);
        if (secondaryButton(tr("Cancel")) && c.actions.cancelBenchmark) c.actions.cancelBenchmark();
    } else {
        if (primaryButton(tr("Run benchmark")) && c.actions.runBenchmark) c.actions.runBenchmark();
        if (!b.error.empty()) {
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            textColored(col::Warn, trText(b.error).c_str(), kFontSmall);
        }
    }
    endCard();
    ImGui::Dummy(ImVec2(0, 2 * sc));

    if (!r.valid) {
        beginCard("none", ImVec2(W, 0));
        textWrappedDim(tr("No benchmark results yet."));
        endCard();
        return;
    }
    const float gap = 12 * sc, half = (W - gap) * 0.5f;
    beginCard("result", ImVec2(half, 0));
    sectionTitle(r.gpuName.c_str(), r.dateUtc.c_str());
    const float valueX = ImGui::GetContentRegionAvail().x * 0.55f;
    auto line = [valueX](const char* k, const std::string& v) {
        ImGui::PushStyleColor(ImGuiCol_Text, col::TextDim);
        ImGui::TextUnformatted(k);
        ImGui::PopStyleColor();
        englishHint(k, kFontBody, valueX - 8.0f);
        ImGui::SameLine(valueX);
        ImGui::TextUnformatted(v.c_str());
    };
    line("GPU", r.gpuName);
    line(tr("Average processing time"), std::format("{:.2f} ms", r.avgMs));
    line(tr("Maximum processing time"), std::format("{:.2f} ms", r.maxMs));
    line(tr("Recommended preset"), presetName(r.recommendedPreset));
    line(tr("Recommended output resolution"), trText(r.recommendedOutput));
    line(tr("Recommended frame interpolation"), std::format("{}  ({:.2f} ms)", tr(toString(r.recommendedFrameGen)), r.frameGenMs));
    line(tr("Test"), std::format("{} \xE2\x86\x92 {}", r.inputResolution, r.outputResolution));
    line(tr("Backend"), r.backend);
    ImGui::Dummy(ImVec2(0, 4 * sc));
    if (primaryButton(tr("Apply recommendation")) && c.actions.applyBenchmark) c.actions.applyBenchmark();
    endCard();
    ImGui::SameLine(0, gap);
    beginCard("tiers", ImVec2(half, 0));
    sectionTitle(tr("Quality tier"), tr("Average GPU time per frame (60 fps stream budget: 8.3 ms)"));
    double mx = 1.0;
    for (double v : r.tierMaxMs) mx = std::max(mx, v);
    for (size_t i = 0; i < r.tierAvgMs.size(); ++i) {
        ImU32 color = int(i) == r.recommendedTier ? col::Accent : (r.tierAvgMs[i] <= 8.33 ? col::Accent2 : col::Warn);
        barRow(tr(tierName(int(i))), float(r.tierAvgMs[i]), float(mx), std::format("{:.2f} ms", r.tierAvgMs[i]).c_str(), color);
    }
    endCard();
}

} // namespace bgn::ui
