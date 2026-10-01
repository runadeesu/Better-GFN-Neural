#pragma once
// Bundle of the per-thread GPU objects that processing stages need.

#include "ShaderShared.h"
#include "renderer/GpuDevice.h"
#include "renderer/GpuResources.h"
#include "renderer/ShaderLibrary.h"

namespace bgn {

struct GpuContext {
    ID3D11Device1* dev = nullptr;
    ID3D11DeviceContext1* ctx = nullptr;
    ShaderLibrary* shaders = nullptr;
    ConstantBuffer<gpu::PassCB>* passCB = nullptr;
    bool halfPrecision = false;

    void setPass(const gpu::PassCB& p) const {
        passCB->update(ctx, p);
        ID3D11Buffer* b = passCB->get();
        ctx->CSSetConstantBuffers(1, 1, &b);
    }
    ID3D11ComputeShader* cs(ShaderId id) const { return shaders->cs(id); }
};

inline gpu::uint4 u4(uint32_t x, uint32_t y = 0, uint32_t z = 0, uint32_t w = 0) { return gpu::uint4{x, y, z, w}; }
inline gpu::float4 f4(float x, float y = 0, float z = 0, float w = 0) { return gpu::float4{x, y, z, w}; }

} // namespace bgn
