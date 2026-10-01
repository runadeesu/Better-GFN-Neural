#pragma once
// GPU optical flow: coarse-to-fine block matching over the luma pyramid
// (shaders/flow.hlsl) followed by a vector-median filter. Produces backward
// flow (current -> previous frame) in input pixels on a cell grid.

#include <array>

#include "renderer/GpuContext.h"

namespace bgn {

constexpr int kPyramidLevels = 5; // levels 1..5 (1/2 .. 1/32 resolution)

struct LumaPyramid {
    std::array<GpuTexture, kPyramidLevels> level; // index 0 = 1/2 res
    bool valid = false;
    bool ensure(ID3D11Device* dev, int inW, int inH);
    void reset();
    // Builds all levels from an RGBA working-space texture
    void build(const GpuContext& g, ID3D11ShaderResourceView* color, int inW, int inH);
};

class OpticalFlow {
public:
    bool ensure(ID3D11Device* dev, int inW, int inH);
    void reset();
    // quality 1: finest level 1/4 (16 px cells), quality 2: finest level 1/2 (8 px cells)
    bool compute(const GpuContext& g, const LumaPyramid& cur, const LumaPyramid& prev, int quality);
    ID3D11ShaderResourceView* flowSrv() const;
    int gridW() const { return gridW_; }
    int gridH() const { return gridH_; }
    float cellSize() const { return cellSize_; }
    bool valid() const { return valid_; }
    void invalidate() { valid_ = false; prevValid_ = false; }

private:
    std::array<GpuTexture, kPyramidLevels> levelFlow_; // per pyramid level
    std::array<GpuTexture, 2> final_;                  // smoothed result ping-pong (previous = temporal candidate)
    int cur_ = 0;
    int inW_ = 0, inH_ = 0;
    int gridW_ = 0, gridH_ = 0;
    float cellSize_ = 16;
    int lastQuality_ = 0;
    bool valid_ = false, prevValid_ = false;
};

} // namespace bgn
