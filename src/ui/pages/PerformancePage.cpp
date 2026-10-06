#include <algorithm>
#include <format>

#include "core/StringUtil.h"
#include "ui/I18n.h"
#include "ui/Pages.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace bgn::ui {

static const char* qualityLabel(double score) {
    if (score >= 80) return "Excellent";
    if (score >= 60) return "Good";
    if (score >= 40) return "Fair";
    return "Poor";
}

static void kv(const char* k, const std::string& v) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::PushStyleColor(ImGuiCol_Text, col::TextDim);
    ImGui::TextUnformatted(k);
    ImGui::PopStyleColor();
    englishHint(k, kFontBody, ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    ImGui::TableNextColumn();
    textWrapped(v.c_str()); // long values (VRAM, frame interpolation state) wrap instead of being clipped
}

void drawPerformancePage(PageContext& c) {
    const EngineStats& e = c.model.engine;
    const SystemSample& sys = c.model.system;
    const float sc = ImGui::GetStyle().FontScaleDpi;
    const float W = ImGui::GetContentRegionAvail().x, gap = 12 * sc;
    const float half = (W - gap) * 0.5f;

    beginCard("stats", ImVec2(half, 0));
    const std::string state = trText(e.state);
    sectionTitle(tr("Performance"), state.c_str());
    if (ImGui::BeginTable("kv", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.5f);
        ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.5f);
        kv(tr("Input FPS"), std::format("{:.1f}", e.inputFps));
        kv(tr("Output FPS"), std::format("{:.1f}", e.outputFps));
        kv(tr("Capture FPS"), std::format("{:.1f}", e.captureFps));
        kv(tr("Input resolution"), resolutionText(e.captureW, e.captureH));
        kv(tr("Processing resolution"), resolutionText(e.procW, e.procH) + (e.detectedStreamHeight ? "  (" + trf("stream {}p detected", e.detectedStreamHeight) + ")" : ""));
        kv(tr("Output resolution"), resolutionText(e.outW, e.outH) + (e.displayW ? "  " + trf("on {}", resolutionText(e.displayW, e.displayH)) : ""));
        kv(tr("Render time"), trf("{:.2f} ms avg  \xC2\xB7  {:.2f} p95  \xC2\xB7  {:.2f} max", e.gpuMsAvg, e.gpuMsP95, e.gpuMsMax));
        kv(tr("GPU usage"), sys.gpuUsage >= 0 ? std::format("{:.0f}%", sys.gpuUsage * 100.0) : std::string(tr("n/a")));
        kv("CPU", sys.cpuUsage >= 0 ? std::format("{:.0f}%", sys.cpuUsage * 100.0) : std::string(tr("n/a")));
        kv(tr("VRAM"), trf("{:.0f} MB used by app ({:.0f} MB pipeline) / budget {:.0f} MB", e.vramUsageMB, e.pipelineVramMB, e.vramBudgetMB));
        kv(tr("Frame time"), std::format("{:.2f} ms", e.frameTimeMs));
        kv(tr("Upscale mode"), std::string(tr(toString(e.upscaler))) + (e.upscalerReduced ? std::string("  (") + tr("reduced by Auto Mode") + ")" : ""));
        kv(tr("Frame Interpolation"), std::format("{} \xE2\x80\x94 {}", tr(toString(e.frameGenMode)),
                                                    e.frameGenActive ? tr("active (2x)") : (e.frameGenBeneficial ? tr("standby") : tr("not beneficial at this refresh rate"))));
        kv(tr("Dropped frames"), std::format("{}", e.droppedFrames));
        kv(tr("Stutter smoothing"), trf("{} late frames filled", e.concealedFrames));
        kv(tr("Stream quality"), e.streamQualityValid ? trf("{:.0f} ({})  \xC2\xB7  compression {:.0f}%  \xC2\xB7  stutter {:.1f}%{}", e.streamQuality,
                                                            tr(qualityLabel(e.streamQuality)), e.blockiness * 100.0, e.stutter * 100.0,
                                                            e.adaptiveCleanupActive ? std::string("  \xC2\xB7  ") + tr("adaptive cleanup active") : std::string())
                                                      : std::string("-"));
        kv(tr("Added latency"), std::format("{:.1f} ms", e.addedLatencyMs));
        kv(tr("Quality tier"), trf("{} ({}/6)  \xC2\xB7  budget {:.1f} ms", trText(e.tierName), e.tier, e.budgetMs));
        kv("HDR", e.hdrOutput ? (e.hdrInput ? tr("HDR stream \xE2\x86\x92 HDR display") : tr("HDR+ (SDR \xE2\x86\x92 HDR)")) : tr("SDR Enhancement"));
        kv(tr("Display"), std::format("{}  \xC2\xB7  {:.2f} Hz", e.monitorName, e.refreshHz));
        kv(tr("Pipeline"), std::format("{}  \xC2\xB7  {}", e.captureBackend, e.presentPath));
        kv("GPU", std::format("{}  \xC2\xB7  {}  \xC2\xB7  {}", e.gpuName, e.halfPrecision ? "FP16" : "FP32", e.driverVersion));
        ImGui::EndTable();
    }
    endCard();
    ImGui::SameLine(0, gap);

    ImGui::BeginGroup();
    beginCard("graphs", ImVec2(half, 0));
    sectionTitle(tr("Live performance"));
    float gw = ImGui::GetContentRegionAvail().x;
    sparkline("gpu", e.gpuMsHistory, 0, 0, ImVec2(gw, 80 * sc), col::Accent, std::format("GPU ms  {:.2f}", e.gpuMsAvg).c_str());
    sparkline("in", e.inputFpsHistory, 0, 0, ImVec2(gw, 70 * sc), col::Accent2, std::format("{}  {:.0f}", tr("Input FPS"), e.inputFps).c_str());
    sparkline("out", e.outputFpsHistory, 0, 0, ImVec2(gw, 70 * sc), col::Warn, std::format("{}  {:.0f}", tr("Output FPS"), e.outputFps).c_str());
    sparkline("quality", e.qualityHistory, 0, 100, ImVec2(gw, 70 * sc), col::Accent,
              (std::string(tr("Stream quality")) + (e.streamQualityValid ? std::format("  {:.0f}", e.streamQuality) : std::string("  -"))).c_str());
    endCard();

    beginCard("stages", ImVec2(half, 0));
    sectionTitle(tr("GPU time per stage"));
    double maxStage = 0.5;
    for (double v : e.stageMs) maxStage = std::max(maxStage, v);
    for (int i = 0; i < kGpuStageCount; ++i) {
        if (GpuStage(i) == GpuStage::Present || GpuStage(i) == GpuStage::Analysis) continue;
        barRow(tr(toString(GpuStage(i))), float(e.stageMs[i]), float(maxStage), std::format("{:.2f} ms", e.stageMs[i]).c_str(),
               GpuStage(i) == GpuStage::Upscale ? col::Accent2 : col::Accent);
    }
    endCard();

    beginCard("auto", ImVec2(half, 0));
    sectionTitle(tr("Auto Mode decision"));
    textColored(col::Text, trText(e.autoReason).c_str(), kFontBody);
    textWrappedDim(tr("Auto Mode keeps our GPU work below the budget (a share of the input frame interval), watches VRAM, GPU load and latency, and lowers the quality tier gradually under load. Frame interpolation is paused first."));
    endCard();
    ImGui::EndGroup();
}

} // namespace bgn::ui
