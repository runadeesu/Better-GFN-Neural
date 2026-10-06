#include "benchmark/SelfTest.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <random>
#include <vector>

#include <nlohmann/json.hpp>

#include "benchmark/GpuTestUtil.h"
#include "core/Log.h"
#include "filters/Pipeline.h"
#include "neural/NsrReference.h"
#include "neural/NsrUpscaler.h"
#include "renderer/GpuTimer.h"
#include "settings/Presets.h"
#include "telemetry/StreamQuality.h"

namespace bgn {

namespace {

struct Ctx {
    GpuDevice device;
    ShaderLibrary shaders;
    ConstantBuffer<gpu::PassCB> pass;
    GpuContext g;
};

double mae(const std::vector<float>& a, const std::vector<float>& b) {
    double s = 0;
    size_t n = 0;
    for (size_t i = 0; i + 3 < a.size() && i + 3 < b.size(); i += 4) {
        for (int c = 0; c < 3; ++c) s += std::fabs(double(a[i + c]) - double(b[i + c]));
        n += 3;
    }
    return n ? s / double(n) : 0.0;
}

bool createInitTexture(ID3D11Device* dev, int w, int h, const std::vector<float>& rgb, ComPtr<ID3D11Texture2D>& tex, ComPtr<ID3D11ShaderResourceView>& srv) {
    std::vector<uint16_t> data(size_t(w) * h * 4);
    for (size_t i = 0; i < size_t(w) * h; ++i) {
        for (int c = 0; c < 3; ++c) data[i * 4 + c] = floatToHalf(rgb[i * 3 + c]);
        data[i * 4 + 3] = floatToHalf(1.0f);
    }
    D3D11_TEXTURE2D_DESC d{};
    d.Width = UINT(w);
    d.Height = UINT(h);
    d.MipLevels = d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    d.SampleDesc.Count = 1;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{data.data(), UINT(w * 8), 0};
    if (FAILED(dev->CreateTexture2D(&d, &init, &tex))) return false;
    return SUCCEEDED(dev->CreateShaderResourceView(tex.Get(), nullptr, &srv));
}

// Test image: gradients, edges, diagonal lines, text-like blocks and noise.
std::vector<float> makeTestImage(int w, int h) {
    std::vector<float> img(size_t(w) * h * 3);
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(0, 1);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float* p = &img[(size_t(y) * w + x) * 3];
            float g = float(x) / w;
            p[0] = g;
            p[1] = float(y) / h;
            p[2] = 0.5f;
            if (((x / 4) + (y / 4)) % 2 == 0 && x < w / 2) p[0] = p[1] = p[2] = 0.9f;
            if ((x + y) % 7 == 0) p[0] = p[1] = p[2] = 0.05f;
            for (int c = 0; c < 3; ++c) p[c] = std::clamp(p[c] + (u(rng) - 0.5f) * 0.04f, 0.0f, 1.0f);
        }
    return img;
}

nlohmann::json testNsr(Ctx& c, NsrModel model, bool& ok) {
    const int w = 40, h = 32;
    std::vector<float> img = makeTestImage(w, h);
    nlohmann::json j;
    ComPtr<ID3D11Texture2D> srcTex;
    ComPtr<ID3D11ShaderResourceView> srcSrv;
    GpuTexture out;
    NsrUpscaler nsr;
    if (!createInitTexture(c.device.device(), w, h, img, srcTex, srcSrv) || !out.create(c.device.device(), w * 2, h * 2, DXGI_FORMAT_R16G16B16A16_FLOAT) ||
        !nsr.ensure(c.device.device(), w, h, model)) {
        ok = false;
        j["error"] = "resource creation failed";
        return j;
    }
    nsr.run(c.g, srcSrv.Get(), out.uav.Get(), model);
    std::vector<float> gpu;
    int ow = 0, oh = 0;
    readbackTexture(c.device.device(), c.device.context(), out.tex.Get(), gpu, ow, oh);
    std::vector<float> ref;
    nsrUpscaleReference(model == NsrModel::Large ? nsrModelL() : (model == NsrModel::Small ? nsrModelS() : nsrModelT()), img, w, h, ref);
    double maxErr = 0, sumErr = 0;
    size_t n = 0;
    for (int y = 0; y < oh; ++y)
        for (int x = 0; x < ow; ++x)
            for (int ch = 0; ch < 3; ++ch) {
                double e = std::fabs(double(gpu[(size_t(y) * ow + x) * 4 + ch]) - double(ref[(size_t(y) * ow + x) * 3 + ch]));
                maxErr = std::max(maxErr, e);
                sumErr += e;
                ++n;
            }
    double meanErr = n ? sumErr / double(n) : 1.0;
    const bool half = c.g.halfPrecision && model != NsrModel::Tiny; // NSR-T has a single FP32 variant
    const double maxTol = half ? 0.04 : 0.012, meanTol = half ? 0.005 : 0.0015;
    ok = maxErr < maxTol && meanErr < meanTol;
    j = {{"model", model == NsrModel::Large ? "nsr_l_x2" : (model == NsrModel::Small ? "nsr_s_x2" : "nsr_t_x2")}, {"half_precision", half && model != NsrModel::Tiny}, {"max_abs_error", maxErr}, {"mean_abs_error", meanErr},
         {"pass", ok}};
    return j;
}

EnhancementSettings plainSettings() {
    EnhancementSettings e;
    for (Feature* f : {&e.denoise, &e.deblock, &e.deband, &e.temporal, &e.deblur, &e.sharpen}) f->enabled = false;
    e.colorEnabled = false;
    e.upscale = UpscaleMode::Native;
    e.frameGen = FrameGenMode::Off;
    e.motionDeblur = false;
    return e;
}

bool processSynthetic(Ctx& c, Pipeline& p, GpuTexture& scene, const PipelineFrameParams& params, int w, int h, float t, float pan) {
    renderSyntheticScene(c.g, scene.uav.Get(), w, h, t, pan);
    CaptureInput in;
    in.texture = scene.tex.Get();
    in.crop = RECT{0, 0, w, h};
    return p.process(in, params, nullptr);
}

} // namespace

bool runSelfTest(const SelfTestOptions& opt, std::string& out) {
    nlohmann::json report;
    bool allOk = true;
    Ctx c;
    GpuDeviceOptions dopt;
    dopt.forceWarp = opt.forceWarp;
    if (!c.device.create(dopt)) {
        report["device"] = {{"pass", false}};
        out = report.dump(2);
        return false;
    }
    const GpuInfo& gi = c.device.info();
    report["device"] = {{"pass", true},
                        {"name", gi.name},
                        {"family", gi.family},
                        {"software", gi.software},
                        {"feature_level", std::format("{:x}", unsigned(gi.featureLevel))},
                        {"half_precision", gi.halfPrecision},
                        {"vram_mb", gi.dedicatedVramMB}};
    bool shadersOk = c.shaders.init(c.device.device());
    report["shaders"] = {{"pass", shadersOk}, {"count", int(ShaderId::Count)}, {"error", c.shaders.error()}};
    if (!shadersOk) {
        out = report.dump(2);
        return false;
    }
    c.pass.create(c.device.device());
    c.g = GpuContext{c.device.device(), c.device.context(), &c.shaders, &c.pass, gi.halfPrecision};

    // ---- NSR shader == trained network ------------------------------------
    BGN_LOG_INFO("SelfTest", "device: {} (software {})", gi.name, gi.software);
    for (NsrModel m : {NsrModel::Tiny, NsrModel::Small, NsrModel::Large}) {
        BGN_LOG_INFO("SelfTest", "NSR {} vs CPU reference", m == NsrModel::Large ? "L" : (m == NsrModel::Small ? "S" : "T"));
        bool ok = false;
        nlohmann::json j = testNsr(c, m, ok);
        if (gi.halfPrecision && m != NsrModel::Tiny) {
            // Also verify the FP32 variant
            c.g.halfPrecision = false;
            bool ok32 = false;
            j["fp32"] = testNsr(c, m, ok32);
            c.g.halfPrecision = true;
            ok = ok && ok32;
        }
        report["nsr"].push_back(j);
        allOk &= ok;
    }

    // ---- Pipeline: every tier, several geometries ---------------------------
    struct Geo {
        int inW, inH, outW, outH, streamH;
    };
    // WARP (software) is ~100x slower than a GPU: use small frames there.
    std::vector<Geo> geos = gi.software || opt.quick ? std::vector<Geo>{{320, 180, 640, 360, 0}, {480, 270, 480, 270, 0}, {320, 180, 800, 450, 0}, {400, 225, 640, 360, 0}, {640, 360, 640, 360, 180}}
                                                     : std::vector<Geo>{{1280, 720, 1920, 1080, 0}, {1920, 1080, 1920, 1080, 0}, {1920, 1080, 3840, 2160, 0}, {2560, 1440, 3840, 2160, 0}, {3840, 2160, 3840, 2160, 1080}};
    Pipeline pipeline;
    if (!pipeline.init(c.device, c.shaders)) {
        report["pipeline"] = {{"pass", false}, {"error", "init failed"}};
        out = report.dump(2);
        return false;
    }
    GpuTimer timer;
    timer.init(c.device.device());
    for (const Geo& geo : geos) {
        GpuTexture scene;
        scene.create(c.device.device(), geo.inW, geo.inH, DXGI_FORMAT_R8G8B8A8_UNORM, true);
        PipelineGeometry pg{geo.inW, geo.inH, geo.streamH ? geo.streamH * geo.inW / geo.inH : 0, geo.streamH, geo.outW, geo.outH};
        bool geoOk = pipeline.configure(pg);
        BGN_LOG_INFO("SelfTest", "pipeline {}x{} -> {}x{} (stream {})", geo.inW, geo.inH, geo.outW, geo.outH, geo.streamH);
        nlohmann::json gj{{"input", std::format("{}x{}", geo.inW, geo.inH)}, {"output", std::format("{}x{}", geo.outW, geo.outH)}, {"stream_height", geo.streamH}};
        for (int tier = 0; tier < kTierCount && geoOk; ++tier) {
            for (int hdr = 0; hdr < 2; ++hdr) {
                if (hdr && tier != 4) continue;
                EnhancementSettings e;
                e.frameGen = FrameGenMode::X2;
                PipelineFrameParams params;
                params.cfg = resolveConfig(e, tier, true);
                params.hdrOutput = hdr == 1;
                params.hdrPlus = hdr == 1;
                params.paperWhiteNits = 200;
                params.peakNits = 1000;
                pipeline.resetHistory();
                bool ok = true;
                const int frames = 4;
                double gpuMs = 0;
                int timed = 0;
                for (int f = 0; f < frames; ++f) {
                    ok &= processSynthetic(c, pipeline, scene, params, geo.inW, geo.inH, 1.0f + f / 60.0f, 300.0f);
                    if (f >= 1) ok &= pipeline.interpolate(nullptr);
                }
                // timing pass
                for (int f = 0; f < 3; ++f) {
                    renderSyntheticScene(c.g, scene.uav.Get(), geo.inW, geo.inH, 2.0f + f / 60.0f, 300.0f);
                    CaptureInput in{scene.tex.Get(), RECT{0, 0, geo.inW, geo.inH}, false};
                    pipeline.process(in, params, &timer);
                    pipeline.interpolate(&timer);
                    timer.endFrame(c.device.context());
                }
                c.device.context()->Flush();
                for (int spin = 0; spin < 500 && timed < 3; ++spin) {
                    GpuFrameTiming t;
                    while (timer.collect(c.device.context(), t)) {
                        gpuMs += t.totalMs;
                        ++timed;
                    }
                    if (timed < 3) Sleep(2);
                }
                std::vector<float> px;
                int w = 0, h = 0;
                readbackTexture(c.device.device(), c.device.context(), pipeline.finalTexture(), px, w, h);
                double mean = 0, maxv = 0;
                bool finite = true;
                for (size_t i = 0; i < px.size(); i += 4)
                    for (int ch = 0; ch < 3; ++ch) {
                        float v = px[i + ch];
                        if (!std::isfinite(v)) finite = false;
                        mean += v;
                        maxv = std::max(maxv, double(v));
                    }
                mean /= std::max<size_t>(1, px.size() / 4 * 3);
                const double maxAllowed = hdr ? 1000.0 / 80.0 + 0.1 : 1.01;
                bool pass = ok && finite && w == geo.outW && h == geo.outH && mean > 0.02 && maxv <= maxAllowed;
                BGN_LOG_INFO("SelfTest", "  tier {} hdr {}: {} mean {:.3f} max {:.3f} gpu {:.2f} ms", tier, hdr, pass ? "PASS" : "FAIL", mean, maxv, timed ? gpuMs / timed : -1.0);
                allOk &= pass;
                gj["tiers"].push_back({{"tier", tier},
                                       {"hdr_output", hdr == 1},
                                       {"upscaler", toString(pipeline.status().upscalerUsed)},
                                       {"mean", mean},
                                       {"max", maxv},
                                       {"gpu_ms", timed ? gpuMs / timed : -1.0},
                                       {"pass", pass}});
            }
        }
        gj["pass"] = geoOk;
        allOk &= geoOk;
        report["pipeline"].push_back(gj);
    }

    // ---- Frame interpolation quality (optical flow vs. naive blend) ----------
    BGN_LOG_INFO("SelfTest", "frame interpolation quality");
    {
        const int w = 640, h = 360;
        const float pan = 480.0f; // 8 px per frame at 60 fps
        GpuTexture scene;
        scene.create(c.device.device(), w, h, DXGI_FORMAT_R8G8B8A8_UNORM, true);
        pipeline.configure(PipelineGeometry{w, h, 0, 0, w, h});
        EnhancementSettings e = plainSettings();
        e.frameGen = FrameGenMode::X2;
        PipelineFrameParams params;
        params.cfg = resolveConfig(e, 4, false);
        const float t0 = 1.0f, dt = 1.0f / 60.0f;
        std::vector<float> f0, f1, mid, ref;
        int ww, hh;
        pipeline.resetHistory();
        processSynthetic(c, pipeline, scene, params, w, h, t0, pan);
        readbackTexture(c.device.device(), c.device.context(), pipeline.finalTexture(), f0, ww, hh);
        processSynthetic(c, pipeline, scene, params, w, h, t0 + dt, pan);
        readbackTexture(c.device.device(), c.device.context(), pipeline.finalTexture(), f1, ww, hh);
        bool interpOk = pipeline.interpolate(nullptr);
        // read the interpolated frame through a temporary texture copy
        ComPtr<ID3D11Resource> midRes;
        pipeline.midSrv()->GetResource(&midRes);
        ComPtr<ID3D11Texture2D> midTex;
        midRes.As(&midTex);
        readbackTexture(c.device.device(), c.device.context(), midTex.Get(), mid, ww, hh);
        pipeline.resetHistory();
        processSynthetic(c, pipeline, scene, params, w, h, t0 + dt * 0.5f, pan);
        readbackTexture(c.device.device(), c.device.context(), pipeline.finalTexture(), ref, ww, hh);
        std::vector<float> blend(f0.size());
        for (size_t i = 0; i < f0.size(); ++i) blend[i] = 0.5f * (f0[i] + f1[i]);
        double eMid = mae(mid, ref), eBlend = mae(blend, ref), eRepeat = mae(f1, ref);
        bool pass = interpOk && eMid < eBlend * 0.85 && eMid < eRepeat;
        allOk &= pass;
        report["frame_interpolation"] = {{"mae_interpolated", eMid}, {"mae_naive_blend", eBlend}, {"mae_frame_repeat", eRepeat}, {"pass", pass}};

        // Stutter smoothing: half a frame of extrapolation beyond f1 must be closer
        // to the true picture at that time than repeating (freezing) f1.
        BGN_LOG_INFO("SelfTest", "stutter smoothing (extrapolation)");
        std::vector<float> ex, truth;
        pipeline.resetHistory();
        processSynthetic(c, pipeline, scene, params, w, h, t0, pan);
        processSynthetic(c, pipeline, scene, params, w, h, t0 + dt, pan);
        bool exOk = pipeline.extrapolate(0.5f, nullptr);
        pipeline.midSrv()->GetResource(&midRes);
        midRes.As(&midTex);
        readbackTexture(c.device.device(), c.device.context(), midTex.Get(), ex, ww, hh);
        pipeline.resetHistory();
        processSynthetic(c, pipeline, scene, params, w, h, t0 + dt * 1.5f, pan);
        readbackTexture(c.device.device(), c.device.context(), pipeline.finalTexture(), truth, ww, hh);
        double eEx = mae(ex, truth), eFreeze = mae(f1, truth);
        bool exPass = exOk && eEx < eFreeze * 0.8;
        allOk &= exPass;
        report["stutter_smoothing"] = {{"mae_extrapolated", eEx}, {"mae_frozen_frame", eFreeze}, {"pass", exPass}};
    }

    // ---- Temporal anti-flicker ---------------------------------------------
    BGN_LOG_INFO("SelfTest", "temporal stability");
    {
        const int w = 640, h = 360;
        GpuTexture scene;
        scene.create(c.device.device(), w, h, DXGI_FORMAT_R8G8B8A8_UNORM, true);
        pipeline.configure(PipelineGeometry{w, h, 0, 0, w, h});
        auto flicker = [&](bool temporal) {
            EnhancementSettings e = plainSettings();
            e.temporal = {temporal, false, 0.8f};
            PipelineFrameParams params;
            params.cfg = resolveConfig(e, 4, false);
            pipeline.resetHistory();
            std::vector<float> prev, cur;
            double sum = 0;
            int n = 0;
            for (int f = 0; f < 12; ++f) {
                processSynthetic(c, pipeline, scene, params, w, h, 1.0f + f / 60.0f, 0.0f); // static camera, per-frame block noise
                int ww, hh;
                readbackTexture(c.device.device(), c.device.context(), pipeline.finalTexture(), cur, ww, hh);
                if (f >= 4) {
                    sum += mae(cur, prev);
                    ++n;
                }
                prev.swap(cur);
            }
            return n ? sum / n : 0.0;
        };
        double off = flicker(false), on = flicker(true);
        bool pass = on < off * 0.7;
        allOk &= pass;
        report["temporal_stability"] = {{"frame_delta_without", off}, {"frame_delta_with", on}, {"reduction", off > 0 ? 1.0 - on / off : 0.0}, {"pass", pass}};

        // Ghosting: on a fast pan the temporally reconstructed frame must stay close
        // to the current frame (a lagging/ghosting result drifts towards the previous one).
        BGN_LOG_INFO("SelfTest", "temporal ghosting");
        auto panned = [&](bool temporal, int tier, std::vector<float>& last, std::vector<float>& beforeLast) {
            EnhancementSettings e = plainSettings();
            e.temporal = {temporal, false, 0.8f};
            PipelineFrameParams params;
            params.cfg = resolveConfig(e, tier, false);
            pipeline.resetHistory();
            int ww, hh;
            for (int f = 0; f < 10; ++f) {
                processSynthetic(c, pipeline, scene, params, w, h, 1.0f + f / 60.0f, 600.0f); // 10 px per frame
                if (f == 8) readbackTexture(c.device.device(), c.device.context(), pipeline.finalTexture(), beforeLast, ww, hh);
            }
            readbackTexture(c.device.device(), c.device.context(), pipeline.finalTexture(), last, ww, hh);
        };
        // Tier 4 (mid-range) and tier 2 (low-end GPUs: NSR-T, fast flow)
        for (int tier : {4, 2}) {
            std::vector<float> onLast, onPrev, offLast, offPrev;
            panned(true, tier, onLast, onPrev);
            panned(false, tier, offLast, offPrev);
            double deviation = mae(onLast, offLast);    // temporal vs. untouched current frame
            double motionDelta = mae(offLast, offPrev); // how different consecutive frames are
            bool ghostPass = deviation < motionDelta * 0.25;
            allOk &= ghostPass;
            report[tier == 4 ? "temporal_ghosting" : "temporal_ghosting_low_end"] = {
                {"tier", tier}, {"deviation_from_current", deviation}, {"consecutive_frame_delta", motionDelta}, {"pass", ghostPass}};
        }
    }

    // ---- Visual styles and accessibility filters ---------------------------
    BGN_LOG_INFO("SelfTest", "visual styles and accessibility");
    {
        const int w = 320, h = 180;
        GpuTexture scene;
        scene.create(c.device.device(), w, h, DXGI_FORMAT_R8G8B8A8_UNORM, true);
        pipeline.configure(PipelineGeometry{w, h, 0, 0, w, h});
        auto render = [&](VisualStyle style, int cvd, float night, std::vector<float>& px) {
            EnhancementSettings e = plainSettings();
            e.style = style;
            e.colorEnabled = true;
            PipelineFrameParams params;
            params.cfg = resolveConfig(e, 4, false);
            params.colorVision = cvd;
            params.colorVisionStrength = 1.0f;
            params.nightLight = night;
            pipeline.resetHistory();
            processSynthetic(c, pipeline, scene, params, w, h, 1.0f, 0.0f);
            int ww = 0, hh = 0;
            return readbackTexture(c.device.device(), c.device.context(), pipeline.finalTexture(), px, ww, hh) && ww == w && hh == h;
        };
        auto sane = [](const std::vector<float>& px) {
            for (float v : px)
                if (!std::isfinite(v) || v < -0.01f || v > 1.01f) return false;
            return !px.empty();
        };
        auto channelMean = [](const std::vector<float>& px, int ch) {
            double s = 0;
            for (size_t i = ch; i < px.size(); i += 4) s += px[i];
            return px.empty() ? 0.0 : s / double(px.size() / 4);
        };
        std::vector<float> natural, px;
        bool ok = render(VisualStyle::Natural, 0, 0.0f, natural) && sane(natural);
        nlohmann::json styles = nlohmann::json::object();
        for (VisualStyle v : {VisualStyle::Vivid, VisualStyle::Cinematic, VisualStyle::Competitive, VisualStyle::Monochrome}) {
            bool r = render(v, 0, 0.0f, px) && sane(px);
            double diff = mae(px, natural);
            bool pass = r && diff > 0.002;
            if (v == VisualStyle::Monochrome) {
                double maxChroma = 0;
                for (size_t i = 0; i + 2 < px.size(); i += 4)
                    maxChroma = std::max(maxChroma, double(std::max({px[i], px[i + 1], px[i + 2]}) - std::min({px[i], px[i + 1], px[i + 2]})));
                pass &= maxChroma < 0.02;
                styles[toString(v)] = {{"diff_vs_natural", diff}, {"max_chroma", maxChroma}, {"pass", pass}};
            } else {
                styles[toString(v)] = {{"diff_vs_natural", diff}, {"pass", pass}};
            }
            ok &= pass;
        }
        nlohmann::json cvd = nlohmann::json::object();
        for (int m = 1; m <= 3; ++m) {
            bool r = render(VisualStyle::Natural, m, 0.0f, px) && sane(px);
            double diff = mae(px, natural);
            bool pass = r && diff > 0.001;
            cvd[toString(ColorVision(m))] = {{"diff_vs_off", diff}, {"pass", pass}};
            ok &= pass;
        }
        bool nr = render(VisualStyle::Natural, 0, 1.0f, px) && sane(px);
        double blueRatio = channelMean(px, 2) / std::max(1e-6, channelMean(natural, 2));
        double redRatio = channelMean(px, 0) / std::max(1e-6, channelMean(natural, 0));
        bool nightPass = nr && blueRatio < 0.8 && redRatio > 0.9;
        ok &= nightPass;
        allOk &= ok;
        report["visual_styles"] = {{"styles", styles},
                                   {"color_vision", cvd},
                                   {"night_light", {{"blue_ratio", blueRatio}, {"red_ratio", redRatio}, {"pass", nightPass}}},
                                   {"pass", ok}};
    }

    // ---- Stream quality monitor (blockiness on the 8x8 grid) -----------------
    BGN_LOG_INFO("SelfTest", "stream quality monitor");
    {
        const int w = 256, h = 160;
        ConstantBuffer<gpu::FrameCB> fcb;
        ReadbackBuffer rb;
        bool ok = fcb.create(c.device.device()) && rb.create(c.device.device(), 16, 1);
        auto measure = [&](const std::vector<float>& rgb, double& blockiness) {
            ComPtr<ID3D11Texture2D> tex;
            ComPtr<ID3D11ShaderResourceView> srv;
            if (!createInitTexture(c.device.device(), w, h, rgb, tex, srv)) return false;
            gpu::FrameCB cb{};
            cb.gIn = f4(float(w), float(h), 1.0f / w, 1.0f / h);
            fcb.update(c.device.context(), cb);
            ID3D11Buffer* cbs[1] = {fcb.get()};
            c.device.context()->CSSetConstantBuffers(0, 1, cbs);
            rb.clear(c.device.context());
            gpu::PassCB p{};
            p.gPassI = u4(0, 0, uint32_t(w - 8), uint32_t(h - 8));
            c.g.setPass(p);
            dispatchCompute(c.device.context(), c.g.cs(ShaderId::quality_cs), {srv.Get()}, {rb.uav()}, groups(w - 8, 16), groups(h - 8, 16));
            rb.requestReadback(c.device.context());
            uint32_t v[4] = {};
            for (int i = 0; i < 400 && !rb.tryRead(c.device.context(), v, sizeof(v)); ++i) {
                c.device.context()->Flush();
                Sleep(5);
            }
            if (v[1] == 0 || v[3] == 0) return false;
            blockiness = blockinessFromMeans(double(v[0]) / 4096.0 / v[1], double(v[2]) / 4096.0 / v[3]);
            return true;
        };
        std::mt19937 rng(11);
        std::uniform_real_distribution<float> u(0, 1);
        std::vector<float> blocky(size_t(w) * h * 3), smooth(size_t(w) * h * 3);
        std::vector<float> blockValue(size_t(w / 8 + 1) * (h / 8 + 1));
        for (auto& b : blockValue) b = 0.35f + 0.3f * u(rng);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                float bv = blockValue[size_t(y / 8) * (w / 8 + 1) + x / 8] + (u(rng) - 0.5f) * 0.01f;
                float sv = 0.3f + 0.4f * (float(x) / w) * (float(y) / h) + (u(rng) - 0.5f) * 0.03f;
                for (int ch = 0; ch < 3; ++ch) {
                    blocky[(size_t(y) * w + x) * 3 + ch] = bv;
                    smooth[(size_t(y) * w + x) * 3 + ch] = sv;
                }
            }
        double bBlocky = -1, bSmooth = -1;
        ok = ok && measure(blocky, bBlocky) && measure(smooth, bSmooth);
        const bool pass = ok && bBlocky > 0.6 && bSmooth < 0.2;
        allOk &= pass;
        report["stream_quality"] = {{"blockiness_blocky_image", bBlocky}, {"blockiness_smooth_image", bSmooth}, {"pass", pass}};
    }

    report["pass"] = allOk;
    BGN_LOG_INFO("SelfTest", "self-test {}", allOk ? "PASSED" : "FAILED");
    out = report.dump(2);
    return allOk;
}

} // namespace bgn
