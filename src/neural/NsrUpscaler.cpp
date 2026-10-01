#include "neural/NsrUpscaler.h"

namespace bgn {

bool NsrUpscaler::ensure(ID3D11Device* dev, int lrW, int lrH, NsrModel model) {
    const int need = model == NsrModel::Large ? 4 : 2;
    if (w_ == lrW && h_ == lrH && groupsAllocated_ >= need) return true;
    bool ok = true;
    for (int i = 0; i < 4; ++i) {
        if (i < need) {
            ok &= actA_[i].ensure(dev, lrW, lrH, DXGI_FORMAT_R16G16B16A16_FLOAT);
            ok &= actB_[i].ensure(dev, lrW, lrH, DXGI_FORMAT_R16G16B16A16_FLOAT);
        } else {
            actA_[i].reset();
            actB_[i].reset();
        }
    }
    w_ = lrW;
    h_ = lrH;
    groupsAllocated_ = ok ? need : 0;
    return ok;
}

void NsrUpscaler::reset() {
    for (auto& t : actA_) t.reset();
    for (auto& t : actB_) t.reset();
    w_ = h_ = groupsAllocated_ = 0;
}

size_t NsrUpscaler::vramBytes() const {
    size_t b = 0;
    for (auto& t : actA_) b += t.bytes();
    for (auto& t : actB_) b += t.bytes();
    return b;
}

void NsrUpscaler::run(const GpuContext& g, ID3D11ShaderResourceView* src, ID3D11UnorderedAccessView* hr, NsrModel model) {
    const bool large = model == NsrModel::Large;
    const bool half = g.halfPrecision;
    gpu::PassCB p{};
    p.gPassI = u4(uint32_t(w_), uint32_t(h_));
    g.setPass(p);
    const UINT gx = groups(w_, 8), gy = groups(h_, 8);
    auto A = [&](int i) { return actA_[i].uav.Get(); };
    auto B = [&](int i) { return actB_[i].uav.Get(); };
    auto As = [&](int i) { return actA_[i].srv.Get(); };
    auto Bs = [&](int i) { return actB_[i].srv.Get(); };

    if (large) {
        dispatchCompute(g.ctx, g.cs(half ? ShaderId::nsr_l_first_h_cs : ShaderId::nsr_l_first_cs), {src}, {A(0), A(1), A(2), A(3)}, gx, gy);
        dispatchCompute(g.ctx, g.cs(half ? ShaderId::nsr_l_l1_h_cs : ShaderId::nsr_l_l1_cs), {As(0), As(1), As(2), As(3)}, {B(0), B(1), B(2), B(3)}, gx, gy);
        dispatchCompute(g.ctx, g.cs(half ? ShaderId::nsr_l_l2_h_cs : ShaderId::nsr_l_l2_cs), {Bs(0), Bs(1), Bs(2), Bs(3)}, {A(0), A(1), A(2), A(3)}, gx, gy);
        dispatchCompute(g.ctx, g.cs(half ? ShaderId::nsr_l_l3_h_cs : ShaderId::nsr_l_l3_cs), {As(0), As(1), As(2), As(3)}, {B(0), B(1), B(2), B(3)}, gx, gy);
        dispatchCompute(g.ctx, g.cs(half ? ShaderId::nsr_l_last_h_cs : ShaderId::nsr_l_last_cs), {Bs(0), Bs(1), Bs(2), Bs(3), src}, {hr}, gx, gy);
    } else {
        dispatchCompute(g.ctx, g.cs(half ? ShaderId::nsr_s_first_h_cs : ShaderId::nsr_s_first_cs), {src}, {A(0), A(1)}, gx, gy);
        dispatchCompute(g.ctx, g.cs(half ? ShaderId::nsr_s_l1_h_cs : ShaderId::nsr_s_l1_cs), {As(0), As(1)}, {B(0), B(1)}, gx, gy);
        dispatchCompute(g.ctx, g.cs(half ? ShaderId::nsr_s_l2_h_cs : ShaderId::nsr_s_l2_cs), {Bs(0), Bs(1)}, {A(0), A(1)}, gx, gy);
        // S model: last stage reads t0,t1 activations and t4 color => bind null t2,t3
        dispatchCompute(g.ctx, g.cs(half ? ShaderId::nsr_s_last_h_cs : ShaderId::nsr_s_last_cs), {As(0), As(1), nullptr, nullptr, src}, {hr}, gx, gy);
    }
}

} // namespace bgn
