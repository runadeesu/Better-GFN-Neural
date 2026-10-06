#include "framegen/FrameInterpolator.h"

namespace bgn {

bool FrameInterpolator::ensure(ID3D11Device* dev, int outW, int outH) { return mid_.ensure(dev, outW, outH, DXGI_FORMAT_R16G16B16A16_FLOAT); }

void FrameInterpolator::reset() { mid_.reset(); }

void FrameInterpolator::run(const GpuContext& g, ID3D11ShaderResourceView* prevFinal, ID3D11ShaderResourceView* curFinal, ID3D11ShaderResourceView* flow) {
    gpu::PassCB p{};
    p.gPassI = u4(uint32_t(mid_.width), uint32_t(mid_.height));
    g.setPass(p);
    dispatchCompute(g.ctx, g.cs(ShaderId::interp_cs), {prevFinal, curFinal, flow}, {mid_.uav.Get()}, groups(mid_.width, 8), groups(mid_.height, 8));
}

void FrameInterpolator::extrapolate(const GpuContext& g, ID3D11ShaderResourceView* curFinal, ID3D11ShaderResourceView* flow) {
    dispatchCompute(g.ctx, g.cs(ShaderId::extrap_cs), {curFinal, flow}, {mid_.uav.Get()}, groups(mid_.width, 8), groups(mid_.height, 8));
}

} // namespace bgn
