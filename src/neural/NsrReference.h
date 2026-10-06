#pragma once
// CPU reference implementation of the NSR x2 networks. Bit-for-bit the same
// math as shaders/nsr.hlsl (up to float rounding); used by the GPU self-test to
// verify the shader against the trained weights. Portable.

#include <string>
#include <vector>

namespace bgn {

struct NsrLayer {
    int cin = 0, cout = 0;
    std::vector<float> w; // [o][i][ky][kx]
    std::vector<float> b;
};

struct NsrModelData {
    std::string name;
    std::vector<NsrLayer> layers;
    int channels() const { return layers.empty() ? 0 : layers.front().cout; }
    int hiddenLayers() const { return int(layers.size()) - 2; }
};

const NsrModelData& nsrModelT();
const NsrModelData& nsrModelS();
const NsrModelData& nsrModelL();

// rgb: interleaved RGB floats (w*h*3), gamma encoded. out: (2w*2h*3).
void nsrUpscaleReference(const NsrModelData& model, const std::vector<float>& rgb, int w, int h, std::vector<float>& out);

} // namespace bgn
