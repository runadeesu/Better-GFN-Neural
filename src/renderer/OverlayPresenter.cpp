#include "renderer/OverlayPresenter.h"

#include <algorithm>

#include "core/Log.h"

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

namespace bgn {

namespace {
const wchar_t* kOverlayClass = L"BetterGFNNeural.Overlay";

LRESULT CALLBACK overlayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_NCHITTEST: return HTTRANSPARENT;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_CLOSE: return 0; // only we close it
    default: return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

void registerClassOnce() {
    static bool done = false;
    if (done) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = overlayProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kOverlayClass;
    wc.hCursor = nullptr; // never change the cursor (the window is click-through anyway)
    RegisterClassExW(&wc);
    done = true;
}
} // namespace

OverlayPresenter::~OverlayPresenter() { destroy(); }

bool OverlayPresenter::create(GpuDevice& device, bool excludeFromCapture) {
    destroy();
    device_ = &device;
    registerClassOnce();
    const DWORD ex = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOREDIRECTIONBITMAP;
    hwnd_ = CreateWindowExW(ex, kOverlayClass, L"Better GFN Neural Output", WS_POPUP, 0, 0, 64, 64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd_) {
        BGN_LOG_ERROR("Present", "overlay window creation failed: {}", lastErrorString());
        return false;
    }
    SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);
    if (excludeFromCapture) SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE);
    return true;
}

void OverlayPresenter::destroy() {
    rtv_.Reset();
    if (waitable_) {
        CloseHandle(waitable_);
        waitable_ = nullptr;
    }
    dcompVisual_.Reset();
    dcompTarget_.Reset();
    dcompDevice_.Reset();
    swapChain_.Reset();
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    width_ = height_ = 0;
    visible_ = false;
}

bool OverlayPresenter::createSwapChain(int w, int h) {
    DXGI_SWAP_CHAIN_DESC1 d{};
    d.Width = UINT(w);
    d.Height = UINT(h);
    d.Format = hdr_ ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R10G10B10A2_UNORM;
    d.SampleDesc.Count = 1;
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d.BufferCount = 3;
    d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    d.Scaling = DXGI_SCALING_STRETCH;
    d.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    flags_ = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    d.Flags = flags_;

    ComPtr<IDXGISwapChain1> sc1;
    HRESULT hr = E_FAIL;
    // Preferred: DirectComposition (designed for layered / click-through windows)
    ComPtr<IDXGIDevice> dxgiDev;
    device_->device()->QueryInterface(IID_PPV_ARGS(&dxgiDev));
    if (SUCCEEDED(DCompositionCreateDevice(dxgiDev.Get(), IID_PPV_ARGS(&dcompDevice_)))) {
        hr = device_->factory()->CreateSwapChainForComposition(device_->device(), &d, nullptr, &sc1);
        if (SUCCEEDED(hr)) {
            hr = dcompDevice_->CreateTargetForHwnd(hwnd_, TRUE, &dcompTarget_);
            if (SUCCEEDED(hr)) hr = dcompDevice_->CreateVisual(&dcompVisual_);
            if (SUCCEEDED(hr)) hr = dcompVisual_->SetContent(sc1.Get());
            if (SUCCEEDED(hr)) hr = dcompTarget_->SetRoot(dcompVisual_.Get());
            if (SUCCEEDED(hr)) hr = dcompDevice_->Commit();
        }
        if (FAILED(hr)) {
            BGN_LOG_WARN("Present", "DirectComposition swap chain failed ({}), falling back to HWND swap chain", hrToString(hr));
            sc1.Reset();
            dcompVisual_.Reset();
            dcompTarget_.Reset();
            dcompDevice_.Reset();
        }
    }
    if (!sc1) {
        d.Scaling = DXGI_SCALING_NONE;
        hr = device_->factory()->CreateSwapChainForHwnd(device_->device(), hwnd_, &d, nullptr, nullptr, &sc1);
        if (FAILED(hr)) {
            BGN_LOG_ERROR("Present", "CreateSwapChainForHwnd failed: {}", hrToString(hr));
            return false;
        }
        device_->factory()->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    }
    if (FAILED(sc1.As(&swapChain_))) return false;
    swapChain_->SetMaximumFrameLatency(lowLatency_ ? 1 : 2);
    waitable_ = swapChain_->GetFrameLatencyWaitableObject();
    ComPtr<IDXGISwapChain3> sc3;
    if (SUCCEEDED(swapChain_.As(&sc3)))
        sc3->SetColorSpace1(hdr_ ? DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
    width_ = w;
    height_ = h;
    createRtv();
    BGN_LOG_INFO("Present", "swap chain {}x{} {} via {}", w, h, hdr_ ? "FP16 scRGB (HDR)" : "10-bit SDR", dcompDevice_ ? "DirectComposition" : "HWND");
    return rtv_ != nullptr;
}

void OverlayPresenter::createRtv() {
    rtv_.Reset();
    ComPtr<ID3D11Texture2D> bb;
    if (FAILED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&bb)))) return;
    device_->device()->CreateRenderTargetView(bb.Get(), nullptr, &rtv_);
}

bool OverlayPresenter::resizeSwapChain(int w, int h) {
    rtv_.Reset();
    device_->context()->ClearState();
    device_->context()->Flush();
    HRESULT hr = swapChain_->ResizeBuffers(0, UINT(w), UINT(h), hdr_ ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R10G10B10A2_UNORM, flags_);
    if (FAILED(hr)) {
        BGN_LOG_WARN("Present", "ResizeBuffers failed ({}), recreating swap chain", hrToString(hr));
        return false;
    }
    ComPtr<IDXGISwapChain3> sc3;
    if (SUCCEEDED(swapChain_.As(&sc3)))
        sc3->SetColorSpace1(hdr_ ? DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
    width_ = w;
    height_ = h;
    createRtv();
    return rtv_ != nullptr;
}

bool OverlayPresenter::configure(const RECT& r, bool hdr, bool lowLatency) {
    if (!hwnd_) return false;
    const int w = std::max<int>(1, r.right - r.left), h = std::max<int>(1, r.bottom - r.top);
    if (r.left != rect_.left || r.top != rect_.top || r.right != rect_.right || r.bottom != rect_.bottom) {
        SetWindowPos(hwnd_, HWND_TOPMOST, r.left, r.top, w, h, SWP_NOACTIVATE | (visible_ ? SWP_SHOWWINDOW : 0));
        rect_ = r;
    }
    const bool formatChange = hdr != hdr_;
    if (lowLatency != lowLatency_ && swapChain_) {
        lowLatency_ = lowLatency;
        swapChain_->SetMaximumFrameLatency(lowLatency ? 1 : 2);
    }
    lowLatency_ = lowLatency;
    if (!swapChain_) {
        hdr_ = hdr;
        return createSwapChain(w, h);
    }
    if (formatChange || w != width_ || h != height_) {
        hdr_ = hdr;
        if (!resizeSwapChain(w, h)) {
            // Full re-creation
            rtv_.Reset();
            if (waitable_) {
                CloseHandle(waitable_);
                waitable_ = nullptr;
            }
            dcompVisual_.Reset();
            dcompTarget_.Reset();
            dcompDevice_.Reset();
            swapChain_.Reset();
            return createSwapChain(w, h);
        }
    }
    return rtv_ != nullptr;
}

void OverlayPresenter::setVisible(bool v) {
    if (!hwnd_ || v == visible_) return;
    visible_ = v;
    if (v) {
        SetWindowPos(hwnd_, HWND_TOPMOST, rect_.left, rect_.top, rect_.right - rect_.left, rect_.bottom - rect_.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    } else {
        ShowWindow(hwnd_, SW_HIDE);
    }
}

void OverlayPresenter::reassertTopmost() {
    if (hwnd_ && visible_) SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void OverlayPresenter::pumpMessages() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void OverlayPresenter::waitForFrameSlot(DWORD timeoutMs) {
    if (waitable_) WaitForSingleObjectEx(waitable_, timeoutMs, TRUE);
}

HRESULT OverlayPresenter::present(UINT syncInterval) {
    if (!swapChain_) return E_FAIL;
    DXGI_PRESENT_PARAMETERS pp{};
    return swapChain_->Present1(syncInterval, 0, &pp);
}

} // namespace bgn
