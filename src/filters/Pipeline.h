#pragma once
// The GPU processing graph. Everything stays on the GPU:
//   capture surface --(GPU copy)--> ingest --> luma pyramid --> optical flow
//   --> compression cleanup --> temporal reconstruction --> deblur
//   --> super resolution (NSR CNN / Lanczos-AR) --> adaptive sharpen + HDR+/color
//   --> [frame interpolation] --> present (swap chain back buffer)

#include <array>
#include <string>

#include "filters/ContentResolution.h"
#include "framegen/FrameInterpolator.h"
#include "framegen/OpticalFlow.h"
#include "neural/NsrUpscaler.h"
#include "renderer/GpuContext.h"
#include "renderer/GpuTimer.h"
#include "settings/Presets.h"

namespace bgn {

struct CaptureInput {
    ID3D11Texture2D* texture = nullptr; // capture surface (any size), read with a GPU copy only
    RECT crop{};                        // client area inside the texture
    bool hdr = false;                   // FP16 scRGB surface
};

struct PipelineGeometry {
    int cropW = 0, cropH = 0;     // captured client area size
    int streamW = 0, streamH = 0; // >0: reconstruct from this (detected) stream resolution
    int outW = 0, outH = 0;       // processing output size
    bool operator==(const PipelineGeometry&) const = default;
};

struct PipelineFrameParams {
    EffectiveConfig cfg;
    bool hdrOutput = false;   // scRGB swap chain
    bool hdrPlus = false;     // SDR->HDR highlight expansion
    float paperWhiteNits = 200.0f;
    float peakNits = 1000.0f;
    bool compareSplit = false;
    float splitPosition = 0.5f;
    // Accessibility (global)
    int colorVision = 0;            // 0 off, 1 protanopia, 2 deuteranopia, 3 tritanopia
    float colorVisionStrength = 1.0f;
    float nightLight = 0.0f;
};

struct PipelineStatus {
    int inW = 0, inH = 0, outW = 0, outH = 0;
    UpscalerKind upscalerUsed = UpscalerKind::None;
    bool flowActive = false;
    bool temporalActive = false;
    size_t vramBytes = 0;
    double sceneLuma = 0;
    double contentFactor = 0;    // last estimated upscale factor already in the content (0 unknown)
};

class Pipeline {
public:
    bool init(GpuDevice& device, ShaderLibrary& shaders);
    void release();

    bool configure(const PipelineGeometry& geo);
    const PipelineGeometry& geometry() const { return geo_; }

    // Processes a new captured frame. Returns false on failure (resources missing).
    bool process(const CaptureInput& in, const PipelineFrameParams& params, GpuTimer* timer);
    // Generates the interpolated frame between the previous and current output.
    bool interpolate(GpuTimer* timer);
    bool canInterpolate() const { return prevFinalValid_ && flow_.valid(); }

    // Draws the current output (or interpolated frame) into a render target.
    void present(ID3D11RenderTargetView* rtv, int bbW, int bbH, const RECT& dst, bool interpolated, uint64_t presentIndex, bool ditherHdr);

    void resetHistory();
    void pollReadbacks();

    const PipelineStatus& status() const { return status_; }
    ID3D11ShaderResourceView* finalSrv() const { return final_[curFinal_].srv.Get(); }
    ID3D11Texture2D* finalTexture() const { return final_[curFinal_].tex.Get(); }
    ID3D11ShaderResourceView* midSrv() const { return interp_.midSrv(); }
    // Latest content resolution measurement (fresh=true once per new measurement)
    bool takeContentMeasurement(ContentResMeasurement& out);
    // Latest blockiness measurement: mean luma step across / inside 8x8 blocks
    bool takeQualityMeasurement(double& boundaryMeanStep, double& interiorMeanStep);

private:
    void updateFrameCB(const PipelineFrameParams& p, uint32_t flags);
    void runUpscale(const PipelineFrameParams& p, ID3D11ShaderResourceView* src, GpuTimer* timer);

    GpuDevice* device_ = nullptr;
    ShaderLibrary* shaders_ = nullptr;
    GpuContext g_;
    ConstantBuffer<gpu::FrameCB> frameCB_;
    ConstantBuffer<gpu::PassCB> passCB_;
    ComPtr<ID3D11SamplerState> linear_, point_;
    ComPtr<ID3D11RasterizerState> raster_;
    ComPtr<ID3D11BlendState> blend_;
    ComPtr<ID3D11DepthStencilState> depth_;

    PipelineGeometry geo_;
    int inW_ = 0, inH_ = 0;
    GpuTexture capCopy_;     // GPU copy of the client area (capture format)
    GpuTexture ingestFull_;  // working space at crop size (only when reconstructing from stream res)
    GpuTexture cur_, clean_, deblurred_, hr_, up_;
    std::array<GpuTexture, 2> stable_;
    std::array<GpuTexture, 2> final_;
    std::array<LumaPyramid, 2> pyramid_;
    int curPyr_ = 0, curStable_ = 0, curFinal_ = 0;
    bool historyValid_ = false, prevFinalValid_ = false;
    OpticalFlow flow_;
    NsrUpscaler nsr_;
    FrameInterpolator interp_;
    ReadbackBuffer statsBuf_, contentBuf_, qualityBuf_;
    bool qualityFresh_ = false;
    double qualityBoundary_ = 0, qualityInterior_ = 0;

    gpu::FrameCB cb_{};
    uint64_t frameIndex_ = 0;
    PipelineStatus status_;
    // Auto color state (smoothed)
    double autoBlack_ = 0, autoWhite_ = 1, sceneLuma_ = 0.4;
    bool contentFresh_ = false;
    ContentResMeasurement content_;
    bool inputHdr_ = false;
    DXGI_FORMAT capFormat_ = DXGI_FORMAT_UNKNOWN;
};

} // namespace bgn
