#include "benchmark/SelfTest.h"

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
    nsrUpscaleReference(model == NsrModel::Large ? nsrModelL() : nsrModelS(), img, w, h, ref);
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
    const bool half = c.g.halfPrecision;
    const double maxTol = half ? 0.04 : 0.012, meanTol = half ? 0.005 : 0.0015;
    ok = maxErr < maxTol && meanErr < meanTol;
    j = {{"model", model == NsrModel::Large ? "nsr_l_x2" : "nsr_s_x2"}, {"half_precision", half}, {"max_abs_error", maxErr}, {"mean_abs_error", meanErr},
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
    for (NsrModel m : {NsrModel::Small, NsrModel::Large}) {
        bool ok = false;
        nlohmann::json j = testNsr(c, m, ok);
        if (gi.halfPrecision) {
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
    std::vector<Geo> geos = gi.software || opt.quick ? std::vector<Geo>{{640, 360, 1280, 720, 0}, {960, 540, 960, 540, 0}, {640, 360, 1600, 900, 0}, {800, 450, 1280, 720, 0}, {1280, 720, 1280, 720, 360}}
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
    }

    // ---- Temporal anti-flicker ---------------------------------------------
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
    }

    report["pass"] = allOk;
    out = report.dump(2);
    return allOk;
}

} // namespace bgn
