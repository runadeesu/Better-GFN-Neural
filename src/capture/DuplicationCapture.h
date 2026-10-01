#pragma once
// DXGI Desktop Duplication fallback: duplicates the monitor that shows the
// GFN window and crops the client area on the GPU. Used when Windows Graphics
// Capture is unavailable. Our own overlay excludes itself from capture
// (WDA_EXCLUDEFROMCAPTURE) to avoid feedback.

#include <dxgi1_6.h>

#include "capture/CaptureSource.h"

namespace bgn {

class DuplicationCapture final : public CaptureSource {
public:
    const char* name() const override { return "DXGI Desktop Duplication"; }
    bool start(GpuDevice& device, HWND window, const CaptureOptions& opt) override;
    void stop() override;
    HANDLE frameEvent() const override { return nullptr; }
    bool acquire(CapturedFrame& out, unsigned timeoutMs) override;
    void releaseFrame() override;
    bool failed() const override { return failed_; }
    std::string error() const override { return error_; }
    uint64_t droppedFrames() const override { return dropped_; }
    void setCursorCapture(bool) override {}

private:
    bool reinit();
    GpuDevice* device_ = nullptr;
    HWND window_ = nullptr;
    HMONITOR monitor_ = nullptr;
    RECT monitorRect_{};
    CaptureOptions opt_;
    ComPtr<IDXGIOutputDuplication> dup_;
    ComPtr<ID3D11Texture2D> current_;
    bool holding_ = false;
    bool failed_ = false;
    bool hdr_ = false;
    std::string error_;
    uint64_t dropped_ = 0, nextId_ = 0;
};

} // namespace bgn
