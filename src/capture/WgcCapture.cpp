#include "capture/WgcCapture.h"

#include <unknwn.h>
#include <inspectable.h>
#include <dwmapi.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Graphics.DirectX.h>

#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <algorithm>
#include <chrono>

#include "core/Log.h"
#include "core/StringUtil.h"

namespace bgn {

namespace wgc = winrt::Windows::Graphics::Capture;
namespace wgd = winrt::Windows::Graphics::DirectX;

RECT clientCropInWindowCapture(HWND window) {
    RECT frame{};
    if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame)))) GetWindowRect(window, &frame);
    RECT client{};
    GetClientRect(window, &client);
    POINT origin{0, 0};
    ClientToScreen(window, &origin);
    RECT crop;
    crop.left = origin.x - frame.left;
    crop.top = origin.y - frame.top;
    crop.right = crop.left + (client.right - client.left);
    crop.bottom = crop.top + (client.bottom - client.top);
    return crop;
}

RECT clientRectOnMonitor(HWND window, const RECT& monitorRect) {
    RECT client{};
    GetClientRect(window, &client);
    POINT origin{0, 0};
    ClientToScreen(window, &origin);
    RECT r;
    r.left = origin.x - monitorRect.left;
    r.top = origin.y - monitorRect.top;
    r.right = r.left + (client.right - client.left);
    r.bottom = r.top + (client.bottom - client.top);
    return r;
}

struct WgcCapture::Impl {
    wgd::Direct3D11::IDirect3DDevice device{nullptr};
    wgc::GraphicsCaptureItem item{nullptr};
    wgc::Direct3D11CaptureFramePool pool{nullptr};
    wgc::GraphicsCaptureSession session{nullptr};
    wgc::Direct3D11CaptureFramePool::FrameArrived_revoker frameArrived;
    wgc::GraphicsCaptureItem::Closed_revoker closed;
    wgc::Direct3D11CaptureFrame current{nullptr};
    ComPtr<ID3D11Texture2D> currentTex;
    winrt::Windows::Graphics::SizeInt32 poolSize{};
    wgd::DirectXPixelFormat format = wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized;
    bool hdr = false;
};

WgcCapture::WgcCapture() : event_(CreateEventW(nullptr, FALSE, FALSE, nullptr)) {}

WgcCapture::~WgcCapture() { stop(); }

bool WgcCapture::isSupported() {
    try {
        return wgc::GraphicsCaptureSession::IsSupported();
    } catch (...) {
        return false;
    }
}

bool WgcCapture::start(GpuDevice& device, HWND window, const CaptureOptions& opt) {
    stop();
    failed_ = false;
    error_.clear();
    window_ = window;
    if (!isSupported()) {
        error_ = "Windows Graphics Capture is not supported on this system";
        failed_ = true;
        return false;
    }
    impl_ = std::make_unique<Impl>();
    try {
        ComPtr<IDXGIDevice> dxgi;
        winrt::check_hresult(device.device()->QueryInterface(IID_PPV_ARGS(&dxgi)));
        winrt::com_ptr<::IInspectable> insp;
        winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), insp.put()));
        impl_->device = insp.as<wgd::Direct3D11::IDirect3DDevice>();

        auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        winrt::check_hresult(interop->CreateForWindow(window, winrt::guid_of<wgc::GraphicsCaptureItem>(), winrt::put_abi(impl_->item)));

        impl_->hdr = opt.hdr;
        impl_->format = opt.hdr ? wgd::DirectXPixelFormat::R16G16B16A16Float : wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized;
        impl_->poolSize = impl_->item.Size();
        impl_->pool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(impl_->device, impl_->format, 2, impl_->poolSize);
        impl_->session = impl_->pool.CreateCaptureSession(impl_->item);
        HANDLE ev = event_.get();
        impl_->frameArrived = impl_->pool.FrameArrived(winrt::auto_revoke, [ev](auto&&, auto&&) { SetEvent(ev); });
        impl_->closed = impl_->item.Closed(winrt::auto_revoke, [this](auto&&, auto&&) { failed_ = true; });
        try {
            impl_->session.IsCursorCaptureEnabled(opt.cursor);
        } catch (...) {
            BGN_LOG_DEBUG("Capture", "cursor capture toggle not supported on this Windows build");
        }
        if (opt.borderless) {
            try {
                impl_->session.IsBorderRequired(false);
            } catch (...) {
                BGN_LOG_DEBUG("Capture", "borderless capture not supported on this Windows build");
            }
        }
#ifdef BGN_HAS_WGC_MIN_UPDATE_INTERVAL
        try {
            impl_->session.MinUpdateInterval(std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(std::chrono::milliseconds(1)));
        } catch (...) {
        }
#endif
        impl_->session.StartCapture();
    } catch (const winrt::hresult_error& e) {
        error_ = "Windows Graphics Capture failed: " + narrow(std::wstring_view(e.message())) + " (" + hrToString(e.code()) + ")";
        BGN_LOG_ERROR("Capture", "{}", error_);
        failed_ = true;
        impl_.reset();
        return false;
    }
    BGN_LOG_INFO("Capture", "WGC started ({}x{}, {})", impl_->poolSize.Width, impl_->poolSize.Height, opt.hdr ? "FP16 HDR" : "BGRA8");
    return true;
}

void WgcCapture::stop() {
    if (!impl_) return;
    try {
        impl_->frameArrived.revoke();
        impl_->closed.revoke();
        if (impl_->current) impl_->current.Close();
        impl_->currentTex.Reset();
        if (impl_->session) impl_->session.Close();
        if (impl_->pool) impl_->pool.Close();
    } catch (...) {
    }
    impl_.reset();
}

bool WgcCapture::acquire(CapturedFrame& out, unsigned timeoutMs) {
    if (!impl_ || failed_) return false;
    if (timeoutMs && WaitForSingleObject(event_.get(), timeoutMs) != WAIT_OBJECT_0) {
        // fall through: a frame may still be queued
    }
    try {
        wgc::Direct3D11CaptureFrame last{nullptr};
        while (auto f = impl_->pool.TryGetNextFrame()) {
            if (last) {
                last.Close();
                ++dropped_;
            }
            last = f;
        }
        if (!last) return false;
        auto size = last.ContentSize();
        if (size.Width != impl_->poolSize.Width || size.Height != impl_->poolSize.Height) {
            impl_->poolSize = size;
            impl_->pool.Recreate(impl_->device, impl_->format, 2, size);
        }
        auto access = last.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        ComPtr<ID3D11Texture2D> tex;
        winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(&tex)));
        impl_->current = last;
        impl_->currentTex = tex;
        out.texture = tex.Get();
        out.contentW = size.Width;
        out.contentH = size.Height;
        out.crop = clientCropInWindowCapture(window_);
        out.crop.left = std::clamp<LONG>(out.crop.left, 0, size.Width);
        out.crop.top = std::clamp<LONG>(out.crop.top, 0, size.Height);
        out.crop.right = std::clamp<LONG>(out.crop.right, out.crop.left, size.Width);
        out.crop.bottom = std::clamp<LONG>(out.crop.bottom, out.crop.top, size.Height);
        out.timestamp = double(last.SystemRelativeTime().count()) * 1e-7;
        out.hdr = impl_->hdr;
        out.id = ++nextId_;
        return true;
    } catch (const winrt::hresult_error& e) {
        error_ = "capture frame error: " + hrToString(e.code());
        failed_ = true;
        return false;
    }
}

void WgcCapture::releaseFrame() {
    if (!impl_) return;
    try {
        impl_->currentTex.Reset();
        if (impl_->current) {
            impl_->current.Close();
            impl_->current = nullptr;
        }
    } catch (...) {
    }
}

void WgcCapture::setCursorCapture(bool enable) {
    if (!impl_ || !impl_->session) return;
    try {
        impl_->session.IsCursorCaptureEnabled(enable);
    } catch (...) {
    }
}

} // namespace bgn
