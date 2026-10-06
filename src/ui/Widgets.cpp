#include "ui/Widgets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "imgui_internal.h"
#include "ui/I18n.h"
#include "ui/Theme.h"

namespace bgn::ui {

static float dpi() { return ImGui::GetStyle().FontScaleDpi; }
static float S(float v) { return v * dpi(); }

void drawIcon(ImDrawList* dl, Icon icon, ImVec2 c, float s, ImU32 col) {
    const float t = std::max(1.5f, s * 0.09f); // stroke
    const float h = s * 0.5f;
    switch (icon) {
    case Icon::Home:
        dl->PathLineTo(ImVec2(c.x - h * 0.85f, c.y - h * 0.05f));
        dl->PathLineTo(ImVec2(c.x, c.y - h * 0.85f));
        dl->PathLineTo(ImVec2(c.x + h * 0.85f, c.y - h * 0.05f));
        dl->PathStroke(col, 0, t);
        dl->AddRect(ImVec2(c.x - h * 0.6f, c.y - h * 0.15f), ImVec2(c.x + h * 0.6f, c.y + h * 0.8f), col, s * 0.06f, 0, t);
        break;
    case Icon::Sliders:
        for (int i = 0; i < 3; ++i) {
            float y = c.y - h * 0.6f + i * h * 0.6f;
            dl->AddLine(ImVec2(c.x - h * 0.8f, y), ImVec2(c.x + h * 0.8f, y), col, t);
            float kx = c.x + (i == 0 ? -0.35f : i == 1 ? 0.35f : -0.05f) * h;
            dl->AddCircleFilled(ImVec2(kx, y), s * 0.12f, col);
        }
        break;
    case Icon::Monitor:
        dl->AddRect(ImVec2(c.x - h * 0.85f, c.y - h * 0.65f), ImVec2(c.x + h * 0.85f, c.y + h * 0.4f), col, s * 0.06f, 0, t);
        dl->AddLine(ImVec2(c.x, c.y + h * 0.4f), ImVec2(c.x, c.y + h * 0.75f), col, t);
        dl->AddLine(ImVec2(c.x - h * 0.4f, c.y + h * 0.78f), ImVec2(c.x + h * 0.4f, c.y + h * 0.78f), col, t);
        break;
    case Icon::Gamepad:
        dl->AddRect(ImVec2(c.x - h * 0.9f, c.y - h * 0.45f), ImVec2(c.x + h * 0.9f, c.y + h * 0.5f), col, s * 0.22f, 0, t);
        dl->AddLine(ImVec2(c.x - h * 0.6f, c.y), ImVec2(c.x - h * 0.2f, c.y), col, t);
        dl->AddLine(ImVec2(c.x - h * 0.4f, c.y - h * 0.2f), ImVec2(c.x - h * 0.4f, c.y + h * 0.2f), col, t);
        dl->AddCircleFilled(ImVec2(c.x + h * 0.35f, c.y - h * 0.08f), s * 0.07f, col);
        dl->AddCircleFilled(ImVec2(c.x + h * 0.6f, c.y + h * 0.12f), s * 0.07f, col);
        break;
    case Icon::Chart:
        dl->AddLine(ImVec2(c.x - h * 0.85f, c.y + h * 0.75f), ImVec2(c.x + h * 0.85f, c.y + h * 0.75f), col, t);
        dl->PathLineTo(ImVec2(c.x - h * 0.75f, c.y + h * 0.35f));
        dl->PathLineTo(ImVec2(c.x - h * 0.25f, c.y - h * 0.15f));
        dl->PathLineTo(ImVec2(c.x + h * 0.15f, c.y + h * 0.15f));
        dl->PathLineTo(ImVec2(c.x + h * 0.8f, c.y - h * 0.6f));
        dl->PathStroke(col, 0, t);
        break;
    case Icon::Gauge:
        dl->PathArcTo(c, h * 0.85f, IM_PI * 0.8f, IM_PI * 2.2f, 24);
        dl->PathStroke(col, 0, t);
        dl->AddLine(c, ImVec2(c.x + h * 0.45f, c.y - h * 0.45f), col, t);
        dl->AddCircleFilled(c, s * 0.08f, col);
        break;
    case Icon::Gear: {
        dl->AddCircle(c, h * 0.35f, col, 16, t);
        for (int i = 0; i < 8; ++i) {
            float a = i * IM_PI / 4.0f;
            dl->AddLine(ImVec2(c.x + std::cos(a) * h * 0.55f, c.y + std::sin(a) * h * 0.55f), ImVec2(c.x + std::cos(a) * h * 0.85f, c.y + std::sin(a) * h * 0.85f),
                        col, t * 1.4f);
        }
        dl->AddCircle(c, h * 0.62f, col, 24, t);
        break;
    }
    case Icon::Play:
        dl->AddTriangleFilled(ImVec2(c.x - h * 0.4f, c.y - h * 0.6f), ImVec2(c.x - h * 0.4f, c.y + h * 0.6f), ImVec2(c.x + h * 0.6f, c.y), col);
        break;
    case Icon::Pause:
        dl->AddRectFilled(ImVec2(c.x - h * 0.5f, c.y - h * 0.6f), ImVec2(c.x - h * 0.15f, c.y + h * 0.6f), col, 2);
        dl->AddRectFilled(ImVec2(c.x + h * 0.15f, c.y - h * 0.6f), ImVec2(c.x + h * 0.5f, c.y + h * 0.6f), col, 2);
        break;
    case Icon::Check:
        dl->PathLineTo(ImVec2(c.x - h * 0.6f, c.y));
        dl->PathLineTo(ImVec2(c.x - h * 0.15f, c.y + h * 0.45f));
        dl->PathLineTo(ImVec2(c.x + h * 0.65f, c.y - h * 0.45f));
        dl->PathStroke(col, 0, t * 1.3f);
        break;
    case Icon::Spark: {
        for (int i = 0; i < 4; ++i) {
            float a = i * IM_PI / 2.0f;
            ImVec2 tip(c.x + std::cos(a) * h * 0.9f, c.y + std::sin(a) * h * 0.9f);
            ImVec2 l(c.x + std::cos(a + 0.6f) * h * 0.22f, c.y + std::sin(a + 0.6f) * h * 0.22f);
            ImVec2 r(c.x + std::cos(a - 0.6f) * h * 0.22f, c.y + std::sin(a - 0.6f) * h * 0.22f);
            dl->AddTriangleFilled(l, tip, r, col);
        }
        dl->AddCircleFilled(c, h * 0.25f, col);
        break;
    }
    case Icon::Bolt:
        dl->PathLineTo(ImVec2(c.x + h * 0.15f, c.y - h * 0.9f));
        dl->PathLineTo(ImVec2(c.x - h * 0.55f, c.y + h * 0.1f));
        dl->PathLineTo(ImVec2(c.x - h * 0.05f, c.y + h * 0.1f));
        dl->PathLineTo(ImVec2(c.x - h * 0.2f, c.y + h * 0.9f));
        dl->PathLineTo(ImVec2(c.x + h * 0.55f, c.y - h * 0.15f));
        dl->PathLineTo(ImVec2(c.x + h * 0.05f, c.y - h * 0.15f));
        dl->PathFillConvex(col);
        break;
    case Icon::Cpu:
        dl->AddRect(ImVec2(c.x - h * 0.55f, c.y - h * 0.55f), ImVec2(c.x + h * 0.55f, c.y + h * 0.55f), col, s * 0.06f, 0, t);
        dl->AddRectFilled(ImVec2(c.x - h * 0.22f, c.y - h * 0.22f), ImVec2(c.x + h * 0.22f, c.y + h * 0.22f), col, 2);
        for (int i = -1; i <= 1; ++i) {
            float o = i * h * 0.3f;
            dl->AddLine(ImVec2(c.x + o, c.y - h * 0.55f), ImVec2(c.x + o, c.y - h * 0.85f), col, t);
            dl->AddLine(ImVec2(c.x + o, c.y + h * 0.55f), ImVec2(c.x + o, c.y + h * 0.85f), col, t);
            dl->AddLine(ImVec2(c.x - h * 0.55f, c.y + o), ImVec2(c.x - h * 0.85f, c.y + o), col, t);
            dl->AddLine(ImVec2(c.x + h * 0.55f, c.y + o), ImVec2(c.x + h * 0.85f, c.y + o), col, t);
        }
        break;
    case Icon::Close:
        dl->AddLine(ImVec2(c.x - h * 0.45f, c.y - h * 0.45f), ImVec2(c.x + h * 0.45f, c.y + h * 0.45f), col, t);
        dl->AddLine(ImVec2(c.x + h * 0.45f, c.y - h * 0.45f), ImVec2(c.x - h * 0.45f, c.y + h * 0.45f), col, t);
        break;
    case Icon::Minimize: dl->AddLine(ImVec2(c.x - h * 0.45f, c.y), ImVec2(c.x + h * 0.45f, c.y), col, t); break;
    case Icon::Info:
        dl->AddCircle(c, h * 0.85f, col, 24, t);
        dl->AddLine(ImVec2(c.x, c.y - h * 0.1f), ImVec2(c.x, c.y + h * 0.45f), col, t * 1.2f);
        dl->AddCircleFilled(ImVec2(c.x, c.y - h * 0.4f), t, col);
        break;
    case Icon::Warning:
        dl->AddTriangle(ImVec2(c.x, c.y - h * 0.85f), ImVec2(c.x - h * 0.9f, c.y + h * 0.7f), ImVec2(c.x + h * 0.9f, c.y + h * 0.7f), col, t);
        dl->AddLine(ImVec2(c.x, c.y - h * 0.3f), ImVec2(c.x, c.y + h * 0.2f), col, t * 1.2f);
        dl->AddCircleFilled(ImVec2(c.x, c.y + h * 0.45f), t, col);
        break;
    }
}

void textColored(ImU32 color, const char* text, float size, bool semibold) {
    ImGui::PushFont(semibold ? fonts().semibold : fonts().regular, size > 0 ? size : 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void textWrappedDim(const char* text, float size) {
    ImGui::PushFont(nullptr, size > 0 ? size : kFontSmall);
    ImGui::PushStyleColor(ImGuiCol_Text, col::TextDim);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void englishHint(const char* shown, float mainFontSize, float maxX) {
    const char* en = englishFor(shown);
    if (!en) return;
    const float hintSize = std::min(kFontLabel, mainFontSize);
    ImGui::PushFont(fonts().regular, hintSize);
    // Window-local x right after the previous item.
    const float startX = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + ImGui::GetScrollX() + S(8);
    if (maxX > 0 && startX + ImGui::CalcTextSize(en).x > maxX) {
        ImGui::PopFont();
        return;
    }
    ImGui::SameLine(0, S(8));
    // Sit on the baseline of the (larger) main text.
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (mainFontSize - hintSize) * dpi() * 0.72f);
    ImGui::PushStyleColor(ImGuiCol_Text, col::TextMute);
    ImGui::TextUnformatted(en);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void label(const char* text) {
    textColored(col::TextMute, text, kFontLabel, true);
    englishHint(text, kFontLabel);
}

void beginCard(const char* id, ImVec2 size, bool padded) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, col::Card);
    ImGui::PushStyleColor(ImGuiCol_Border, col::Border);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padded ? ImVec2(S(18), S(16)) : ImVec2(0, 0));
    ImGui::BeginChild(id, size, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | (size.y == 0 ? ImGuiChildFlags_AutoResizeY : 0),
                      ImGuiWindowFlags_NoScrollbar);
}

void endCard() {
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

void sectionTitle(const char* title, const char* subtitle) {
    textColored(col::Text, title, kFontValue - 2, true);
    englishHint(title, kFontValue - 2);
    if (subtitle) textWrappedDim(subtitle);
    ImGui::Dummy(ImVec2(0, S(2)));
}

bool toggleSwitch(const char* id, bool* v) {
    const float h = ImGui::GetFrameHeight() * 0.72f, w = h * 1.85f;
    ImVec2 p = ImGui::GetCursorScreenPos();
    p.y += (ImGui::GetFrameHeight() - h) * 0.5f;
    ImGui::PushID(id);
    ImGui::InvisibleButton("##toggle", ImVec2(w, ImGui::GetFrameHeight()));
    bool changed = false;
    if (ImGui::IsItemClicked()) {
        *v = !*v;
        changed = true;
    }
    ImGuiStorage* st = ImGui::GetStateStorage();
    ImGuiID key = ImGui::GetID("anim");
    float t = st->GetFloat(key, *v ? 1.0f : 0.0f);
    float target = *v ? 1.0f : 0.0f;
    t += (target - t) * std::min(1.0f, ImGui::GetIO().DeltaTime * 14.0f);
    st->SetFloat(key, t);
    ImGui::PopID();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 bg = lerpColor(col::rgba(48, 60, 76), col::Accent, t);
    if (ImGui::IsItemHovered()) bg = lerpColor(bg, col::Text, 0.08f);
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), bg, h * 0.5f);
    float r = h * 0.5f - S(2.5f);
    ImVec2 knob(p.x + h * 0.5f + t * (w - h), p.y + h * 0.5f);
    dl->AddCircleFilled(knob, r, lerpColor(col::Text, col::Bg0, t * 0.85f), 24);
    return changed;
}

static float labelColumn() { return ImGui::GetContentRegionAvail().x * 0.42f; }

bool toggleRow(const char* text, bool* v, const char* help) {
    ImGui::PushID(text);
    ImGui::AlignTextToFramePadding();
    const float maxX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() * 1.6f - S(40);
    ImGui::TextUnformatted(text);
    englishHint(text, kFontBody, maxX);
    if (help) {
        ImGui::SameLine();
        helpMarker(help);
    }
    float w = ImGui::GetFrameHeight() * 0.72f * 1.85f;
    ImGui::SameLine();
    float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), right - w));
    bool c = toggleSwitch("sw", v);
    ImGui::PopID();
    return c;
}

bool segmented(const char* id, int* current, const char* const* items, int count, float width) {
    ImGui::PushID(id);
    if (width <= 0) width = ImGui::GetContentRegionAvail().x;
    const float h = ImGui::GetFrameHeight() + S(4);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + width, p.y + h), col::rgba(13, 18, 25), h * 0.5f);
    dl->AddRect(p, ImVec2(p.x + width, p.y + h), col::Border, h * 0.5f);
    const float segW = width / float(count);
    bool changed = false;
    ImGuiStorage* st = ImGui::GetStateStorage();
    ImGuiID key = ImGui::GetID("pos");
    float pos = st->GetFloat(key, float(*current));
    pos += (float(*current) - pos) * std::min(1.0f, ImGui::GetIO().DeltaTime * 16.0f);
    st->SetFloat(key, pos);
    ImVec2 sel0(p.x + pos * segW + S(3), p.y + S(3)), sel1(p.x + (pos + 1) * segW - S(3), p.y + h - S(3));
    dl->AddRectFilled(sel0, sel1, col::Accent, (h - S(6)) * 0.5f);
    for (int i = 0; i < count; ++i) {
        ImGui::SetCursorScreenPos(ImVec2(p.x + i * segW, p.y));
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("seg", ImVec2(segW, h))) {
            if (*current != i) changed = true;
            *current = i;
        }
        bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        const char* txt = items[i];
        ImGui::PushFont(fonts().semibold, kFontSmall + 0.5f);
        ImVec2 ts = ImGui::CalcTextSize(txt);
        ImU32 tc = (i == *current) ? col::Bg0 : (hovered ? col::Text : col::TextDim);
        dl->AddText(ImVec2(p.x + i * segW + (segW - ts.x) * 0.5f, p.y + (h - ts.y) * 0.5f), tc, txt);
        ImGui::PopFont();
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + ImGui::GetStyle().ItemSpacing.y));
    ImGui::Dummy(ImVec2(width, 0));
    ImGui::PopID();
    return changed;
}

bool sliderRow(const char* text, float* v, float minV, float maxV, const char* fmt, const char* help) {
    ImGui::PushID(text);
    float x0 = ImGui::GetCursorPosX();
    float col = labelColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, col::TextDim);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    englishHint(text, kFontBody, x0 + col - S(6));
    if (help) {
        ImGui::SameLine();
        helpMarker(help);
    }
    ImGui::SameLine(x0 + col);
    ImGui::SetNextItemWidth(-1);
    bool c = ImGui::SliderFloat("##v", v, minV, maxV, fmt);
    ImGui::PopID();
    return c;
}

bool featureRow(const char* text, Feature& f, const char* help) {
    ImGui::PushID(text);
    bool changed = toggleSwitch("en", &f.enabled);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, f.enabled ? col::Text : col::TextMute);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    englishHint(text, kFontBody, ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - S(30));
    if (help) {
        ImGui::SameLine();
        helpMarker(help);
    }
    if (f.enabled) {
        float avail = ImGui::GetContentRegionAvail().x;
        ImGui::Indent(S(52));
        changed |= ImGui::Checkbox(tr("Auto"), &f.automatic);
        ImGui::SameLine();
        ImGui::BeginDisabled(f.automatic);
        ImGui::SetNextItemWidth(std::max(S(120), avail - S(160)));
        float pct = f.strength * 100.0f;
        if (ImGui::SliderFloat("##s", &pct, 0.0f, 100.0f, f.automatic ? tr("Auto") : "%.0f%%")) {
            f.strength = pct / 100.0f;
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::Unindent(S(52));
    }
    ImGui::PopID();
    return changed;
}

bool comboRow(const char* text, int* current, const char* const* items, int count) {
    ImGui::PushID(text);
    float x0 = ImGui::GetCursorPosX();
    float col = labelColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, col::TextDim);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    englishHint(text, kFontBody, x0 + col - S(6));
    ImGui::SameLine(x0 + col);
    ImGui::SetNextItemWidth(-1);
    bool c = ImGui::Combo("##c", current, items, count);
    ImGui::PopID();
    return c;
}

bool primaryButton(const char* text, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_Button, col::Accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, lerpColor(col::Accent, col::Text, 0.25f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, lerpColor(col::Accent, col::Bg0, 0.2f));
    ImGui::PushStyleColor(ImGuiCol_Text, col::Bg0);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(22));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(20), S(10)));
    ImGui::PushFont(fonts().semibold, 0.0f);
    bool r = ImGui::Button(text, size);
    ImGui::PopFont();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    return r;
}

bool secondaryButton(const char* text, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_Button, col::rgba(28, 37, 49));
    ImGui::PushStyleColor(ImGuiCol_Border, col::Border);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(22));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(18), S(9)));
    bool r = ImGui::Button(text, size);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
    return r;
}

bool linkButton(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, col::Accent);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    bool hovered = ImGui::IsItemHovered();
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x, b.y), b, col::Accent, 1.0f);
    }
    return ImGui::IsItemClicked();
}

void statTile(const char* id, const char* labelText, const char* value, const char* sub, ImU32 accent, ImVec2 size, Icon icon) {
    ImGui::PushID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("tile", size);
    bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = S(14);
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), hovered ? col::CardHover : col::Card, r);
    dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), hovered ? withAlpha(accent, 0.5f) : col::Border, r);
    // accent glow strip
    dl->AddRectFilledMultiColor(ImVec2(p.x + r, p.y + 1), ImVec2(p.x + size.x - r, p.y + S(3)), withAlpha(accent, 0.0f), withAlpha(accent, 0.8f), withAlpha(accent, 0.8f),
                                withAlpha(accent, 0.0f));
    const float pad = S(16);
    ImVec2 ic(p.x + pad + S(14), p.y + pad + S(14));
    dl->AddCircleFilled(ic, S(15), withAlpha(accent, 0.14f), 32);
    drawIcon(dl, icon, ic, S(17), accent);
    ImGui::PushFont(fonts().semibold, kFontLabel);
    const ImVec2 lp(ic.x + S(24), ic.y - ImGui::GetFontSize() * 0.5f);
    dl->AddText(lp, col::TextMute, labelText);
    const float labelW = ImGui::CalcTextSize(labelText).x;
    ImGui::PopFont();
    if (const char* en = englishFor(labelText)) {
        ImGui::PushFont(fonts().regular, kFontLabel - 0.5f);
        if (lp.x + labelW + S(8) + ImGui::CalcTextSize(en).x < p.x + size.x - S(8))
            dl->AddText(ImVec2(lp.x + labelW + S(8), lp.y + S(0.5f)), withAlpha(col::TextMute, 0.75f), en);
        ImGui::PopFont();
    }
    ImGui::PushFont(fonts().semibold, kFontValue);
    float vy = p.y + pad + S(38);
    dl->PushClipRect(p, ImVec2(p.x + size.x - S(6), p.y + size.y), true);
    dl->AddText(ImVec2(p.x + pad, vy), col::Text, value);
    float vh = ImGui::GetFontSize();
    ImGui::PopFont();
    if (sub && *sub) {
        ImGui::PushFont(fonts().regular, kFontSmall);
        dl->AddText(ImVec2(p.x + pad, vy + vh + S(4)), col::TextDim, sub);
        ImGui::PopFont();
    }
    dl->PopClipRect();
    ImGui::PopID();
}

void pill(const char* text, ImU32 color) {
    ImGui::PushFont(fonts().semibold, kFontLabel);
    ImVec2 ts = ImGui::CalcTextSize(text);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 size(ts.x + S(20), ts.y + S(8));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), withAlpha(color, 0.16f), size.y * 0.5f);
    dl->AddCircleFilled(ImVec2(p.x + S(9), p.y + size.y * 0.5f), S(3), color);
    dl->AddText(ImVec2(p.x + S(15), p.y + S(4)), color, text);
    ImGui::Dummy(ImVec2(size.x + S(4), size.y));
    ImGui::PopFont();
}

void statusOrb(ImVec2 c, float radius, ImU32 color, bool animate) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float t = float(ImGui::GetTime());
    if (animate) {
        for (int i = 0; i < 3; ++i) {
            float ph = std::fmod(t * 0.6f + i / 3.0f, 1.0f);
            dl->AddCircle(c, radius * (1.0f + ph * 0.9f), withAlpha(color, 0.45f * (1.0f - ph)), 64, S(2));
        }
    }
    dl->AddCircleFilled(c, radius * 1.15f, withAlpha(color, 0.10f), 64);
    dl->AddCircleFilled(c, radius, withAlpha(color, 0.22f), 64);
    // rotating arc
    float a0 = animate ? t * 2.0f : 0.0f;
    dl->PathArcTo(c, radius * 0.82f, a0, a0 + IM_PI * (animate ? 1.3f : 2.0f), 48);
    dl->PathStroke(color, 0, S(4));
    dl->AddCircleFilled(c, radius * 0.48f, color, 48);
}

void sparkline(const char* id, const std::vector<float>& values, float minV, float maxV, ImVec2 size, ImU32 color, const char* overlay) {
    ImGui::PushID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), col::rgba(13, 18, 25), S(10));
    for (int i = 1; i < 4; ++i) {
        float y = p.y + size.y * i / 4.0f;
        dl->AddLine(ImVec2(p.x + S(8), y), ImVec2(p.x + size.x - S(8), y), col::rgba(30, 39, 50), 1.0f);
    }
    if (values.size() >= 2) {
        if (maxV <= minV) {
            maxV = minV + 1.0f;
            for (float v : values) maxV = std::max(maxV, v * 1.15f);
        }
        const float pad = S(6);
        const size_t n = values.size();
        std::vector<ImVec2> pts(n);
        for (size_t i = 0; i < n; ++i) {
            float x = p.x + pad + (size.x - 2 * pad) * float(i) / float(n - 1);
            float v = std::clamp((values[i] - minV) / (maxV - minV), 0.0f, 1.0f);
            pts[i] = ImVec2(x, p.y + size.y - pad - v * (size.y - 2 * pad));
        }
        for (size_t i = 1; i < n; ++i) {
            dl->AddQuadFilled(pts[i - 1], pts[i], ImVec2(pts[i].x, p.y + size.y - pad), ImVec2(pts[i - 1].x, p.y + size.y - pad), withAlpha(color, 0.10f));
        }
        dl->AddPolyline(pts.data(), int(n), color, 0, S(2));
    }
    if (overlay) {
        ImGui::PushFont(fonts().semibold, kFontSmall);
        dl->AddText(ImVec2(p.x + S(10), p.y + S(6)), col::TextDim, overlay);
        ImGui::PopFont();
    }
    ImGui::PopID();
}

void barRow(const char* text, float value, float maxValue, const char* valueText, ImU32 color) {
    float x0 = ImGui::GetCursorPosX();
    float col = labelColumn();
    ImGui::PushStyleColor(ImGuiCol_Text, col::TextDim);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    englishHint(text, kFontBody, x0 + col - S(6));
    ImGui::SameLine(x0 + col);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x - S(80);
    float h = S(8);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float y = p.y + ImGui::GetTextLineHeight() * 0.5f - h * 0.5f;
    dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + w, y + h), col::rgba(30, 39, 50), h * 0.5f);
    float f = maxValue > 0 ? std::clamp(value / maxValue, 0.0f, 1.0f) : 0.0f;
    if (f > 0) dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + std::max(h, w * f), y + h), color, h * 0.5f);
    ImGui::Dummy(ImVec2(w, ImGui::GetTextLineHeight()));
    ImGui::SameLine();
    ImGui::TextUnformatted(valueText);
}

void helpMarker(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, col::TextMute);
    ImGui::TextUnformatted("(?)");
    ImGui::PopStyleColor();
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

} // namespace bgn::ui
