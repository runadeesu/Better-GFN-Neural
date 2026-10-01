#pragma once
// Helpers shared by the benchmark and the GPU self-test.

#include <vector>

#include "renderer/GpuContext.h"

namespace bgn {

// Renders the synthetic test scene (shaders/synthetic.hlsl) into an RGBA8 UAV texture.
void renderSyntheticScene(const GpuContext& g, ID3D11UnorderedAccessView* uav, int w, int h, float timeSeconds, float panPxPerSecond);

// Reads back an RGBA16F / RGBA8 texture as float RGBA (blocking; tests/benchmark only).
bool readbackTexture(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D* tex, std::vector<float>& rgba, int& w, int& h);

float halfToFloat(uint16_t h);
uint16_t floatToHalf(float f);

} // namespace bgn
