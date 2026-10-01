#include "ui/UiApp.h"

#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"
#include "core/Log.h"
#include "core/Version.h"
#include "imgui.h"
#include "stb_image_write.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

namespace bgn {

using namespace ui;

namespace {
const wchar_t* kMainClass = L"BetterGFNNeural.Main";

void drawLogo(ImDrawList* dl, ImVec2 p, float s) {
    const float r = s * 0.26f;
    dl->AddRectFilledMultiColor(p, ImVec2(p.x + s, p.y + s), col::Accent, col::Accent2, col::Accent2, col::Accent);
    // round the corners by painting the background over them
    dl->AddRect(ImVec2(p.x - r * 0.5f, p.y - r * 0.5f), ImVec2(p.x + s + r * 0.5f, p.y + s + r * 0.5f), col::Bg1, r * 1.5f, 0, r);
    // neural "N": three nodes and links
    ImVec2 a(p.x + s * 0.3f, p.y + s * 0.72f), b(p.x + s * 0.3f, p.y + s * 0.28f), c(p.x + s * 0.7f, p.y + s * 0.72f), d(p.x + s * 0.7f, p.y + s * 0.28f);
    const float t = std::max(1.5f, s * 0.08f);
    ImU32 ink = col::Bg0;
    dl->AddLine(a, b, ink, t);
    dl->AddLine(b, c, ink, t);
    dl->AddLine(c, d, ink, t);
    for (ImVec2 n : {a, b, c, d}) dl->AddCircleFilled(n, s * 0.085f, ink, 16);
}
} // namespace

bool UiApp::create(Settings* settings, UiModel* model, UiActions* actions, MessageHook hook) {
    settings_ = settings;
    model_ = model;
    actions_ = actions;
    hook_ = std::move(hook);
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(9, 12, 17));
    wc.lpszClassName = kMainClass;
    RegisterClassExW(&wc);

    // Size relative to the primary monitor work area
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const UINT sysDpi = GetDpiForSystem();
    dpiScale_ = sysDpi / 96.0f;
    int w = std::min(int(1300 * dpiScale_), int((work.right - work.left) * 0.9));
    int h = std::min(int(860 * dpiScale_), int((work.bottom - work.top) * 0.9));
    int x = work.left + ((work.right - work.left) - w) / 2, y = work.top + ((work.bottom - work.top) - h) / 2;
    hwnd_ = CreateWindowExW(WS_EX_APPWINDOW, kMainClass, L"Better GFN Neural", WS_OVERLAPPEDWINDOW, x, y, w, h, nullptr, nullptr, inst, this);
    if (!hwnd_) {
        BGN_LOG_ERROR("UI", "main window creation failed: {}", lastErrorString());
        return false;
    }
    dpiScale_ = GetDpiForWindow(hwnd_) / 96.0f;
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    int corner = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    MARGINS margins{0, 0, 0, 1};
    DwmExtendFrameIntoClientArea(hwnd_, &margins);
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    if (!createDevice()) return false;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui_ImplWin32_Init(hwnd_);
    ImGui_ImplDX11_Init(device_.Get(), context_.Get());
    loadFonts();
    applyDpi(dpiScale_);
    imguiReady_ = true;
    return true;
}

void UiApp::destroy() {
    if (imguiReady_) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        imguiReady_ = false;
    }
    rtv_.Reset();
    swapChain_.Reset();
    context_.Reset();
    device_.Reset();
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

bool UiApp::createDevice() {
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels),
                                   D3D11_SDK_VERSION, &device_, nullptr, &context_);
    if (FAILED(hr))
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                               &device_, nullptr, &context_);
    if (FAILED(hr)) {
        BGN_LOG_ERROR("UI", "UI device creation failed: {}", hrToString(hr));
        return false;
    }
    ComPtr<IDXGIDevice> dxgiDev;
    device_.As(&dxgiDev);
    ComPtr<IDXGIAdapter> adapter;
    dxgiDev->GetAdapter(&adapter);
    ComPtr<IDXGIFactory2> factory;
    adapter->GetParent(IID_PPV_ARGS(&factory));
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    width_ = std::max<int>(1, rc.right);
    height_ = std::max<int>(1, rc.bottom);
    DXGI_SWAP_CHAIN_DESC1 d{};
    d.Width = UINT(width_);
    d.Height = UINT(height_);
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d.BufferCount = 2;
    d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    d.Scaling = DXGI_SCALING_NONE;
    hr = factory->CreateSwapChainForHwnd(device_.Get(), hwnd_, &d, nullptr, nullptr, &swapChain_);
    if (FAILED(hr)) {
        d.Scaling = DXGI_SCALING_STRETCH;
        hr = factory->CreateSwapChainForHwnd(device_.Get(), hwnd_, &d, nullptr, nullptr, &swapChain_);
    }
    if (FAILED(hr)) {
        BGN_LOG_ERROR("UI", "UI swap chain creation failed: {}", hrToString(hr));
        return false;
    }
    factory->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
    createRtv();
    return true;
}

void UiApp::createRtv() {
    rtv_.Reset();
    ComPtr<ID3D11Texture2D> bb;
    if (SUCCEEDED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&bb)))) device_->CreateRenderTargetView(bb.Get(), nullptr, &rtv_);
}

void UiApp::resizeBuffers(int w, int h) {
    if (!swapChain_ || w <= 0 || h <= 0) return;
    rtv_.Reset();
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    swapChain_->ResizeBuffers(0, UINT(w), UINT(h), DXGI_FORMAT_UNKNOWN, 0);
    width_ = w;
    height_ = h;
    createRtv();
}

void UiApp::applyDpi(float scale) {
    dpiScale_ = scale;
    applyStyle(scale);
}

void UiApp::show() {
    if (!hwnd_) return;
    ShowWindow(hwnd_, IsIconic(hwnd_) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(hwnd_);
}

void UiApp::hide() {
    if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
}

bool UiApp::visible() const { return hwnd_ && IsWindowVisible(hwnd_) && !IsIconic(hwnd_); }

void UiApp::setFirstRun(bool on) {
    firstRun_ = on;
    firstRunStart_ = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
}

LRESULT CALLBACK UiApp::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    UiApp* self = reinterpret_cast<UiApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<UiApp*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    }
    if (self) return self->handle(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT UiApp::hitTest(int sx, int sy) {
    POINT pt{sx, sy};
    ScreenToClient(hwnd_, &pt);
    RECT rc;
    GetClientRect(hwnd_, &rc);
    const int border = int(6 * dpiScale_);
    if (!IsZoomed(hwnd_)) {
        bool left = pt.x < border, right = pt.x >= rc.right - border, top = pt.y < border, bottom = pt.y >= rc.bottom - border;
        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;
    }
    if (pt.y < LONG(titleHeight_)) {
        if (PtInRect(&minBtn_, pt) || PtInRect(&closeBtn_, pt)) return HTCLIENT;
        return HTCAPTION;
    }
    return HTCLIENT;
}

LRESULT UiApp::handle(UINT msg, WPARAM wp, LPARAM lp) {
    if (hook_) {
        LRESULT r = 0;
        if (hook_(hwnd_, msg, wp, lp, r)) return r;
    }
    if (imguiReady_ && ImGui_ImplWin32_WndProcHandler(hwnd_, msg, wp, lp)) return 1;
    switch (msg) {
    case WM_NCCALCSIZE:
        if (wp == TRUE) {
            auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
            if (IsZoomed(hwnd_)) {
                // Maximized windows extend past the monitor by the frame thickness
                const int fx = GetSystemMetricsForDpi(SM_CXFRAME, GetDpiForWindow(hwnd_)) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, GetDpiForWindow(hwnd_));
                const int fy = GetSystemMetricsForDpi(SM_CYFRAME, GetDpiForWindow(hwnd_)) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, GetDpiForWindow(hwnd_));
                p->rgrc[0].left += fx;
                p->rgrc[0].right -= fx;
                p->rgrc[0].top += fy;
                p->rgrc[0].bottom -= fy;
            }
            return 0;
        }
        break;
    case WM_NCHITTEST: return hitTest(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
    case WM_NCACTIVATE: return DefWindowProcW(hwnd_, msg, wp, -1); // keep custom frame from repainting
    case WM_GETMINMAXINFO: {
        auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
        mm->ptMinTrackSize.x = LONG(1040 * dpiScale_);
        mm->ptMinTrackSize.y = LONG(700 * dpiScale_);
        return 0;
    }
    case WM_SIZE:
        minimized_ = wp == SIZE_MINIMIZED;
        if (!minimized_) resizeBuffers(LOWORD(lp), HIWORD(lp));
        return 0;
    case WM_DPICHANGED: {
        const RECT* r = reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        applyDpi(HIWORD(wp) / 96.0f);
        return 0;
    }
    case WM_CLOSE:
        if (settings_ && settings_->closeToTray) {
            hide();
        } else if (actions_ && actions_->quit) {
            actions_->quit();
        }
        return 0;
    case WM_ERASEBKGND: return 1;
    default: break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

void UiApp::drawTitleBar(float width) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float s = dpiScale_;
    titleHeight_ = 46 * s;
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(width, titleHeight_), col::Bg1);
    dl->AddLine(ImVec2(0, titleHeight_ - 1), ImVec2(width, titleHeight_ - 1), col::Border);
    drawLogo(dl, ImVec2(16 * s, 11 * s), 24 * s);
    ImGui::PushFont(fonts().semibold, 14.5f);
    dl->AddText(ImVec2(50 * s, (titleHeight_ - ImGui::GetFontSize()) * 0.5f), col::Text, "Better GFN Neural");
    float tx = 50 * s + ImGui::CalcTextSize("Better GFN Neural").x + 10 * s;
    ImGui::PopFont();
    ImGui::PushFont(fonts().regular, kFontLabel);
    std::string ver = std::string("v") + kVersionString + "  \xC2\xB7  Unofficial";
    dl->AddText(ImVec2(tx, (titleHeight_ - ImGui::GetFontSize()) * 0.5f + 1), col::TextMute, ver.c_str());
    ImGui::PopFont();

    // Window buttons
    const float bw = 46 * s;
    auto button = [&](const char* id, float x, Icon icon, bool danger, RECT& rect) {
        ImGui::SetCursorPos(ImVec2(x, 0));
        bool pressed = ImGui::InvisibleButton(id, ImVec2(bw, titleHeight_ - 1));
        bool hov = ImGui::IsItemHovered();
        if (hov) dl->AddRectFilled(ImVec2(x, 0), ImVec2(x + bw, titleHeight_ - 1), danger ? col::Error : col::CardHover);
        drawIcon(dl, icon, ImVec2(x + bw * 0.5f, titleHeight_ * 0.5f), 14 * s, hov && danger ? col::Text : col::TextDim);
        rect = RECT{LONG(x), 0, LONG(x + bw), LONG(titleHeight_)};
        return pressed;
    };
    if (button("##close", width - bw, Icon::Close, true, closeBtn_)) SendMessageW(hwnd_, WM_CLOSE, 0, 0);
    if (button("##min", width - 2 * bw, Icon::Minimize, false, minBtn_)) ShowWindow(hwnd_, SW_MINIMIZE);
}

void UiApp::drawSidebar(float height) {
    const float s = dpiScale_;
    const float w = 220 * s;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(0, titleHeight_), ImVec2(w, height), col::Bg1);
    dl->AddLine(ImVec2(w - 1, titleHeight_), ImVec2(w - 1, height), col::Border);
    struct Item {
        Page page;
        const char* label;
        Icon icon;
    } items[] = {{Page::Home, "Home", Icon::Home},          {Page::Enhancement, "Enhancement", Icon::Sliders}, {Page::Display, "Display", Icon::Monitor},
                 {Page::Games, "Games", Icon::Gamepad},     {Page::Performance, "Performance", Icon::Chart},   {Page::Benchmark, "Benchmark", Icon::Gauge},
                 {Page::Settings, "Settings", Icon::Gear}};
    float y = titleHeight_ + 18 * s;
    for (const auto& it : items) {
        ImGui::SetCursorPos(ImVec2(12 * s, y));
        ImGui::PushID(it.label);
        bool clicked = ImGui::InvisibleButton("nav", ImVec2(w - 24 * s, 42 * s));
        bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        if (clicked) page_ = it.page;
        const bool sel = page_ == it.page;
        ImVec2 a(12 * s, y), b(w - 12 * s, y + 42 * s);
        if (sel) {
            dl->AddRectFilled(a, b, withAlpha(col::Accent, 0.12f), 10 * s);
            dl->AddRectFilled(ImVec2(a.x, a.y + 10 * s), ImVec2(a.x + 3 * s, b.y - 10 * s), col::Accent, 2 * s);
        } else if (hov) {
            dl->AddRectFilled(a, b, col::CardHover, 10 * s);
        }
        drawIcon(dl, it.icon, ImVec2(a.x + 24 * s, a.y + 21 * s), 18 * s, sel ? col::Accent : col::TextDim);
        ImGui::PushFont(sel ? fonts().semibold : fonts().regular, kFontBody);
        dl->AddText(ImVec2(a.x + 46 * s, a.y + (42 * s - ImGui::GetFontSize()) * 0.5f), sel ? col::Text : col::TextDim, tr(it.label));
        ImGui::PopFont();
        y += 48 * s;
    }
    // Bottom status
    const GfnState st = model_->gfn.state;
    const bool enh = model_->engine.overlayVisible;
    ImU32 c = enh ? col::Enhancing : (st == GfnState::NotRunning ? col::Waiting : col::Connected);
    const char* txt = enh ? tr("Enhancing") : (st == GfnState::NotRunning ? tr("Waiting") : tr("Connected"));
    ImVec2 p(24 * s, height - 54 * s);
    dl->AddCircleFilled(ImVec2(p.x + 5 * s, p.y + 9 * s), 5 * s, c);
    if (enh) dl->AddCircle(ImVec2(p.x + 5 * s, p.y + 9 * s), (7 + 3 * float(std::fmod(ImGui::GetTime(), 1.0))) * s, withAlpha(c, 0.5f));
    ImGui::PushFont(fonts().semibold, kFontSmall);
    dl->AddText(ImVec2(p.x + 18 * s, p.y + 1 * s), col::Text, txt);
    ImGui::PopFont();
    ImGui::PushFont(fonts().regular, kFontLabel);
    dl->AddText(ImVec2(p.x + 18 * s, p.y + 20 * s), col::TextMute, model_->engine.gpuName.empty() ? "" : model_->engine.gpuFamily.c_str());
    ImGui::PopFont();
}

void UiApp::buildUi(float width, float height) {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoScrollWithMouse);
    drawTitleBar(width);
    drawSidebar(height);
    const float s = dpiScale_;
    const float sideW = 220 * s;
    ImGui::SetCursorPos(ImVec2(sideW, titleHeight_));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(26 * s, 22 * s));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, col::Bg0);
    ImGui::BeginChild("##content", ImVec2(width - sideW, height - titleHeight_), ImGuiChildFlags_AlwaysUseWindowPadding, 0);
    PageContext ctx{*settings_, *model_, *actions_, page_};
    if (!model_->notice.empty()) {
        beginCard("notice", ImVec2(ImGui::GetContentRegionAvail().x, 0));
        textColored(col::Warn, model_->notice.c_str(), kFontBody, true);
        endCard();
    }
    switch (page_) {
    case Page::Home: drawHomePage(ctx); break;
    case Page::Enhancement: drawEnhancementPage(ctx); break;
    case Page::Display: drawDisplayPage(ctx); break;
    case Page::Games: drawGamesPage(ctx); break;
    case Page::Performance: drawPerformancePage(ctx); break;
    case Page::Benchmark: drawBenchmarkPage(ctx); break;
    case Page::Settings: drawSettingsPage(ctx); break;
    case Page::Count: break;
    }
    ImGui::Dummy(ImVec2(0, 10 * s));
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::End();
    if (firstRun_) {
        firstRun_ = drawFirstRun(ctx, float(ImGui::GetTime() - firstRunStart_));
    }
}

void UiApp::frame() {
    if (!imguiReady_ || !visible() || minimized_ || !rtv_) return;
    // Lower the redraw rate when the window is in the background
    const bool active = GetForegroundWindow() == hwnd_;
    const double now = qpcSeconds();
    if (!active && now - lastFrame_ < 1.0 / 20.0) {
        Sleep(5);
        return;
    }
    lastFrame_ = now;
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    buildUi(io.DisplaySize.x, io.DisplaySize.y);
    ImGui::Render();
    const float clear[4] = {9 / 255.0f, 12 / 255.0f, 17 / 255.0f, 1.0f};
    ID3D11RenderTargetView* rtv = rtv_.Get();
    context_->OMSetRenderTargets(1, &rtv, nullptr);
    context_->ClearRenderTargetView(rtv, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    swapChain_->Present(1, 0);
}

bool UiApp::renderToPng(const std::filesystem::path& file, int width, int height, Page page, bool firstRun) {
    if (!imguiReady_) return false;
    D3D11_TEXTURE2D_DESC d{};
    d.Width = UINT(width);
    d.Height = UINT(height);
    d.MipLevels = d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> tex;
    ComPtr<ID3D11RenderTargetView> rtv;
    if (FAILED(device_->CreateTexture2D(&d, nullptr, &tex)) || FAILED(device_->CreateRenderTargetView(tex.Get(), nullptr, &rtv))) return false;
    Page savedPage = page_;
    bool savedFirst = firstRun_;
    page_ = page;
    if (firstRun) setFirstRun(true);
    ImGuiIO& io = ImGui::GetIO();
    for (int i = 0; i < 90; ++i) { // settle animations / font atlas uploads
        ImGui_ImplDX11_NewFrame();
        io.DisplaySize = ImVec2(float(width), float(height));
        io.DeltaTime = 1.0f / 30.0f;
        io.MousePos = ImVec2(-1, -1);
        ImGui::NewFrame();
        buildUi(float(width), float(height));
        ImGui::Render();
        const float clear[4] = {9 / 255.0f, 12 / 255.0f, 17 / 255.0f, 1.0f};
        ID3D11RenderTargetView* r = rtv.Get();
        context_->OMSetRenderTargets(1, &r, nullptr);
        context_->ClearRenderTargetView(r, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }
    page_ = savedPage;
    firstRun_ = savedFirst;
    D3D11_TEXTURE2D_DESC sd = d;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device_->CreateTexture2D(&sd, nullptr, &staging))) return false;
    context_->CopyResource(staging.Get(), tex.Get());
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) return false;
    std::vector<unsigned char> rgba(size_t(width) * height * 4);
    for (int y = 0; y < height; ++y) memcpy(&rgba[size_t(y) * width * 4], static_cast<unsigned char*>(m.pData) + size_t(y) * m.RowPitch, size_t(width) * 4);
    context_->Unmap(staging.Get(), 0);
    for (size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 255;
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    return stbi_write_png(file.string().c_str(), width, height, 4, rgba.data(), width * 4) != 0;
}

} // namespace bgn
