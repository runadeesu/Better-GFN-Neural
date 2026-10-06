#include <format>

#include "ui/I18n.h"
#include "ui/Pages.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace bgn::ui {

void drawDisplayPage(PageContext& c) {
    Settings& s = c.settings;
    const UiModel& m = c.model;
    const float sc = ImGui::GetStyle().FontScaleDpi;
    const float W = ImGui::GetContentRegionAvail().x;

    beginCard("output", ImVec2(W, 0));
    sectionTitle(tr("Output"), tr("Better GFN Neural shows the enhanced picture in a click-through overlay exactly over GeForce NOW. Mouse, keyboard and controllers keep working normally."));
    label(tr("Output mode"));
    {
        const char* items[] = {tr("Auto"), tr("Match GFN window"), tr("Fullscreen (upscale to monitor)")};
        int v = int(s.outputMode);
        if (segmented("om", &v, items, 3)) {
            s.outputMode = OutputMode(v);
            c.actions.settingsChanged();
        }
        textWrappedDim(tr("Auto: when GeForce NOW runs fullscreen the overlay matches it; when it runs in a smaller window the picture is upscaled to the whole monitor (the cursor is kept inside the game window and drawn scaled)."));
    }
    label(tr("Output resolution"));
    {
        const char* items[] = {tr("Auto"), tr("Source"), "1080p", "1440p", "2160p"};
        int v = int(s.outputResolution);
        if (segmented("or", &v, items, 5)) {
            s.outputResolution = OutputResolution(v);
            c.actions.settingsChanged();
        }
    }
    if (toggleRow(tr("Before / after split view"), &s.compareSplit)) c.actions.settingsChanged();
    if (toggleRow(tr("Low Latency Mode"), &s.lowLatency)) c.actions.settingsChanged();
    label(tr("Capture method"));
    {
        const char* items[] = {tr("Auto"), "Windows Graphics Capture", "DXGI Desktop Duplication"};
        int v = int(s.captureBackend);
        if (segmented("cb", &v, items, 3)) {
            s.captureBackend = CaptureBackend(v);
            c.actions.settingsChanged();
        }
    }
    endCard();
    ImGui::Dummy(ImVec2(0, 2 * sc));

    beginCard("access", ImVec2(W, 0));
    sectionTitle(tr("Accessibility"),
                 tr("Color vision support shifts colors that are hard to tell apart into colors you can distinguish (daltonization). Night light reduces blue light. Both change only the enhanced picture."));
    label(tr("Color vision support"));
    {
        const char* items[] = {tr("Off"), tr("Protanopia (red)"), tr("Deuteranopia (green)"), tr("Tritanopia (blue)")};
        int v = int(s.accessibility.colorVision);
        if (segmented("cvd", &v, items, 4)) {
            s.accessibility.colorVision = ColorVision(v);
            c.actions.settingsChanged();
        }
    }
    ImGui::BeginDisabled(s.accessibility.colorVision == ColorVision::Off);
    if (sliderRow(tr("Correction strength"), &s.accessibility.colorVisionStrength, 0.0f, 1.0f, "%.2f")) c.actions.settingsChanged();
    ImGui::EndDisabled();
    if (sliderRow(tr("Night light (blue light reduction)"), &s.accessibility.nightLight, 0.0f, 1.0f, "%.2f")) c.actions.settingsChanged();
    endCard();
    ImGui::Dummy(ImVec2(0, 2 * sc));

    beginCard("monitors", ImVec2(W, 0));
    sectionTitle(tr("Monitors"));
    label(tr("Preferred monitor"));
    {
        std::vector<std::string> names{tr("Automatic (monitor showing GeForce NOW)")};
        int current = 0;
        for (size_t i = 0; i < m.monitors.size(); ++i) {
            const auto& mon = m.monitors[i];
            names.push_back(std::format("{}  ({})", mon.friendlyName.empty() ? mon.deviceName : mon.friendlyName, mon.deviceName));
            if (mon.deviceName == s.preferredMonitor) current = int(i) + 1;
        }
        std::vector<const char*> ptrs;
        for (auto& n : names) ptrs.push_back(n.c_str());
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##mon", &current, ptrs.data(), int(ptrs.size()))) {
            s.preferredMonitor = current == 0 ? std::string() : m.monitors[size_t(current - 1)].deviceName;
            c.actions.settingsChanged();
        }
    }
    ImGui::Dummy(ImVec2(0, 4 * sc));
    if (ImGui::BeginTable("montable", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX)) {
        ImGui::TableSetupColumn(tr("Monitor"));
        ImGui::TableSetupColumn(tr("Resolution"));
        ImGui::TableSetupColumn(tr("Refresh"));
        ImGui::TableSetupColumn("HDR");
        ImGui::TableSetupColumn(tr("Peak / SDR white"));
        ImGui::TableSetupColumn(tr("Color depth"));
        ImGui::TableSetupColumn(tr("VRR / tearing"));
        ImGui::TableHeadersRow();
        for (const auto& mon : m.monitors) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%s%s", mon.friendlyName.empty() ? mon.deviceName.c_str() : mon.friendlyName.c_str(), mon.primary ? "  \xE2\x98\x85" : "");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(resolutionText(mon.width, mon.height).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.2f Hz", mon.refreshHz);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(mon.hdrEnabled ? tr("On") : (mon.hdrSupported ? tr("Supported (off)") : tr("Not supported")));
            ImGui::TableNextColumn();
            ImGui::Text("%.0f / %.0f nits", mon.maxLuminance, mon.sdrWhiteNits);
            ImGui::TableNextColumn();
            ImGui::Text("%d-bit", mon.bitsPerColor);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(m.tearingSupported ? tr("Capable") : tr("Not available"));
        }
        ImGui::EndTable();
    }
    textWrappedDim(tr("Refresh rate is read from the active display mode (e.g. 60, 120, 144, 165, 240 Hz) and used by Auto Mode and frame pacing. VRR-capable presentation is reported by DXGI (tearing support); the overlay is composed by Windows, so VRR follows the desktop compositor."));
    endCard();
}

} // namespace bgn::ui
