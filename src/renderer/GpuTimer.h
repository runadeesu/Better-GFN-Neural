#pragma once
// Non-blocking GPU timestamp profiling (D3D11 timestamp + disjoint queries).

#include <d3d11.h>

#include <array>

#include "platform/Win32.h"

namespace bgn {

enum class GpuStage : int { Ingest, Pyramid, Flow, Cleanup, Temporal, Deblur, Upscale, Finish, Interpolate, Present, Analysis, Count };
const char* toString(GpuStage s);
constexpr int kGpuStageCount = static_cast<int>(GpuStage::Count);

struct GpuFrameTiming {
    double totalMs = 0;
    std::array<double, kGpuStageCount> stageMs{};
    uint64_t frameId = 0;
};

class GpuTimer {
public:
    bool init(ID3D11Device* device);
    void release();
    void beginFrame(ID3D11DeviceContext* ctx, uint64_t frameId);
    void mark(ID3D11DeviceContext* ctx, GpuStage stage); // time since the previous mark is attributed to |stage|
    void endFrame(ID3D11DeviceContext* ctx);
    // Retrieves the oldest finished frame (never stalls). Returns false if none ready.
    bool collect(ID3D11DeviceContext* ctx, GpuFrameTiming& out);

private:
    static constexpr int kRing = 6;
    static constexpr int kMaxMarks = 24;
    struct Slot {
        ComPtr<ID3D11Query> disjoint;
        std::array<ComPtr<ID3D11Query>, kMaxMarks + 1> stamps;
        std::array<int, kMaxMarks + 1> stage{};
        int count = 0;
        bool pending = false;
        uint64_t frameId = 0;
    };
    std::array<Slot, kRing> slots_;
    int write_ = 0, read_ = 0;
    Slot* cur_ = nullptr;
    bool ok_ = false;
};

} // namespace bgn
