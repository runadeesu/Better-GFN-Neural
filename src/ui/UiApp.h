#pragma once
// Main application window: custom-drawn modern chrome, sidebar navigation and
// page content rendered with Dear ImGui on its own lightweight D3D11 device
// (independent from the video engine so the UI can never stall enhancement).

#include <d3d11.h>
#include <dxgi1_2.h>

#include <filesystem>
#include <functional>

#include "platform/Win32.h"
#include "ui/Pages.h"
#include "ui/UiModel.h"

namespace bgn {

class UiApp {
public:
    using MessageHook = std::function<bool(HWND, UINT, WPARAM, LPARAM, LRESULT&)>;

    bool create(Settings* settings, UiModel* model, UiActions* actions, MessageHook hook);
    void destroy();

    void show();
    void hide();
    bool visible() const;
    HWND hwnd() const { return hwnd_; }

    // Renders one frame when visible. Returns immediately when hidden.
    void frame();
    void setPage(ui::Page p) { page_ = p; }
    void setFirstRun(bool on);
    bool firstRunActive() const { return firstRun_; }

    // Renders a page off-screen and writes it as PNG (documentation/tests).
    bool renderToPng(const std::filesystem::path& file, int width, int height, ui::Page page, bool firstRun = false);

private:
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);
    bool createDevice();
    void createRtv();
    void resizeBuffers(int w, int h);
    void buildUi(float width, float height);
    void drawTitleBar(float width);
    void drawSidebar(float height);
    LRESULT hitTest(int x, int y);
    void applyDpi(float scale);

    HWND hwnd_ = nullptr;
    Settings* settings_ = nullptr;
    UiModel* model_ = nullptr;
    UiActions* actions_ = nullptr;
    MessageHook hook_;
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGISwapChain1> swapChain_;
    ComPtr<ID3D11RenderTargetView> rtv_;
    bool imguiReady_ = false;
    ui::Page page_ = ui::Page::Home;
    bool firstRun_ = false;
    double firstRunStart_ = 0;
    float dpiScale_ = 1.0f;
    int width_ = 0, height_ = 0;
    bool minimized_ = false;
    // Title bar interaction rects (client coords) for hit testing
    RECT minBtn_{}, closeBtn_{}, langBtn_{};
    float titleHeight_ = 46.0f;
    double lastFrame_ = 0;
};

} // namespace bgn
