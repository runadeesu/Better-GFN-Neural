#pragma once
// Neural Super Resolution x2 on the GPU (shaders/nsr.hlsl). Runs the trained
// CNN layer by layer; activations live in RGBA16F textures (4 channels each).

#include <array>

#include "renderer/GpuContext.h"

namespace bgn {

enum class NsrModel { Small, Large };

class NsrUpscaler {
public:
    bool ensure(ID3D11Device* dev, int lrW, int lrH, NsrModel model);
    void reset();
    // src: low-res color (working space). Writes the 2x result into |hr| (must be 2*lrW x 2*lrH, UAV).
    void run(const GpuContext& g, ID3D11ShaderResourceView* src, ID3D11UnorderedAccessView* hr, NsrModel model);
    size_t vramBytes() const;

private:
    std::array<GpuTexture, 4> actA_, actB_;
    int w_ = 0, h_ = 0;
    int groupsAllocated_ = 0;
};

} // namespace bgn
