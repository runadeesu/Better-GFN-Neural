#pragma once
// Windows.Graphics.Capture window capture (Windows 10 1903+). Frames arrive as
// D3D11 textures created on our device (zero CPU copy).

#include <atomic>
#include <memory>

#include "capture/CaptureSource.h"

namespace bgn {

class WgcCapture final : public CaptureSource {
public:
    WgcCapture();
    ~WgcCapture() override;
    static bool isSupported();

    const char* name() const override { return "Windows Graphics Capture"; }
    bool start(GpuDevice& device, HWND window, const CaptureOptions& opt) override;
    void stop() override;
    HANDLE frameEvent() const override { return event_.get(); }
    bool acquire(CapturedFrame& out, unsigned timeoutMs) override;
    void releaseFrame() override;
    bool failed() const override { return failed_.load(); }
    std::string error() const override { return error_; }
    uint64_t droppedFrames() const override { return dropped_; }
    void setCursorCapture(bool enable) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    UniqueHandle event_;
    std::atomic<bool> failed_{false};
    std::string error_;
    uint64_t dropped_ = 0;
    uint64_t nextId_ = 0;
    HWND window_ = nullptr;
};

} // namespace bgn
