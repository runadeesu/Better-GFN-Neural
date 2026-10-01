#include "framegen/OpticalFlow.h"

#include <algorithm>

namespace bgn {

static int levelDim(int in, int l) { return std::max(1, (in + (1 << l) - 1) >> l); }

bool LumaPyramid::ensure(ID3D11Device* dev, int inW, int inH) {
    bool ok = true;
    for (int i = 0; i < kPyramidLevels; ++i) {
        int l = i + 1;
        bool changed = level[i].width != levelDim(inW, l) || level[i].height != levelDim(inH, l);
        ok &= level[i].ensure(dev, levelDim(inW, l), levelDim(inH, l), DXGI_FORMAT_R16_FLOAT);
        if (changed) valid = false;
    }
    return ok;
}

void LumaPyramid::reset() {
    for (auto& t : level) t.reset();
    valid = false;
}

void LumaPyramid::build(const GpuContext& g, ID3D11ShaderResourceView* color, int inW, int inH) {
    int srcW = inW, srcH = inH;
    for (int i = 0; i < kPyramidLevels; ++i) {
        const GpuTexture& dst = level[i];
        gpu::PassCB p{};
        p.gPassI = u4(uint32_t(dst.width), uint32_t(dst.height), uint32_t(srcW), uint32_t(srcH));
        g.setPass(p);
        if (i == 0)
            dispatchCompute(g.ctx, g.cs(ShaderId::luma_down_color_cs), {color}, {dst.uav.Get()}, groups(dst.width, 8), groups(dst.height, 8));
        else
            dispatchCompute(g.ctx, g.cs(ShaderId::luma_down_cs), {level[i - 1].srv.Get()}, {dst.uav.Get()}, groups(dst.width, 8), groups(dst.height, 8));
        srcW = dst.width;
        srcH = dst.height;
    }
    valid = true;
}

bool OpticalFlow::ensure(ID3D11Device* dev, int inW, int inH) {
    if (inW == inW_ && inH == inH_ && levelFlow_[0].valid()) return true;
    inW_ = inW;
    inH_ = inH;
    bool ok = true;
    for (int i = 0; i < kPyramidLevels; ++i) {
        int l = i + 1;
        int gw = (levelDim(inW, l) + 3) / 4, gh = (levelDim(inH, l) + 3) / 4;
        ok &= levelFlow_[i].create(dev, gw, gh, DXGI_FORMAT_R16G16B16A16_FLOAT);
    }
    lastQuality_ = 0; // forces final_ re-allocation
    invalidate();
    return ok;
}

void OpticalFlow::reset() {
    for (auto& t : levelFlow_) t.reset();
    for (auto& t : final_) t.reset();
    inW_ = inH_ = 0;
    invalidate();
}

bool OpticalFlow::compute(const GpuContext& g, const LumaPyramid& cur, const LumaPyramid& prev, int quality) {
    if (!cur.valid || !prev.valid || quality <= 0) {
        valid_ = false;
        return false;
    }
    const int finest = quality >= 2 ? 1 : 2; // pyramid level (1 = 1/2 res)
    const GpuTexture& finestFlow = levelFlow_[finest - 1];
    if (quality != lastQuality_ || final_[0].width != finestFlow.width || final_[0].height != finestFlow.height) {
        for (auto& f : final_) f.create(g.dev, finestFlow.width, finestFlow.height, DXGI_FORMAT_R16G16B16A16_FLOAT);
        lastQuality_ = quality;
        prevValid_ = false;
    }
    cur_ ^= 1;
    for (int l = kPyramidLevels; l >= finest; --l) {
        const GpuTexture& out = levelFlow_[l - 1];
        const bool coarsest = l == kPyramidLevels;
        const bool final = l == finest;
        const GpuTexture* coarse = coarsest ? nullptr : &levelFlow_[l];
        gpu::PassCB p{};
        p.gPassI = u4(uint32_t(out.width), uint32_t(out.height), uint32_t(cur.level[l - 1].width), uint32_t(cur.level[l - 1].height));
        p.gPassF = f4(float(1 << l), coarse ? float(coarse->width) : 1.0f, coarse ? float(coarse->height) : 1.0f, coarsest ? 1.0f : 0.0f);
        p.gPassF2 = f4(final ? 1.0f : 0.0f, (final && prevValid_) ? 1.0f : 0.0f, float(final_[cur_ ^ 1].width), float(final_[cur_ ^ 1].height));
        g.setPass(p);
        dispatchCompute(g.ctx, g.cs(ShaderId::flow_cs),
                        {cur.level[l - 1].srv.Get(), prev.level[l - 1].srv.Get(), coarse ? coarse->srv.Get() : nullptr, final ? final_[cur_ ^ 1].srv.Get() : nullptr},
                        {out.uav.Get()}, groups(out.width, 8), groups(out.height, 8));
    }
    gpu::PassCB p{};
    p.gPassI = u4(uint32_t(finestFlow.width), uint32_t(finestFlow.height));
    g.setPass(p);
    dispatchCompute(g.ctx, g.cs(ShaderId::flow_smooth_cs), {finestFlow.srv.Get()}, {final_[cur_].uav.Get()}, groups(finestFlow.width, 8),
                    groups(finestFlow.height, 8));
    gridW_ = finestFlow.width;
    gridH_ = finestFlow.height;
    cellSize_ = float(4 << finest);
    valid_ = true;
    prevValid_ = true;
    return true;
}

ID3D11ShaderResourceView* OpticalFlow::flowSrv() const { return final_[cur_].srv.Get(); }

} // namespace bgn
