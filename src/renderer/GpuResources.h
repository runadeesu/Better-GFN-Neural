#pragma once
// GPU resource helpers: 2D textures with views, raw buffers with CPU readback,
// constant buffers and a compute dispatch helper that binds/unbinds cleanly.

#include <d3d11_1.h>

#include <cstring>
#include <initializer_list>
#include <vector>

#include "platform/Win32.h"
#include "renderer/ShaderLibrary.h"

namespace bgn {

struct GpuTexture {
    ComPtr<ID3D11Texture2D> tex;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11UnorderedAccessView> uav;
    ComPtr<ID3D11RenderTargetView> rtv;
    int width = 0, height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;

    bool create(ID3D11Device* dev, int w, int h, DXGI_FORMAT fmt, bool withUav = true, bool withRtv = false);
    // Re-creates only if size/format changed. Returns false on failure.
    bool ensure(ID3D11Device* dev, int w, int h, DXGI_FORMAT fmt, bool withUav = true, bool withRtv = false);
    void reset();
    bool valid() const { return tex != nullptr; }
    size_t bytes() const;
};

// Raw (byte address) UAV buffer + staging copy for asynchronous readback.
class ReadbackBuffer {
public:
    bool create(ID3D11Device* dev, UINT bytes, int ringSize = 3);
    void reset();
    ID3D11UnorderedAccessView* uav() const { return uav_.Get(); }
    void clear(ID3D11DeviceContext* ctx);
    // Copies the GPU buffer into the next staging slot (call after the dispatch).
    void requestReadback(ID3D11DeviceContext* ctx);
    // Maps the oldest pending staging slot without stalling. Returns false if not ready.
    bool tryRead(ID3D11DeviceContext* ctx, void* dst, UINT bytes);

private:
    ComPtr<ID3D11Buffer> buffer_;
    ComPtr<ID3D11UnorderedAccessView> uav_;
    std::vector<ComPtr<ID3D11Buffer>> staging_;
    std::vector<bool> pending_;
    int write_ = 0, read_ = 0;
    UINT bytes_ = 0;
};

template <typename T>
class ConstantBuffer {
public:
    bool create(ID3D11Device* dev) {
        D3D11_BUFFER_DESC d{};
        d.ByteWidth = UINT((sizeof(T) + 15) & ~size_t(15));
        d.Usage = D3D11_USAGE_DYNAMIC;
        d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        return SUCCEEDED(dev->CreateBuffer(&d, nullptr, &buf_));
    }
    void update(ID3D11DeviceContext* ctx, const T& v) {
        D3D11_MAPPED_SUBRESOURCE m{};
        if (SUCCEEDED(ctx->Map(buf_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            memcpy(m.pData, &v, sizeof(T));
            ctx->Unmap(buf_.Get(), 0);
        }
    }
    ID3D11Buffer* get() const { return buf_.Get(); }
    void reset() { buf_.Reset(); }

private:
    ComPtr<ID3D11Buffer> buf_;
};

inline UINT groups(int size, int tile) { return UINT((size + tile - 1) / tile); }

// Binds SRVs (t0..), UAVs (u0..), runs the compute shader and unbinds everything again.
void dispatchCompute(ID3D11DeviceContext* ctx, ID3D11ComputeShader* cs, std::initializer_list<ID3D11ShaderResourceView*> srvs,
                     std::initializer_list<ID3D11UnorderedAccessView*> uavs, UINT gx, UINT gy, UINT gz = 1);

} // namespace bgn
