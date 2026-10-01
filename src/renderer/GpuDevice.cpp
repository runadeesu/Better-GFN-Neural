#include "renderer/GpuDevice.h"

#include <algorithm>
#include <format>
#include <vector>

#include "core/Log.h"
#include "core/StringUtil.h"
#include "settings/Presets.h"

namespace bgn {

static std::string driverVersionOf(IDXGIAdapter1* adapter) {
    LARGE_INTEGER v{};
    if (FAILED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &v))) return {};
    return std::format("{}.{}.{}.{}", HIWORD(v.HighPart), LOWORD(v.HighPart), HIWORD(v.LowPart), LOWORD(v.LowPart));
}

static GpuInfo describeAdapter(IDXGIAdapter1* adapter) {
    DXGI_ADAPTER_DESC1 d{};
    adapter->GetDesc1(&d);
    GpuInfo g;
    g.name = narrow(d.Description);
    g.vendorId = d.VendorId;
    g.deviceId = d.DeviceId;
    g.luid = d.AdapterLuid;
    g.dedicatedVramMB = double(d.DedicatedVideoMemory) / (1024.0 * 1024.0);
    g.sharedMB = double(d.SharedSystemMemory) / (1024.0 * 1024.0);
    g.software = (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0 || d.VendorId == 0x1414;
    g.family = gpuFamily(g.vendorId, g.name);
    g.halfPrecision = preferHalfPrecision(g.vendorId, g.name);
    g.driverVersion = driverVersionOf(adapter);
    return g;
}

std::vector<GpuInfo> enumerateGpus() {
    std::vector<GpuInfo> out;
    ComPtr<IDXGIFactory1> f;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&f)))) return out;
    ComPtr<IDXGIAdapter1> a;
    for (UINT i = 0; f->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; ++i, a.Reset()) {
        GpuInfo g = describeAdapter(a.Get());
        if (!g.software) out.push_back(g);
    }
    return out;
}

bool GpuDevice::create(const GpuDeviceOptions& opt) {
    destroy();
    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory_));
    if (FAILED(hr)) {
        BGN_LOG_ERROR("GPU", "CreateDXGIFactory2 failed: {}", hrToString(hr));
        return false;
    }
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    if (opt.debugLayer) flags |= D3D11_CREATE_DEVICE_DEBUG;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};

    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;

    if (!opt.forceWarp) {
        // Candidate adapters: preferred LUID first, then high-performance order.
        std::vector<ComPtr<IDXGIAdapter1>> candidates;
        ComPtr<IDXGIFactory6> f6;
        if (SUCCEEDED(factory_.As(&f6))) {
            ComPtr<IDXGIAdapter1> a;
            for (UINT i = 0; f6->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&a)) != DXGI_ERROR_NOT_FOUND; ++i) {
                candidates.push_back(a);
                a.Reset();
            }
        } else {
            ComPtr<IDXGIAdapter1> a;
            for (UINT i = 0; factory_->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; ++i) {
                candidates.push_back(a);
                a.Reset();
            }
        }
        if (opt.preferredLuid.LowPart || opt.preferredLuid.HighPart) {
            std::stable_partition(candidates.begin(), candidates.end(), [&](const ComPtr<IDXGIAdapter1>& a) {
                DXGI_ADAPTER_DESC1 d{};
                a->GetDesc1(&d);
                return d.AdapterLuid.LowPart == opt.preferredLuid.LowPart && d.AdapterLuid.HighPart == opt.preferredLuid.HighPart;
            });
        }
        for (auto& a : candidates) {
            DXGI_ADAPTER_DESC1 d{};
            a->GetDesc1(&d);
            if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
            hr = D3D11CreateDevice(a.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, &dev, &fl, &ctx);
            if (FAILED(hr) && opt.debugLayer) {
                hr = D3D11CreateDevice(a.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags & ~D3D11_CREATE_DEVICE_DEBUG, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                                       &dev, &fl, &ctx);
            }
            if (SUCCEEDED(hr)) {
                adapter_ = a;
                break;
            }
            BGN_LOG_WARN("GPU", "adapter '{}' rejected: {}", narrow(d.Description), hrToString(hr));
        }
    }
    if (!dev) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags & ~D3D11_CREATE_DEVICE_DEBUG, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, &dev,
                               &fl, &ctx);
        if (FAILED(hr)) {
            BGN_LOG_ERROR("GPU", "no usable Direct3D 11 device (WARP failed: {})", hrToString(hr));
            return false;
        }
        ComPtr<IDXGIDevice> dxgiDev;
        dev.As(&dxgiDev);
        ComPtr<IDXGIAdapter> a;
        dxgiDev->GetAdapter(&a);
        a.As(&adapter_);
        BGN_LOG_WARN("GPU", "using WARP software rasterizer");
    }
    dev.As(&device_);
    ctx.As(&context_);
    if (!device_ || !context_) {
        BGN_LOG_ERROR("GPU", "D3D11.1 interfaces unavailable");
        destroy();
        return false;
    }
    info_ = describeAdapter(adapter_.Get());
    info_.featureLevel = fl;
    // Half precision only if the driver actually exposes 16-bit min precision for all stages
    D3D11_FEATURE_DATA_SHADER_MIN_PRECISION_SUPPORT mp{};
    if (SUCCEEDED(device_->CheckFeatureSupport(D3D11_FEATURE_SHADER_MIN_PRECISION_SUPPORT, &mp, sizeof(mp))))
        info_.halfPrecision = info_.halfPrecision && (mp.AllOtherShaderStagesMinPrecision & D3D11_SHADER_MIN_PRECISION_16_BIT);
    else
        info_.halfPrecision = false;
    ComPtr<IDXGIFactory5> f5;
    if (SUCCEEDED(factory_.As(&f5))) {
        BOOL allow = FALSE;
        if (SUCCEEDED(f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow)))) info_.tearingSupported = allow == TRUE;
    }
    // Keep latency low: never queue more than necessary on the driver side
    ComPtr<IDXGIDevice1> dxgi1;
    if (SUCCEEDED(device_.As(&dxgi1))) dxgi1->SetMaximumFrameLatency(1);
    ComPtr<ID3D11Multithread> mt;
    if (SUCCEEDED(context_.As(&mt))) mt->SetMultithreadProtected(TRUE); // WGC callbacks touch the device from another thread
    BGN_LOG_INFO("GPU", "device: {} [{}] FL {:x}, VRAM {:.0f} MB, driver {}, fp16 {}, tearing {}", info_.name, info_.family, unsigned(fl),
                 info_.dedicatedVramMB, info_.driverVersion, info_.halfPrecision, info_.tearingSupported);
    return true;
}

void GpuDevice::destroy() {
    if (context_) {
        context_->ClearState();
        context_->Flush();
    }
    context_.Reset();
    device_.Reset();
    adapter_.Reset();
    factory_.Reset();
}

HRESULT GpuDevice::removedReason() const { return device_ ? device_->GetDeviceRemovedReason() : E_FAIL; }

bool GpuDevice::queryVideoMemory(double& usageMB, double& budgetMB) const {
    ComPtr<IDXGIAdapter3> a3;
    if (!adapter_ || FAILED(adapter_.As(&a3))) return false;
    DXGI_QUERY_VIDEO_MEMORY_INFO vi{};
    if (FAILED(a3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &vi))) return false;
    usageMB = double(vi.CurrentUsage) / (1024.0 * 1024.0);
    budgetMB = double(vi.Budget) / (1024.0 * 1024.0);
    return true;
}

} // namespace bgn
