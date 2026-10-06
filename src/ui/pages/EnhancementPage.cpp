#include <format>

#include "ui/I18n.h"
#include "ui/Pages.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace bgn::ui {

bool drawEnhancementEditor(EnhancementSettings& e, const char* scope) {
    bool changed = false;
    const float sc = ImGui::GetStyle().FontScaleDpi;
    const float W = ImGui::GetContentRegionAvail().x;
    const float gap = 12 * sc;
    const float colW = (W - gap) * 0.5f;
    ImGui::PushID(scope);

    // ---- Left column -----------------------------------------------------
    ImGui::BeginGroup();
    beginCard("nsr", ImVec2(colW, 0));
    sectionTitle("Neural Super Resolution",
                 tr("Real-time CNN (trained in-house) reconstructs detail when the stream is lower resolution than your display. Not DLSS: GeForce NOW does not expose game motion vectors to local apps."));
    label(tr("Upscaling mode"));
    {
        const char* items[] = {tr("Auto"), tr("Quality"), tr("Balanced"), tr("Performance"), tr("Native")};
        int v = int(e.upscale);
        if (segmented("up", &v, items, 5)) {
            e.upscale = UpscaleMode(v);
            changed = true;
        }
        textWrappedDim(tr("Quality = Neural SR (L)   Balanced = Neural SR (S)   Performance = Neural SR (Tiny, for low-end GPUs)   Native = no upscaling"));
    }
    endCard();

    beginCard("clean", ImVec2(colW, 0));
    sectionTitle(tr("Stream Compression Cleanup"), tr("Removes macroblocking, banding, mosquito noise and dark-scene artifacts from the video stream."));
    changed |= featureRow(tr("Blocking / macroblock cleanup"), e.deblock, tr("Smooths compression block edges in flat regions while keeping real edges."));
    changed |= featureRow(tr("Banding reduction"), e.deband, tr("Rebuilds smooth gradients (sky, fog, dark scenes) and dithers the output."));
    changed |= featureRow(tr("Compression & mosquito noise reduction"), e.denoise, tr("Edge-aware denoise, stronger around edges where ringing appears and in dark scenes."));
    changed |= toggleRow(tr("Adapt to stream quality"), &e.adaptiveCleanup,
                         tr("Raises the automatic cleanup strength when the stream quality monitor measures heavy compression (low bitrate, busy scenes)."));
    endCard();

    beginCard("temporal", ImVec2(colW, 0));
    sectionTitle(tr("Temporal Reconstruction"), tr("Stabilizes thin lines, text, foliage, fences and distant detail across frames using optical flow; rejects history on disocclusion to avoid ghosting."));
    changed |= featureRow(tr("Temporal stabilization & anti-flicker"), e.temporal);
    endCard();
    ImGui::EndGroup();

    ImGui::SameLine(0, gap);

    // ---- Right column ----------------------------------------------------
    ImGui::BeginGroup();
    beginCard("deblur", ImVec2(colW, 0));
    sectionTitle(tr("Deblur"), tr("Recovers edges and texture lost to motion and stream softening. Strength adapts to motion speed automatically."));
    changed |= featureRow(tr("Adaptive deblur"), e.deblur);
    changed |= toggleRow(tr("Motion deblur (follows optical flow)"), &e.motionDeblur);
    endCard();

    beginCard("sharp", ImVec2(colW, 0));
    sectionTitle(tr("Adaptive Sharpening"), tr("Content-aware: more detail when still, less during fast motion, extra clarity for text/HUD, gentle on skin."));
    changed |= featureRow(tr("Sharpness"), e.sharpen);
    changed |= toggleRow(tr("Text & HUD clarity boost"), &e.textBoost);
    changed |= toggleRow(tr("Protect skin tones"), &e.skinProtect);
    endCard();

    beginCard("fg", ImVec2(colW, 0));
    sectionTitle(tr("Frame Interpolation"),
                 tr("Optical-flow frame interpolation (not neural). Auto enables it only when your display refresh rate allows extra frames and the added latency stays within budget. HUD pixels that do not move are never warped."));
    {
        const char* items[] = {tr("Off"), tr("Auto"), "2x"};
        int v = int(e.frameGen);
        if (segmented("fgm", &v, items, 3)) {
            e.frameGen = FrameGenMode(v);
            changed = true;
        }
    }
    endCard();
    ImGui::EndGroup();

    // ---- Visual style (full width) -------------------------------------------
    beginCard("style", ImVec2(W, 0));
    sectionTitle(tr("Visual style"),
                 tr("A look applied on top of the color settings. Vivid: punchy colors. Cinematic: film-like contrast with warm highlights and teal shadows. Competitive: brighter shadows and clearer detail to spot opponents. Monochrome: black and white."));
    {
        const char* items[] = {tr("Natural"), tr("Vivid"), tr("Cinematic"), tr("Competitive"), tr("Monochrome")};
        int v = int(e.style);
        if (segmented("vstyle", &v, items, 5)) {
            e.style = VisualStyle(v);
            changed = true;
        }
    }
    endCard();

    // ---- Color (full width) -------------------------------------------------
    beginCard("color", ImVec2(W, 0));
    sectionTitle(tr("HDR+ / Color"), tr("HDR+ expands bright highlights on HDR displays; on SDR displays the same pipeline provides SDR Enhancement. Auto keeps saturation and black levels natural."));
    {
        const float half = (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
        ImGui::BeginChild("colorL", ImVec2(half, 0), ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoBackground);
        label(tr("HDR+ mode"));
        const char* hdrItems[] = {tr("Auto"), tr("On"), tr("Off")};
        int hm = int(e.hdr.mode);
        if (segmented("hdr", &hm, hdrItems, 3)) {
            e.hdr.mode = TriState(hm);
            changed = true;
        }
        changed |= sliderRow(tr("HDR+ intensity"), &e.hdr.intensity, 0.0f, 1.0f, "%.2f");
        changed |= toggleRow(tr("Color enhancement"), &e.colorEnabled);
        changed |= toggleRow(tr("Automatic color (scene adaptive)"), &e.color.automatic);
        label(tr("Tone mapping"));
        int tm = int(e.color.toneMapping);
        if (segmented("tm", &tm, hdrItems, 3)) {
            e.color.toneMapping = TriState(tm);
            changed = true;
        }
        if (secondaryButton(tr("Reset to defaults"))) {
            e.color = ColorSettings{};
            e.hdr = HdrSettings{};
            changed = true;
        }
        ImGui::EndChild();
        ImGui::SameLine(0, gap);
        ImGui::BeginChild("colorR", ImVec2(half, 0), ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoBackground);
        ImGui::BeginDisabled(!e.colorEnabled);
        auto& k = e.color;
        changed |= sliderRow(tr("Black level"), &k.blackLevel, -0.5f, 0.5f, "%+.2f");
        changed |= sliderRow(tr("White level"), &k.whiteLevel, -0.5f, 0.5f, "%+.2f");
        changed |= sliderRow(tr("Contrast"), &k.contrast, -1.0f, 1.0f, "%+.2f");
        changed |= sliderRow(tr("Gamma"), &k.gamma, 0.7f, 1.4f, "%.2f");
        changed |= sliderRow(tr("Saturation"), &k.saturation, -1.0f, 1.0f, "%+.2f");
        changed |= sliderRow(tr("Vibrance"), &k.vibrance, 0.0f, 1.0f, "%.2f");
        changed |= sliderRow(tr("Color temperature"), &k.temperature, -1.0f, 1.0f, "%+.2f");
        changed |= sliderRow(tr("Highlight recovery"), &k.highlightRecovery, 0.0f, 1.0f, "%.2f");
        changed |= sliderRow(tr("Shadow detail"), &k.shadowDetail, 0.0f, 1.0f, "%.2f");
        changed |= sliderRow(tr("Local contrast"), &k.localContrast, 0.0f, 1.0f, "%.2f");
        ImGui::EndDisabled();
        ImGui::EndChild();
    }
    endCard();
    ImGui::PopID();
    return changed;
}

void drawEnhancementPage(PageContext& c) {
    Settings& s = c.settings;
    const float sc = ImGui::GetStyle().FontScaleDpi;
    beginCard("enhHeader", ImVec2(ImGui::GetContentRegionAvail().x, 0));
    sectionTitle(tr("Enhancement"), tr("Editing global settings"));
    {
        label(tr("Preset"));
        const char* presets[] = {presetName(Preset::Auto), presetName(Preset::Ultra), presetName(Preset::Quality), presetName(Preset::Balanced),
                                 presetName(Preset::Performance), presetName(Preset::LowLatency)};
        int p = int(s.preset);
        if (segmented("preset", &p, presets, 6)) {
            s.preset = Preset(p);
            c.actions.settingsChanged();
        }
        if (toggleRow(tr("Auto Mode"), &s.autoMode,
                      tr("Monitors GPU time, VRAM, frame rates, refresh rate, GPU load and latency, and continuously picks the best quality tier. When the PC is too slow it lowers quality step by step instead of turning enhancement off.")))
            c.actions.settingsChanged();
        if (toggleRow(tr("Low Latency Mode"), &s.lowLatency, tr("Minimal queueing (1 frame), immediate presentation, tighter latency budget for interpolation.")))
            c.actions.settingsChanged();
        if (toggleRow(tr("Stutter smoothing"), &s.stutterSmoothing, tr("When a stream frame arrives late (network hiccup), a motion-continued frame is shown instead of a frozen picture. Real frames are never delayed, so no latency is added.")))
            c.actions.settingsChanged();
        label(tr("Stream resolution"));
        const char* sr[] = {tr("Auto"), tr("Native"), "720p", "1080p", "1440p"};
        int v = int(s.streamResolution);
        if (segmented("sr", &v, sr, 5)) {
            s.streamResolution = StreamResolution(v);
            c.actions.settingsChanged();
        }
        textWrappedDim(tr("When GeForce NOW fills the screen it scales the stream itself. Auto detects the real stream resolution and reconstructs it with Neural SR."));
    }
    endCard();
    ImGui::Dummy(ImVec2(0, 2 * sc));
    if (drawEnhancementEditor(s.enhancement, "global")) c.actions.settingsChanged();
}

} // namespace bgn::ui
