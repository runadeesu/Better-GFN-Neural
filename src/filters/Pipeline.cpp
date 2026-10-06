#include "filters/Pipeline.h"

#include <algorithm>
#include <cmath>

#include "core/Log.h"

namespace bgn {

namespace {
constexpr DXGI_FORMAT kWork = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr int kStatsWords = 66;
constexpr int kContentWords = 3;
constexpr int kQualityWords = 4;
} // namespace

bool Pipeline::init(GpuDevice& device, ShaderLibrary& shaders) {
    release();
    device_ = &device;
    shaders_ = &shaders;
    auto* dev = device.device();
    if (!frameCB_.create(dev) || !passCB_.create(dev)) return false;
    g_.dev = dev;
    g_.ctx = device.context();
    g_.shaders = &shaders;
    g_.passCB = &passCB_;
    g_.halfPrecision = device.info().halfPrecision;

    D3D11_SAMPLER_DESC s{};
    s.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    s.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(dev->CreateSamplerState(&s, &linear_))) return false;
    s.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    if (FAILED(dev->CreateSamplerState(&s, &point_))) return false;
    D3D11_RASTERIZER_DESC r{};
    r.FillMode = D3D11_FILL_SOLID;
    r.CullMode = D3D11_CULL_NONE;
    r.DepthClipEnable = TRUE;
    if (FAILED(dev->CreateRasterizerState(&r, &raster_))) return false;
    D3D11_BLEND_DESC b{};
    b.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(dev->CreateBlendState(&b, &blend_))) return false;
    D3D11_DEPTH_STENCIL_DESC d{};
    d.DepthEnable = FALSE;
    d.StencilEnable = FALSE;
    if (FAILED(dev->CreateDepthStencilState(&d, &depth_))) return false;
    if (!statsBuf_.create(dev, kStatsWords * 4) || !contentBuf_.create(dev, kContentWords * 4) || !qualityBuf_.create(dev, kQualityWords * 4)) return false;
    return true;
}

void Pipeline::release() {
    capCopy_.reset();
    ingestFull_.reset();
    cur_.reset();
    clean_.reset();
    deblurred_.reset();
    hr_.reset();
    up_.reset();
    for (auto& t : stable_) t.reset();
    for (auto& t : final_) t.reset();
    for (auto& p : pyramid_) p.reset();
    flow_.reset();
    nsr_.reset();
    interp_.reset();
    statsBuf_.reset();
    contentBuf_.reset();
    qualityBuf_.reset();
    qualityFresh_ = false;
    frameCB_.reset();
    passCB_.reset();
    linear_.Reset();
    point_.Reset();
    raster_.Reset();
    blend_.Reset();
    depth_.Reset();
    geo_ = {};
    inW_ = inH_ = 0;
    historyValid_ = prevFinalValid_ = false;
    device_ = nullptr;
}

bool Pipeline::configure(const PipelineGeometry& geo) {
    if (!device_) return false;
    if (geo == geo_ && cur_.valid()) return true;
    auto* dev = g_.dev;
    geo_ = geo;
    const bool reconstruct = geo.streamW > 0 && geo.streamH > 0 && (geo.streamW < geo.cropW || geo.streamH < geo.cropH);
    inW_ = reconstruct ? geo.streamW : geo.cropW;
    inH_ = reconstruct ? geo.streamH : geo.cropH;
    bool ok = true;
    if (reconstruct) ok &= ingestFull_.ensure(dev, geo.cropW, geo.cropH, kWork);
    else ingestFull_.reset();
    ok &= cur_.ensure(dev, inW_, inH_, kWork);
    ok &= clean_.ensure(dev, inW_, inH_, kWork);
    ok &= deblurred_.ensure(dev, inW_, inH_, kWork);
    for (auto& t : stable_) ok &= t.ensure(dev, inW_, inH_, kWork);
    for (auto& t : final_) ok &= t.ensure(dev, geo.outW, geo.outH, kWork);
    for (auto& p : pyramid_) ok &= p.ensure(dev, inW_, inH_);
    ok &= flow_.ensure(dev, inW_, inH_);
    hr_.reset();
    up_.reset();
    nsr_.reset();
    interp_.reset();
    resetHistory();
    status_.inW = inW_;
    status_.inH = inH_;
    status_.outW = geo.outW;
    status_.outH = geo.outH;
    BGN_LOG_INFO("Pipeline", "configured: capture {}x{}, processing {}x{}{}, output {}x{}", geo.cropW, geo.cropH, inW_, inH_,
                 reconstruct ? " (reconstructed stream resolution)" : "", geo.outW, geo.outH);
    if (!ok) BGN_LOG_ERROR("Pipeline", "GPU resource allocation failed (out of video memory?)");
    return ok;
}

void Pipeline::resetHistory() {
    historyValid_ = false;
    prevFinalValid_ = false;
    for (auto& p : pyramid_) p.valid = false;
    flow_.invalidate();
}

void Pipeline::updateFrameCB(const PipelineFrameParams& p, uint32_t flags) {
    const EffectiveConfig& c = p.cfg;
    gpu::FrameCB& cb = cb_;
    cb.gIn = f4(float(inW_), float(inH_), 1.0f / float(inW_), 1.0f / float(inH_));
    cb.gOut = f4(float(geo_.outW), float(geo_.outH), 1.0f / float(geo_.outW), 1.0f / float(geo_.outH));
    cb.gSrc = f4(0, 0, float(geo_.cropW), float(geo_.cropH));
    cb.gFrame = u4(uint32_t(frameIndex_), inputHdr_ ? BGN_INPUT_SCRGB : BGN_INPUT_SDR, p.hdrOutput ? BGN_OUTPUT_SCRGB : BGN_OUTPUT_SDR, flags);
    cb.gCleanup = f4(c.deblock, c.deband, c.denoise, 0.008f);
    cb.gTemporal = f4(c.temporal, 1.0f, 1.0f, 0);
    cb.gDeblur = f4(c.deblur, c.motionDeblur ? 1.0f : 0.0f, 1.0f, 0.8f);
    cb.gSharpen = f4(c.sharpen, float(geo_.outW) / float(inW_), 0.5f, 0.6f);
    const ColorParams& k = c.color;
    cb.gColor0 = f4(k.blackLevel, k.whiteLevel, k.contrast, k.gamma);
    cb.gColor1 = f4(k.saturation, k.vibrance, k.temperature, k.highlightRecovery);
    cb.gColor2 = f4(k.shadowDetail, k.localContrast, k.toneMapping ? 1.0f : 0.0f, k.enabled ? 1.0f : 0.0f);
    float autoAmount = (k.enabled && k.automatic && !inputHdr_) ? 1.0f : 0.0f;
    cb.gAuto = f4(float(autoBlack_), float(autoWhite_), autoAmount, float(sceneLuma_));
    cb.gHdr = f4(p.paperWhiteNits, p.peakNits, c.hdrIntensity, p.hdrPlus ? 1.0f : 0.0f);
    cb.gFlow = f4(float(std::max(flow_.gridW(), 1)), float(std::max(flow_.gridH(), 1)), flow_.cellSize(), 0);
    cb.gInterp = f4(0.5f, 0.035f, 0.25f, 0);
    cb.gPresent2.w = p.splitPosition;
    cb.gStyle = f4(c.monochrome, c.splitTone, p.nightLight, float(p.colorVision));
    cb.gStyle2 = f4(p.colorVision > 0 ? p.colorVisionStrength : 0.0f, 0, 0, 0);
    frameCB_.update(g_.ctx, cb);
}

bool Pipeline::process(const CaptureInput& in, const PipelineFrameParams& params, GpuTimer* timer) {
    if (!device_ || !cur_.valid() || !in.texture) return false;
    auto* ctx = g_.ctx;
    ++frameIndex_;

    // --- GPU copy of the client area (the capture buffer is released right after)
    D3D11_TEXTURE2D_DESC td{};
    in.texture->GetDesc(&td);
    if (!capCopy_.ensure(g_.dev, geo_.cropW, geo_.cropH, td.Format, false)) return false;
    if (td.Format != capFormat_) {
        capFormat_ = td.Format;
        resetHistory();
    }
    inputHdr_ = in.hdr;
    D3D11_BOX box{UINT(std::max<LONG>(in.crop.left, 0)), UINT(std::max<LONG>(in.crop.top, 0)), 0, 0, 0, 1};
    box.right = std::min<UINT>(box.left + UINT(geo_.cropW), td.Width);
    box.bottom = std::min<UINT>(box.top + UINT(geo_.cropH), td.Height);
    if (box.right <= box.left || box.bottom <= box.top) return false;
    if (timer) timer->beginFrame(ctx, frameIndex_);
    ctx->CopySubresourceRegion(capCopy_.tex.Get(), 0, 0, 0, 0, in.texture, 0, &box);

    const EffectiveConfig& c = params.cfg;
    curPyr_ ^= 1;
    LumaPyramid& pyr = pyramid_[curPyr_];
    LumaPyramid& prevPyr = pyramid_[curPyr_ ^ 1];
    const bool useFlow = c.flowQuality > 0;
    bool flowValid = false;

    // Bind shared state for the whole frame
    ID3D11Buffer* cbs[1] = {frameCB_.get()};
    ID3D11SamplerState* samplers[2] = {linear_.Get(), point_.Get()};
    ctx->CSSetConstantBuffers(0, 1, cbs);
    ctx->CSSetSamplers(0, 2, samplers);
    uint32_t flags = 0;
    if (historyValid_) flags |= BGN_FLAG_HISTORY_VALID;
    if (c.textBoost) flags |= BGN_FLAG_TEXT_BOOST;
    if (c.skinProtect) flags |= BGN_FLAG_SKIN_PROTECT;
    if (c.motionDeblur) flags |= BGN_FLAG_MOTION_DEBLUR;
    if (params.compareSplit) flags |= BGN_FLAG_COMPARE_SPLIT;
    if (c.adaptiveSharpen) flags |= BGN_FLAG_ADAPTIVE_SHARPEN;
    if (c.cleanupHQ) flags |= BGN_FLAG_CLEANUP_HQ;
    if (c.darkSceneCleanup) flags |= BGN_FLAG_DARK_CLEANUP;
    if (c.color.automatic) flags |= BGN_FLAG_COLOR_AUTO;
    updateFrameCB(params, flags);

    // --- Ingest (+ optional reconstruction from the real stream resolution)
    if (ingestFull_.valid()) {
        cb_.gIn = f4(float(geo_.cropW), float(geo_.cropH), 1.0f / geo_.cropW, 1.0f / geo_.cropH);
        frameCB_.update(ctx, cb_);
        dispatchCompute(ctx, g_.cs(ShaderId::ingest_cs), {capCopy_.srv.Get()}, {ingestFull_.uav.Get()}, groups(geo_.cropW, 8), groups(geo_.cropH, 8));
        updateFrameCB(params, flags);
        gpu::PassCB p{};
        p.gPassI = u4(uint32_t(inW_), uint32_t(inH_), uint32_t(geo_.cropW), uint32_t(geo_.cropH));
        g_.setPass(p);
        dispatchCompute(ctx, g_.cs(ShaderId::resample_down_cs), {ingestFull_.srv.Get()}, {cur_.uav.Get()}, groups(inW_, 8), groups(inH_, 8));
    } else {
        dispatchCompute(ctx, g_.cs(ShaderId::ingest_cs), {capCopy_.srv.Get()}, {cur_.uav.Get()}, groups(inW_, 8), groups(inH_, 8));
    }
    if (timer) timer->mark(ctx, GpuStage::Ingest);

    // --- Luma pyramid (flow, local contrast, statistics)
    pyr.build(g_, cur_.srv.Get(), inW_, inH_);
    if (timer) timer->mark(ctx, GpuStage::Pyramid);

    // --- Analysis: scene histogram (every 4th frame), content resolution (every 60th)
    if (frameIndex_ % 4 == 0) {
        statsBuf_.clear(ctx);
        gpu::PassCB p{};
        p.gPassI = u4(uint32_t(pyr.level[2].width), uint32_t(pyr.level[2].height));
        g_.setPass(p);
        dispatchCompute(ctx, g_.cs(ShaderId::stats_cs), {pyr.level[2].srv.Get()}, {statsBuf_.uav()}, groups(pyr.level[2].width, 16),
                        groups(pyr.level[2].height, 16));
        statsBuf_.requestReadback(ctx);
    }
    if (frameIndex_ % 60 == 5) {
        const GpuTexture& src = ingestFull_.valid() ? ingestFull_ : cur_;
        int rw = std::min(src.width, 512), rh = std::min(src.height, 512);
        int rx = (src.width - rw) / 2, ry = (src.height - rh) / 2;
        contentBuf_.clear(ctx);
        cb_.gIn = f4(float(src.width), float(src.height), 1.0f / src.width, 1.0f / src.height);
        frameCB_.update(ctx, cb_);
        gpu::PassCB p{};
        p.gPassI = u4(uint32_t(rx), uint32_t(ry), uint32_t(rw), uint32_t(rh));
        g_.setPass(p);
        dispatchCompute(ctx, g_.cs(ShaderId::content_res_cs), {src.srv.Get()}, {contentBuf_.uav()}, groups(rw, 16), groups(rh, 16));
        contentBuf_.requestReadback(ctx);
        updateFrameCB(params, flags);
    }
    if (frameIndex_ % 15 == 9) {
        // Stream quality: blockiness on the 8x8 grid of the (reconstructed) stream
        int rw = std::min(inW_ - 1, 512) & ~7, rh = std::min(inH_ - 1, 512) & ~7;
        int rx = ((inW_ - rw) / 2) & ~7, ry = ((inH_ - rh) / 2) & ~7;
        if (rw >= 16 && rh >= 16) {
            qualityBuf_.clear(ctx);
            gpu::PassCB p{};
            p.gPassI = u4(uint32_t(rx), uint32_t(ry), uint32_t(rw), uint32_t(rh));
            g_.setPass(p);
            dispatchCompute(ctx, g_.cs(ShaderId::quality_cs), {cur_.srv.Get()}, {qualityBuf_.uav()}, groups(rw, 16), groups(rh, 16));
            qualityBuf_.requestReadback(ctx);
        }
    }
    if (timer) timer->mark(ctx, GpuStage::Analysis);

    // --- Optical flow
    if (useFlow && prevPyr.valid) flowValid = flow_.compute(g_, pyr, prevPyr, c.flowQuality);
    else flow_.invalidate();
    if (timer) timer->mark(ctx, GpuStage::Flow);
    if (flowValid) {
        flags |= BGN_FLAG_FLOW_VALID;
        updateFrameCB(params, flags);
    }

    // --- Stream compression cleanup
    ID3D11ShaderResourceView* stage = cur_.srv.Get();
    if (c.deblock > 0 || c.deband > 0 || c.denoise > 0) {
        dispatchCompute(ctx, g_.cs(ShaderId::cleanup_cs), {cur_.srv.Get()}, {clean_.uav.Get()}, groups(inW_, 8), groups(inH_, 8));
        stage = clean_.srv.Get();
    }
    if (timer) timer->mark(ctx, GpuStage::Cleanup);

    // --- Temporal reconstruction (history is kept even when strength is 0 so it can start instantly)
    curStable_ ^= 1;
    GpuTexture& stableOut = stable_[curStable_];
    GpuTexture& stableHist = stable_[curStable_ ^ 1];
    dispatchCompute(ctx, g_.cs(ShaderId::temporal_cs), {stage, stableHist.srv.Get(), flowValid ? flow_.flowSrv() : nullptr}, {stableOut.uav.Get()},
                    groups(inW_, 8), groups(inH_, 8));
    stage = stableOut.srv.Get();
    historyValid_ = true;
    if (timer) timer->mark(ctx, GpuStage::Temporal);

    // --- Deblur
    if (c.deblur > 0) {
        dispatchCompute(ctx, g_.cs(ShaderId::deblur_cs), {stage, flowValid ? flow_.flowSrv() : nullptr}, {deblurred_.uav.Get()}, groups(inW_, 8),
                        groups(inH_, 8));
        stage = deblurred_.srv.Get();
    }
    if (timer) timer->mark(ctx, GpuStage::Deblur);

    // --- Super resolution / resampling to the output size
    runUpscale(params, stage, timer);
    ID3D11ShaderResourceView* upSrv = (status_.upscalerUsed == UpscalerKind::None && geo_.outW == inW_ && geo_.outH == inH_) ? stage : up_.srv.Get();
    if (timer) timer->mark(ctx, GpuStage::Upscale);

    // --- Adaptive sharpen + color + output encoding
    curFinal_ ^= 1;
    GpuTexture& fin = final_[curFinal_];
    dispatchCompute(ctx, g_.cs(ShaderId::finish_cs), {upSrv, flowValid ? flow_.flowSrv() : nullptr, pyr.level[2].srv.Get()}, {fin.uav.Get()},
                    groups(geo_.outW, 8), groups(geo_.outH, 8));
    if (timer) timer->mark(ctx, GpuStage::Finish);

    prevFinalValid_ = prevPyr.valid; // previous final exists and geometry unchanged
    status_.flowActive = flowValid;
    status_.temporalActive = c.temporal > 0 && historyValid_;
    status_.vramBytes = capCopy_.bytes() + ingestFull_.bytes() + cur_.bytes() * 5 + hr_.bytes() + up_.bytes() + final_[0].bytes() * 2 + nsr_.vramBytes() +
                        interp_.vramBytes();
    return true;
}

void Pipeline::runUpscale(const PipelineFrameParams& params, ID3D11ShaderResourceView* src, GpuTimer*) {
    auto* ctx = g_.ctx;
    const int outW = geo_.outW, outH = geo_.outH;
    const double scale = double(outW) / double(inW_);
    UpscalerKind kind = params.cfg.upscaler;
    if (std::fabs(scale - 1.0) < 0.02 && outH == inH_) {
        status_.upscalerUsed = UpscalerKind::None;
        return;
    }
    if (scale < 1.0) kind = UpscalerKind::LanczosAR; // downscaling path
    if ((kind == UpscalerKind::NsrT || kind == UpscalerKind::NsrS || kind == UpscalerKind::NsrL) && scale < 1.15) kind = UpscalerKind::LanczosAR;
    status_.upscalerUsed = kind;
    up_.ensure(g_.dev, outW, outH, kWork);

    auto resample = [&](ShaderId id, ID3D11ShaderResourceView* s, int sw, int sh) {
        gpu::PassCB p{};
        p.gPassI = u4(uint32_t(outW), uint32_t(outH), uint32_t(sw), uint32_t(sh));
        g_.setPass(p);
        dispatchCompute(ctx, g_.cs(id), {s}, {up_.uav.Get()}, groups(outW, 8), groups(outH, 8));
    };

    switch (kind) {
    case UpscalerKind::None:
    case UpscalerKind::Bilinear: resample(ShaderId::resample_bilinear_cs, src, inW_, inH_); break;
    case UpscalerKind::LanczosAR: resample(scale >= 1.0 ? ShaderId::resample_up_cs : ShaderId::resample_down_cs, src, inW_, inH_); break;
    case UpscalerKind::NsrT:
    case UpscalerKind::NsrS:
    case UpscalerKind::NsrL: {
        NsrModel model = kind == UpscalerKind::NsrL ? NsrModel::Large : (kind == UpscalerKind::NsrS ? NsrModel::Small : NsrModel::Tiny);
        const int hw = inW_ * 2, hh = inH_ * 2;
        if (!nsr_.ensure(g_.dev, inW_, inH_, model)) {
            resample(ShaderId::resample_up_cs, src, inW_, inH_);
            status_.upscalerUsed = UpscalerKind::LanczosAR;
            break;
        }
        if (hw == outW && hh == outH) {
            nsr_.run(g_, src, up_.uav.Get(), model);
        } else {
            hr_.ensure(g_.dev, hw, hh, kWork);
            nsr_.run(g_, src, hr_.uav.Get(), model);
            resample(hw > outW ? ShaderId::resample_down_cs : ShaderId::resample_up_cs, hr_.srv.Get(), hw, hh);
        }
        break;
    }
    }
}

bool Pipeline::interpolate(GpuTimer* timer) {
    if (!canInterpolate()) return false;
    auto* ctx = g_.ctx;
    if (!interp_.ensure(g_.dev, geo_.outW, geo_.outH)) return false;
    ID3D11Buffer* cbs[1] = {frameCB_.get()};
    ID3D11SamplerState* samplers[2] = {linear_.Get(), point_.Get()};
    ctx->CSSetConstantBuffers(0, 1, cbs);
    ctx->CSSetSamplers(0, 2, samplers);
    interp_.run(g_, final_[curFinal_ ^ 1].srv.Get(), final_[curFinal_].srv.Get(), flow_.flowSrv());
    if (timer) timer->mark(ctx, GpuStage::Interpolate);
    return true;
}

bool Pipeline::extrapolate(float e, GpuTimer* timer) {
    if (!canExtrapolate()) return false;
    auto* ctx = g_.ctx;
    if (!interp_.ensure(g_.dev, geo_.outW, geo_.outH)) return false;
    gpu::FrameCB cb = cb_;
    cb.gInterp.x = std::clamp(e, 0.0f, 1.0f);
    frameCB_.update(ctx, cb);
    ID3D11Buffer* cbs[1] = {frameCB_.get()};
    ID3D11SamplerState* samplers[2] = {linear_.Get(), point_.Get()};
    ctx->CSSetConstantBuffers(0, 1, cbs);
    ctx->CSSetSamplers(0, 2, samplers);
    interp_.extrapolate(g_, final_[curFinal_].srv.Get(), flow_.flowSrv());
    frameCB_.update(ctx, cb_);
    if (timer) timer->mark(ctx, GpuStage::Interpolate);
    return true;
}

void Pipeline::present(ID3D11RenderTargetView* rtv, int bbW, int bbH, const RECT& dst, bool interpolated, uint64_t presentIndex, bool ditherHdr,
                       const OsdOverlay* osd) {
    auto* ctx = g_.ctx;
    gpu::FrameCB cb = cb_;
    cb.gOsd = f4(0, 0, 1, 0);
    if (osd && osd->cols > 0 && osd->rows > 0) {
        const float sc = float(std::clamp(osd->scale, 1, 4));
        const float boxW = (osd->cols * 8.0f + 10.0f) * sc, boxH = (osd->rows * 12.0f + 8.0f) * sc, m = 12.0f;
        const bool right = osd->position == 1 || osd->position == 3, bottom = osd->position >= 2;
        const float x = right ? float(dst.right) - boxW - m : float(dst.left) + m;
        const float y = bottom ? float(dst.bottom) - boxH - m : float(dst.top) + m;
        cb.gOsd = f4(std::floor(x), std::floor(y), sc, 1.0f);
        cb.gOsd2 = f4(float(osd->cols), float(osd->rows), 0.62f, 0);
        for (int i = 0; i < BGN_OSD_TEXT_VECTORS; ++i)
            cb.gOsdText[i] = u4(osd->words[i * 4], osd->words[i * 4 + 1], osd->words[i * 4 + 2], osd->words[i * 4 + 3]);
    }
    cb.gFrame.x = uint32_t(presentIndex);
    cb.gPresent = f4(float(dst.left), float(dst.top), float(dst.right - dst.left), float(dst.bottom - dst.top));
    const bool hdrOut = cb.gFrame.z == BGN_OUTPUT_SCRGB;
    cb.gPresent2 = f4(float(bbW), float(bbH), hdrOut ? (ditherHdr ? 0.0005f : 0.0f) : 1.0f / 1023.0f, cb_.gPresent2.w);
    frameCB_.update(ctx, cb);
    D3D11_VIEWPORT vp{0, 0, float(bbW), float(bbH), 0, 1};
    ctx->RSSetViewports(1, &vp);
    ctx->RSSetState(raster_.Get());
    ctx->OMSetBlendState(blend_.Get(), nullptr, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(depth_.Get(), 0);
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(shaders_->vs(ShaderId::present_vs), nullptr, 0);
    ctx->PSSetShader(shaders_->ps(ShaderId::present_ps), nullptr, 0);
    ID3D11Buffer* cbs[1] = {frameCB_.get()};
    ctx->PSSetConstantBuffers(0, 1, cbs);
    ID3D11SamplerState* samplers[2] = {linear_.Get(), point_.Get()};
    ctx->PSSetSamplers(0, 2, samplers);
    ID3D11ShaderResourceView* srvs[2] = {interpolated ? interp_.midSrv() : final_[curFinal_].srv.Get(), cur_.srv.Get()};
    ctx->PSSetShaderResources(0, 2, srvs);
    ctx->Draw(3, 0);
    ID3D11ShaderResourceView* nulls[2] = {};
    ctx->PSSetShaderResources(0, 2, nulls);
    ID3D11RenderTargetView* nullRtv = nullptr;
    ctx->OMSetRenderTargets(1, &nullRtv, nullptr);
}

void Pipeline::pollReadbacks() {
    if (!device_) return;
    auto* ctx = g_.ctx;
    uint32_t stats[kStatsWords];
    while (statsBuf_.tryRead(ctx, stats, sizeof(stats))) {
        double count = stats[65];
        if (count < 16) continue;
        // percentiles from the 64-bin histogram
        auto percentile = [&](double q) {
            double target = q * count, acc = 0;
            for (int i = 0; i < 64; ++i) {
                acc += stats[i];
                if (acc >= target) return (i + 0.5) / 64.0;
            }
            return 1.0;
        };
        double p01 = percentile(0.005), p997 = percentile(0.997);
        double avg = double(stats[64]) / 1024.0 / count;
        // Conservative auto levels: never crush more than 4% black / 6% white
        double targetBlack = std::clamp(p01 - 0.01, 0.0, 0.04);
        double targetWhite = std::clamp(p997 + 0.02, 0.94, 1.0);
        const double k = 0.08; // smoothing (~1 s at 60 fps / 4)
        autoBlack_ += (targetBlack - autoBlack_) * k;
        autoWhite_ += (targetWhite - autoWhite_) * k;
        sceneLuma_ += (avg - sceneLuma_) * k;
        status_.sceneLuma = sceneLuma_;
    }
    uint32_t content[kContentWords];
    while (contentBuf_.tryRead(ctx, content, sizeof(content))) {
        if (content[2] == 0) continue;
        content_.topBand = double(content[0]) / 65536.0 / double(content[2]);
        content_.refBand = double(content[1]) / 65536.0 / double(content[2]);
        contentFresh_ = true;
        status_.contentFactor = estimateUpscaleFactor(content_);
    }
    uint32_t quality[kQualityWords];
    while (qualityBuf_.tryRead(ctx, quality, sizeof(quality))) {
        if (quality[1] < 64 || quality[3] < 64) continue;
        qualityBoundary_ = double(quality[0]) / 4096.0 / double(quality[1]);
        qualityInterior_ = double(quality[2]) / 4096.0 / double(quality[3]);
        qualityFresh_ = true;
    }
}

bool Pipeline::takeQualityMeasurement(double& boundary, double& interior) {
    if (!qualityFresh_) return false;
    boundary = qualityBoundary_;
    interior = qualityInterior_;
    qualityFresh_ = false;
    return true;
}

bool Pipeline::takeContentMeasurement(ContentResMeasurement& out) {
    if (!contentFresh_) return false;
    out = content_;
    contentFresh_ = false;
    return true;
}

} // namespace bgn
