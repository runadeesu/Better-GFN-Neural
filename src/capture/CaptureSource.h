#pragma once
// Capture source interface. Implementations deliver GPU textures on the
// processing device; nothing is copied through system memory.

#include <cstdint>
#include <string>

#include "renderer/GpuDevice.h"

namespace bgn {

struct CaptureOptions {
    bool hdr = false;       // request FP16 scRGB surfaces (HDR desktop)
    bool cursor = false;    // include the mouse cursor in the captured image
    bool borderless = true; // hide the yellow capture border where supported
};

struct CapturedFrame {
    ID3D11Texture2D* texture = nullptr; // borrowed until releaseFrame()
    int contentW = 0, contentH = 0;     // valid region of the texture
    RECT crop{};                        // game client area within the texture
    double timestamp = 0;               // seconds (QPC timebase)
    bool hdr = false;
    uint64_t id = 0;
};

class CaptureSource {
public:
    virtual ~CaptureSource() = default;
    virtual const char* name() const = 0;
    virtual bool start(GpuDevice& device, HWND window, const CaptureOptions& opt) = 0;
    virtual void stop() = 0;
    // Event signalled when a new frame is available (nullptr => poll with acquire timeout).
    virtual HANDLE frameEvent() const = 0;
    // Gets the newest frame (older queued frames are dropped). False if no new frame.
    virtual bool acquire(CapturedFrame& out, unsigned timeoutMs) = 0;
    virtual void releaseFrame() = 0;
    virtual bool failed() const = 0;
    virtual std::string error() const = 0;
    virtual uint64_t droppedFrames() const = 0;
    virtual void setCursorCapture(bool enable) = 0;
};

// Computes the client-area rectangle of |window| relative to its DWM frame
// bounds (the origin of a Windows.Graphics.Capture window frame).
RECT clientCropInWindowCapture(HWND window);
// Client area of |window| relative to the monitor's top-left corner.
RECT clientRectOnMonitor(HWND window, const RECT& monitorRect);

} // namespace bgn
