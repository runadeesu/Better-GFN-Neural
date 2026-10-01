#pragma once
// Better GFN Neural visual identity ("Neural Night"): palette, fonts, ImGui style.

#include "imgui.h"

namespace bgn::ui {

namespace col {
constexpr ImU32 rgba(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }
inline const ImU32 Bg0 = rgba(9, 12, 17);
inline const ImU32 Bg1 = rgba(13, 18, 25);
inline const ImU32 Card = rgba(19, 26, 35);
inline const ImU32 CardHover = rgba(24, 33, 44);
inline const ImU32 Border = rgba(35, 45, 58);
inline const ImU32 Text = rgba(232, 238, 245);
inline const ImU32 TextDim = rgba(139, 152, 169);
inline const ImU32 TextMute = rgba(93, 104, 120);
inline const ImU32 Accent = rgba(43, 227, 176);   // mint
inline const ImU32 Accent2 = rgba(108, 123, 255); // indigo
inline const ImU32 Warn = rgba(255, 181, 71);
inline const ImU32 Error = rgba(255, 92, 108);
inline const ImU32 Waiting = rgba(107, 122, 144);
inline const ImU32 Connected = rgba(108, 123, 255);
inline const ImU32 Enhancing = rgba(43, 227, 176);
} // namespace col

ImVec4 toVec4(ImU32 c);
ImU32 withAlpha(ImU32 c, float a);
ImU32 lerpColor(ImU32 a, ImU32 b, float t);

struct Fonts {
    ImFont* regular = nullptr;
    ImFont* semibold = nullptr;
};
Fonts& fonts();

// Loads Segoe UI (+ Yu Gothic / Meiryo for Japanese) from the Windows font folder.
void loadFonts();
void applyStyle(float dpiScale);

// Sizes (unscaled, multiplied by DPI through style.FontScaleDpi)
constexpr float kFontBody = 15.0f;
constexpr float kFontSmall = 12.5f;
constexpr float kFontLabel = 11.5f;
constexpr float kFontTitle = 24.0f;
constexpr float kFontHero = 30.0f;
constexpr float kFontValue = 20.0f;

} // namespace bgn::ui
