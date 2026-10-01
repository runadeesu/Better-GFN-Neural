#pragma once
// D3D11 device creation with adapter selection, capability probing and
// device-removed detection. One instance per thread that renders.

#include <d3d11_4.h>
#include <dxgi1_6.h>

#include <string>
#include <vector>

#include "platform/Win32.h"

namespace bgn {

struct GpuInfo {
    std::string name;
    std::string family;       // e.g. "NVIDIA GeForce RTX 40 Series"
    unsigned vendorId = 0;
    unsigned deviceId = 0;
    LUID luid{};
    double dedicatedVramMB = 0;
    double sharedMB = 0;
    bool software = false;     // WARP
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    bool halfPrecision = false; // min16float shaders preferred
    bool tearingSupported = false;
    std::string driverVersion;
};

struct GpuDeviceOptions {
    bool forceWarp = false;
    bool debugLayer = false;
    LUID preferredLuid{};      // 0 = high-performance adapter
};

class GpuDevice {
public:
    bool create(const GpuDeviceOptions& opt);
    void destroy();
    bool valid() const { return device_ != nullptr; }

    ID3D11Device1* device() const { return device_.Get(); }
    ID3D11DeviceContext1* context() const { return context_.Get(); }
    IDXGIAdapter1* adapter() const { return adapter_.Get(); }
    IDXGIFactory2* factory() const { return factory_.Get(); }
    const GpuInfo& info() const { return info_; }

    // Returns S_OK or the device removed reason.
    HRESULT removedReason() const;
    bool isRemoved() const { return FAILED(removedReason()); }

    // Process-local VRAM usage / budget from DXGI (MB). Returns false if unavailable.
    bool queryVideoMemory(double& usageMB, double& budgetMB) const;

private:
    ComPtr<IDXGIFactory2> factory_;
    ComPtr<IDXGIAdapter1> adapter_;
    ComPtr<ID3D11Device1> device_;
    ComPtr<ID3D11DeviceContext1> context_;
    GpuInfo info_;
};

// Lists hardware adapters (for diagnostics / UI)
std::vector<GpuInfo> enumerateGpus();

} // namespace bgn
