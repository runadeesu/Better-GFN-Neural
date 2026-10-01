#pragma once
// Motion-compensated frame interpolation (shaders/interp.hlsl).

#include "framegen/FramePacing.h"
#include "renderer/GpuContext.h"

namespace bgn {

class FrameInterpolator {
public:
    bool ensure(ID3D11Device* dev, int outW, int outH);
    void reset();
    // Writes the t=0.5 frame between prev and cur final frames.
    void run(const GpuContext& g, ID3D11ShaderResourceView* prevFinal, ID3D11ShaderResourceView* curFinal, ID3D11ShaderResourceView* flow);
    ID3D11ShaderResourceView* midSrv() const { return mid_.srv.Get(); }
    size_t vramBytes() const { return mid_.bytes(); }

private:
    GpuTexture mid_;
};

} // namespace bgn
