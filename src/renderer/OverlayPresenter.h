#pragma once
// The output window: a borderless, topmost, click-through, non-activating
// overlay placed exactly over the GeForce NOW game area (or the whole monitor
// in fullscreen-upscale mode). Mouse, keyboard and controller input keep going
// to GeForce NOW. Presentation uses a DirectComposition flip-model swap chain
// (fallback: HWND flip-model swap chain) with a frame-latency waitable object.

#include <dcomp.h>
#include <dxgi1_6.h>

#include "renderer/GpuDevice.h"

namespace bgn {

class OverlayPresenter {
public:
    ~OverlayPresenter();
    bool create(GpuDevice& device, bool excludeFromCapture);
    void destroy();
    bool valid() const { return swapChain_ != nullptr; }

    // Positions the overlay (screen coordinates, physical pixels) and resizes the
    // swap chain if needed. |hdr| selects an FP16 scRGB swap chain.
    bool configure(const RECT& screenRect, bool hdr, bool lowLatency);
    void setVisible(bool visible);
    bool visible() const { return visible_; }
    void reassertTopmost();
    void pumpMessages();

    // Waits (up to timeout) until the swap chain can accept another frame.
    void waitForFrameSlot(DWORD timeoutMs);
    ID3D11RenderTargetView* backBufferRtv() const { return rtv_.Get(); }
    int width() const { return width_; }
    int height() const { return height_; }
    bool hdr() const { return hdr_; }
    HRESULT present(UINT syncInterval);
    HWND hwnd() const { return hwnd_; }
    bool usesComposition() const { return dcompDevice_ != nullptr; }

private:
    bool createSwapChain(int w, int h);
    bool resizeSwapChain(int w, int h);
    void createRtv();

    GpuDevice* device_ = nullptr;
    HWND hwnd_ = nullptr;
    ComPtr<IDXGISwapChain2> swapChain_;
    ComPtr<ID3D11RenderTargetView> rtv_;
    ComPtr<IDCompositionDevice> dcompDevice_;
    ComPtr<IDCompositionTarget> dcompTarget_;
    ComPtr<IDCompositionVisual> dcompVisual_;
    HANDLE waitable_ = nullptr;
    RECT rect_{};
    int width_ = 0, height_ = 0;
    bool hdr_ = false;
    bool lowLatency_ = true;
    bool visible_ = false;
    UINT flags_ = 0;
};

} // namespace bgn
