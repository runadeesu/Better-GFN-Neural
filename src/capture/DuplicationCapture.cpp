#include "capture/DuplicationCapture.h"

#include <algorithm>

#include "core/Log.h"
#include "platform/Win32.h"

namespace bgn {

bool DuplicationCapture::start(GpuDevice& device, HWND window, const CaptureOptions& opt) {
    stop();
    device_ = &device;
    window_ = window;
    opt_ = opt;
    failed_ = false;
    error_.clear();
    return reinit();
}

bool DuplicationCapture::reinit() {
    dup_.Reset();
    monitor_ = MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST);
    ComPtr<IDXGIOutput> output;
    for (UINT i = 0; device_->adapter()->EnumOutputs(i, &output) != DXGI_ERROR_NOT_FOUND; ++i, output.Reset()) {
        DXGI_OUTPUT_DESC d{};
        output->GetDesc(&d);
        if (d.Monitor == monitor_) {
            monitorRect_ = d.DesktopCoordinates;
            break;
        }
    }
    if (!output) {
        error_ = "the GFN monitor is driven by a different GPU (Desktop Duplication needs the same adapter)";
        failed_ = true;
        return false;
    }
    HRESULT hr = E_FAIL;
    ComPtr<IDXGIOutput5> o5;
    if (SUCCEEDED(output.As(&o5))) {
        const DXGI_FORMAT formats[] = {opt_.hdr ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM};
        hr = o5->DuplicateOutput1(device_->device(), 0, 2, formats, &dup_);
    }
    if (FAILED(hr)) {
        ComPtr<IDXGIOutput1> o1;
        if (SUCCEEDED(output.As(&o1))) hr = o1->DuplicateOutput(device_->device(), &dup_);
    }
    if (FAILED(hr)) {
        error_ = "Desktop Duplication unavailable: " + hrToString(hr);
        failed_ = true;
        return false;
    }
    DXGI_OUTDUPL_DESC dd{};
    dup_->GetDesc(&dd);
    hdr_ = dd.ModeDesc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT;
    BGN_LOG_INFO("Capture", "Desktop Duplication started ({}x{}, {})", dd.ModeDesc.Width, dd.ModeDesc.Height, hdr_ ? "FP16" : "BGRA8");
    return true;
}

void DuplicationCapture::stop() {
    releaseFrame();
    dup_.Reset();
}

bool DuplicationCapture::acquire(CapturedFrame& out, unsigned timeoutMs) {
    if (!dup_ || failed_) return false;
    releaseFrame();
    DXGI_OUTDUPL_FRAME_INFO info{};
    ComPtr<IDXGIResource> res;
    HRESULT hr = dup_->AcquireNextFrame(timeoutMs, &info, &res);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) return false;
    if (hr == DXGI_ERROR_ACCESS_LOST) {
        // Mode change, UAC prompt, fullscreen app switch: rebuild the duplication.
        BGN_LOG_INFO("Capture", "desktop duplication access lost, re-initializing");
        if (!reinit()) return false;
        return false;
    }
    if (FAILED(hr)) {
        error_ = "AcquireNextFrame failed: " + hrToString(hr);
        failed_ = true;
        return false;
    }
    holding_ = true;
    if (info.LastPresentTime.QuadPart == 0) {
        // Only the mouse pointer changed: no new image
        releaseFrame();
        return false;
    }
    if (info.AccumulatedFrames > 1) dropped_ += info.AccumulatedFrames - 1;
    if (FAILED(res.As(&current_))) {
        releaseFrame();
        return false;
    }
    D3D11_TEXTURE2D_DESC td{};
    current_->GetDesc(&td);
    out.texture = current_.Get();
    out.contentW = int(td.Width);
    out.contentH = int(td.Height);
    out.crop = clientRectOnMonitor(window_, monitorRect_);
    out.crop.left = std::clamp<LONG>(out.crop.left, 0, LONG(td.Width));
    out.crop.top = std::clamp<LONG>(out.crop.top, 0, LONG(td.Height));
    out.crop.right = std::clamp<LONG>(out.crop.right, out.crop.left, LONG(td.Width));
    out.crop.bottom = std::clamp<LONG>(out.crop.bottom, out.crop.top, LONG(td.Height));
    out.timestamp = qpcToSeconds(info.LastPresentTime.QuadPart);
    out.hdr = hdr_;
    out.id = ++nextId_;
    return true;
}

void DuplicationCapture::releaseFrame() {
    current_.Reset();
    if (holding_ && dup_) dup_->ReleaseFrame();
    holding_ = false;
}

} // namespace bgn
