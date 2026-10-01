#include "renderer/GpuTimer.h"

namespace bgn {

const char* toString(GpuStage s) {
    switch (s) {
    case GpuStage::Ingest: return "Ingest";
    case GpuStage::Pyramid: return "Pyramid";
    case GpuStage::Flow: return "Optical flow";
    case GpuStage::Cleanup: return "Compression cleanup";
    case GpuStage::Temporal: return "Temporal";
    case GpuStage::Deblur: return "Deblur";
    case GpuStage::Upscale: return "Super resolution";
    case GpuStage::Finish: return "Sharpen + Color";
    case GpuStage::Interpolate: return "Frame interpolation";
    case GpuStage::Present: return "Present";
    case GpuStage::Analysis: return "Analysis";
    case GpuStage::Count: break;
    }
    return "?";
}

bool GpuTimer::init(ID3D11Device* device) {
    release();
    D3D11_QUERY_DESC dj{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
    D3D11_QUERY_DESC ts{D3D11_QUERY_TIMESTAMP, 0};
    for (auto& s : slots_) {
        if (FAILED(device->CreateQuery(&dj, &s.disjoint))) return false;
        for (auto& q : s.stamps)
            if (FAILED(device->CreateQuery(&ts, &q))) return false;
    }
    ok_ = true;
    return true;
}

void GpuTimer::release() {
    for (auto& s : slots_) {
        s.disjoint.Reset();
        for (auto& q : s.stamps) q.Reset();
        s.pending = false;
        s.count = 0;
    }
    write_ = read_ = 0;
    cur_ = nullptr;
    ok_ = false;
}

void GpuTimer::beginFrame(ID3D11DeviceContext* ctx, uint64_t frameId) {
    cur_ = nullptr;
    if (!ok_) return;
    Slot& s = slots_[write_];
    if (s.pending) return; // ring full: skip timing this frame (never stall)
    cur_ = &s;
    s.count = 0;
    s.frameId = frameId;
    ctx->Begin(s.disjoint.Get());
    ctx->End(s.stamps[0].Get());
    s.stage[0] = -1;
    s.count = 1;
}

void GpuTimer::mark(ID3D11DeviceContext* ctx, GpuStage stage) {
    if (!cur_ || cur_->count > kMaxMarks) return;
    ctx->End(cur_->stamps[cur_->count].Get());
    cur_->stage[cur_->count] = static_cast<int>(stage);
    ++cur_->count;
}

void GpuTimer::endFrame(ID3D11DeviceContext* ctx) {
    if (!cur_) return;
    ctx->End(cur_->disjoint.Get());
    cur_->pending = true;
    write_ = (write_ + 1) % kRing;
    cur_ = nullptr;
}

bool GpuTimer::collect(ID3D11DeviceContext* ctx, GpuFrameTiming& out) {
    if (!ok_) return false;
    Slot& s = slots_[read_];
    if (!s.pending) return false;
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
    if (ctx->GetData(s.disjoint.Get(), &dj, sizeof(dj), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) return false;
    std::array<UINT64, kMaxMarks + 1> t{};
    for (int i = 0; i < s.count; ++i)
        if (ctx->GetData(s.stamps[i].Get(), &t[i], sizeof(UINT64), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) return false;
    s.pending = false;
    read_ = (read_ + 1) % kRing;
    if (dj.Disjoint || dj.Frequency == 0) return false;
    out = GpuFrameTiming{};
    out.frameId = s.frameId;
    const double toMs = 1000.0 / double(dj.Frequency);
    for (int i = 1; i < s.count; ++i) {
        double ms = double(t[i] - t[i - 1]) * toMs;
        if (ms < 0 || ms > 1000) continue;
        out.stageMs[s.stage[i]] += ms;
    }
    if (s.count > 1) out.totalMs = double(t[s.count - 1] - t[0]) * toMs;
    return true;
}

} // namespace bgn
