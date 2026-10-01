#include "ui/Theme.h"

#include <filesystem>

#include "core/Log.h"
#include "core/StringUtil.h"
#include "platform/Win32.h"

#include <shlobj.h>

namespace bgn::ui {

ImVec4 toVec4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

ImU32 withAlpha(ImU32 c, float a) {
    ImVec4 v = toVec4(c);
    v.w *= a;
    return ImGui::ColorConvertFloat4ToU32(v);
}

ImU32 lerpColor(ImU32 a, ImU32 b, float t) {
    ImVec4 x = toVec4(a), y = toVec4(b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t, x.z + (y.z - x.z) * t, x.w + (y.w - x.w) * t));
}

Fonts& fonts() {
    static Fonts f;
    return f;
}

static std::filesystem::path fontDir() {
    PWSTR p = nullptr;
    std::filesystem::path out = L"C:\\Windows\\Fonts";
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Fonts, 0, nullptr, &p)) && p) out = p;
    if (p) CoTaskMemFree(p);
    return out;
}

static ImFont* addFirst(ImGuiIO& io, const std::filesystem::path& dir, std::initializer_list<const wchar_t*> names, const ImFontConfig* cfg) {
    std::error_code ec;
    for (const wchar_t* n : names) {
        std::filesystem::path p = dir / n;
        if (std::filesystem::exists(p, ec)) {
            if (ImFont* f = io.Fonts->AddFontFromFileTTF(narrow(p.wstring()).c_str(), kFontBody, cfg)) return f;
        }
    }
    return nullptr;
}

static void mergeJapanese(ImGuiIO& io, const std::filesystem::path& dir) {
    ImFontConfig cfg;
    cfg.MergeMode = true;
    cfg.PixelSnapH = true;
    std::error_code ec;
    for (const wchar_t* n : {L"YuGothM.ttc", L"meiryo.ttc", L"msgothic.ttc"}) {
        std::filesystem::path p = dir / n;
        if (std::filesystem::exists(p, ec) && io.Fonts->AddFontFromFileTTF(narrow(p.wstring()).c_str(), kFontBody, &cfg)) return;
    }
}

void loadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    const auto dir = fontDir();
    ImFontConfig cfg;
    cfg.PixelSnapH = true;
    Fonts& f = fonts();
    f.regular = addFirst(io, dir, {L"SegUIVar.ttf", L"segoeui.ttf", L"arial.ttf"}, &cfg);
    if (f.regular) mergeJapanese(io, dir);
    f.semibold = addFirst(io, dir, {L"seguisb.ttf", L"segoeuib.ttf", L"arialbd.ttf"}, &cfg);
    if (f.semibold) mergeJapanese(io, dir);
    if (!f.regular) f.regular = io.Fonts->AddFontDefault();
    if (!f.semibold) f.semibold = f.regular;
    io.FontDefault = f.regular;
}

void applyStyle(float dpiScale) {
    ImGuiStyle s;
    ImGui::StyleColorsDark(&s);
    s.WindowPadding = ImVec2(0, 0);
    s.FramePadding = ImVec2(12, 8);
    s.ItemSpacing = ImVec2(10, 10);
    s.ItemInnerSpacing = ImVec2(8, 6);
    s.ScrollbarSize = 10;
    s.GrabMinSize = 14;
    s.WindowRounding = 0;
    s.ChildRounding = 14;
    s.FrameRounding = 9;
    s.PopupRounding = 10;
    s.ScrollbarRounding = 8;
    s.GrabRounding = 8;
    s.TabRounding = 8;
    s.WindowBorderSize = 0;
    s.ChildBorderSize = 1;
    s.FrameBorderSize = 0;
    s.PopupBorderSize = 1;
    s.FontSizeBase = kFontBody;
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = toVec4(col::Text);
    c[ImGuiCol_TextDisabled] = toVec4(col::TextMute);
    c[ImGuiCol_WindowBg] = toVec4(col::Bg0);
    c[ImGuiCol_ChildBg] = toVec4(col::Card);
    c[ImGuiCol_PopupBg] = toVec4(col::rgba(20, 27, 37, 250));
    c[ImGuiCol_Border] = toVec4(col::Border);
    c[ImGuiCol_FrameBg] = toVec4(col::rgba(28, 37, 49));
    c[ImGuiCol_FrameBgHovered] = toVec4(col::rgba(35, 46, 61));
    c[ImGuiCol_FrameBgActive] = toVec4(col::rgba(40, 53, 70));
    c[ImGuiCol_TitleBg] = toVec4(col::Bg1);
    c[ImGuiCol_TitleBgActive] = toVec4(col::Bg1);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = toVec4(col::rgba(48, 60, 76));
    c[ImGuiCol_ScrollbarGrabHovered] = toVec4(col::rgba(64, 79, 99));
    c[ImGuiCol_ScrollbarGrabActive] = toVec4(col::Accent);
    c[ImGuiCol_CheckMark] = toVec4(col::Accent);
    c[ImGuiCol_SliderGrab] = toVec4(col::Accent);
    c[ImGuiCol_SliderGrabActive] = toVec4(col::rgba(120, 245, 210));
    c[ImGuiCol_Button] = toVec4(col::rgba(31, 41, 54));
    c[ImGuiCol_ButtonHovered] = toVec4(col::rgba(40, 53, 70));
    c[ImGuiCol_ButtonActive] = toVec4(col::rgba(48, 63, 83));
    c[ImGuiCol_Header] = toVec4(col::rgba(31, 41, 54));
    c[ImGuiCol_HeaderHovered] = toVec4(col::rgba(40, 53, 70));
    c[ImGuiCol_HeaderActive] = toVec4(col::rgba(48, 63, 83));
    c[ImGuiCol_Separator] = toVec4(col::Border);
    c[ImGuiCol_PlotLines] = toVec4(col::Accent);
    c[ImGuiCol_PlotHistogram] = toVec4(col::Accent);
    c[ImGuiCol_TextSelectedBg] = toVec4(withAlpha(col::Accent, 0.35f));
    c[ImGuiCol_NavCursor] = toVec4(col::Accent);
    s.ScaleAllSizes(dpiScale);
    s.FontScaleDpi = dpiScale;
    ImGui::GetStyle() = s;
}

} // namespace bgn::ui
