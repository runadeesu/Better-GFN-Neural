#pragma once
// Custom widgets for the Better GFN Neural UI (drawn with ImDrawList).

#include <string>
#include <vector>

#include "imgui.h"
#include "settings/Settings.h"

namespace bgn::ui {

enum class Icon { Home, Sliders, Monitor, Gamepad, Chart, Gauge, Gear, Play, Pause, Check, Spark, Bolt, Cpu, Close, Minimize, Info, Warning, Clock };

void drawIcon(ImDrawList* dl, Icon icon, ImVec2 center, float size, ImU32 color);

// Text helpers
void textColored(ImU32 color, const char* text, float size = 0, bool semibold = false);
void textWrappedDim(const char* text, float size = 0);
// Wrapped text in the current font/color. Japanese text may break between any
// two characters (with basic kinsoku rules); other text wraps at spaces.
void textWrapped(const char* text);
// Inserts line breaks so that `text` fits `wrapWidth` (Japanese-aware).
std::string wrapText(const char* text, float wrapWidth);
void label(const char* text); // small uppercase dim label
// Bilingual mode: the English original of `shown` in small muted text on the
// same line (skipped when it would not fit before `maxX`, a window-local x).
void englishHint(const char* shown, float mainFontSize, float maxX = 0.0f);

// Containers
void beginCard(const char* id, ImVec2 size, bool padded = true);
void endCard();
void sectionTitle(const char* title, const char* subtitle = nullptr);

// Controls
bool toggleSwitch(const char* id, bool* v);
bool toggleRow(const char* label, bool* v, const char* help = nullptr);
bool segmented(const char* id, int* current, const char* const* items, int count, float width = -1.0f);
bool sliderRow(const char* label, float* v, float minV, float maxV, const char* fmt = "%.2f", const char* help = nullptr);
bool featureRow(const char* label, Feature& f, const char* help = nullptr);
bool comboRow(const char* label, int* current, const char* const* items, int count);
bool primaryButton(const char* label, ImVec2 size = ImVec2(0, 0));
bool secondaryButton(const char* label, ImVec2 size = ImVec2(0, 0));
bool linkButton(const char* label);

// Displays
void statTile(const char* id, const char* labelText, const char* value, const char* sub, ImU32 accent, ImVec2 size, Icon icon);
void pill(const char* text, ImU32 color);
void statusOrb(ImVec2 center, float radius, ImU32 color, bool animate);
void sparkline(const char* id, const std::vector<float>& values, float minV, float maxV, ImVec2 size, ImU32 color, const char* overlay = nullptr);
void barRow(const char* label, float value, float maxValue, const char* valueText, ImU32 color);
void helpMarker(const char* text);

} // namespace bgn::ui
