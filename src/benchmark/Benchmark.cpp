#include "benchmark/Benchmark.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <format>
#include <thread>

#include <nlohmann/json.hpp>

#include "automode/AutoModeController.h"
#include "benchmark/GpuTestUtil.h"
#include "core/Log.h"
#include "filters/Pipeline.h"
#include "renderer/GpuTimer.h"
#include "settings/Presets.h"
#include "telemetry/RollingStats.h"

namespace bgn {

namespace {

std::string utcNow() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_s(&tm, &t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M UTC", &tm);
    return buf;
}

Preset presetForTier(int tier) {
    if (tier >= 6) return Preset::Ultra;
    if (tier >= 5) return Preset::Quality;
    if (tier >= 3) return Preset::Balanced;
    return Preset::Performance;
}

} // namespace

BenchmarkResult runBenchmark(const BenchmarkOptions& opt, std::string& error) {
    BenchmarkResult r;
    GpuDevice device;
    GpuDeviceOptions dopt;
    dopt.forceWarp = opt.forceWarp;
    if (!device.create(dopt)) {
        error = "No Direct3D 11 GPU available";
        return r;
    }
    ShaderLibrary shaders;
    if (!shaders.init(device.device())) {
        error = shaders.error();
        return r;
    }
    Pipeline pipeline;
    if (!pipeline.init(device, shaders)) {
        error = "Pipeline initialization failed";
        return r;
    }
    GpuTimer timer;
    timer.init(device.device());
    ConstantBuffer<gpu::PassCB> pass;
    pass.create(device.device());
    GpuContext g{device.device(), device.context(), &shaders, &pass, device.info().halfPrecision};
    GpuTexture scene;
    if (!scene.create(device.device(), opt.inW, opt.inH, DXGI_FORMAT_R8G8B8A8_UNORM, true)) {
        error = "Out of GPU memory";
        return r;
    }
    PipelineGeometry geo{opt.inW, opt.inH, 0, 0, opt.outW, opt.outH};
    if (!pipeline.configure(geo)) {
        error = "Out of GPU memory at the selected output resolution";
        return r;
    }

    r.gpuName = device.info().name;
    r.backend = std::string("Direct3D 11 compute") + (device.info().software ? " (WARP software)" : "") + (device.info().halfPrecision ? ", FP16" : ", FP32");
    r.inputResolution = std::format("{}x{}", opt.inW, opt.inH);
    r.outputResolution = std::format("{}x{}", opt.outW, opt.outH);
    r.tierAvgMs.assign(kTierCount, 0.0);
    r.tierMaxMs.assign(kTierCount, 0.0);
    RollingStats fg(512);
    auto* ctx = device.context();
    const int frames = opt.framesPerTier + opt.warmupFrames;
    const int totalSteps = kTierCount * frames;
    int step = 0;

    for (int tier = 0; tier < kTierCount; ++tier) {
        EnhancementSettings e;
        e.frameGen = FrameGenMode::X2;
        PipelineFrameParams params;
        params.cfg = resolveConfig(e, tier, true);
        pipeline.resetHistory();
        RollingStats times(1024);
        int collected = 0;
        for (int i = 0; i < frames; ++i, ++step) {
            if (opt.cancel && opt.cancel->load()) {
                error = "Cancelled";
                return BenchmarkResult{};
            }
            renderSyntheticScene(g, scene.uav.Get(), opt.inW, opt.inH, float(i) / float(opt.streamFps), 240.0f);
            CaptureInput in;
            in.texture = scene.tex.Get();
            in.crop = RECT{0, 0, opt.inW, opt.inH};
            pipeline.process(in, params, &timer);
            if (tier >= 3) pipeline.interpolate(&timer);
            timer.endFrame(ctx);
            ctx->Flush();
            GpuFrameTiming t;
            while (timer.collect(ctx, t)) {
                if (int(t.frameId) > 0 && collected++ >= opt.warmupFrames) {
                    double interp = t.stageMs[int(GpuStage::Interpolate)];
                    times.add(t.totalMs - interp);
                    if (interp > 0) fg.add(interp);
                }
            }
            if (opt.progress && (step % 8 == 0)) opt.progress(float(step) / float(totalSteps), std::format("Measuring {} quality", tierName(tier)));
        }
        // Drain remaining queries
        for (int spin = 0; spin < 200; ++spin) {
            GpuFrameTiming t;
            bool any = false;
            while (timer.collect(ctx, t)) {
                any = true;
                if (collected++ >= opt.warmupFrames) {
                    double interp = t.stageMs[int(GpuStage::Interpolate)];
                    times.add(t.totalMs - interp);
                    if (interp > 0) fg.add(interp);
                }
            }
            if (!any) std::this_thread::sleep_for(std::chrono::milliseconds(2));
            if (collected >= frames) break;
        }
        r.tierAvgMs[tier] = times.mean();
        r.tierMaxMs[tier] = times.max();
        BGN_LOG_INFO("Benchmark", "tier {} ({}): avg {:.2f} ms, max {:.2f} ms", tier, tierName(tier), r.tierAvgMs[tier], r.tierMaxMs[tier]);
    }
    r.frameGenMs = fg.mean();

    // Recommendation: highest tier whose p95-ish cost fits half of a 60 fps frame
    const double budget = AutoModeController::gpuBudgetMs(opt.streamFps, 0.5);
    int best = 0;
    for (int t = 0; t < kTierCount; ++t)
        if (r.tierAvgMs[t] > 0 && (r.tierAvgMs[t] * 0.7 + r.tierMaxMs[t] * 0.3) <= budget) best = t;
    r.recommendedTier = best;
    r.recommendedPreset = presetForTier(best);
    r.avgMs = r.tierAvgMs[best];
    r.maxMs = r.tierMaxMs[best];
    r.recommendedOutput = best >= 3 ? std::format("{}x{} (monitor)", opt.outW, opt.outH) : (opt.outH > 1440 ? "2560x1440" : std::format("{}x{}", opt.outW, opt.outH));
    const bool fgFits = r.tierAvgMs[best] + r.frameGenMs <= budget * 0.9;
    r.recommendedFrameGen = (fgFits && opt.refreshHz >= opt.streamFps * 1.8) ? FrameGenMode::Auto : FrameGenMode::Off;
    r.dateUtc = utcNow();
    r.valid = true;
    if (opt.progress) opt.progress(1.0f, "Done");
    return r;
}

std::string benchmarkResultToJson(const BenchmarkResult& r) {
    nlohmann::json j{{"gpu", r.gpuName},
                     {"backend", r.backend},
                     {"input", r.inputResolution},
                     {"output", r.outputResolution},
                     {"tier_avg_ms", r.tierAvgMs},
                     {"tier_max_ms", r.tierMaxMs},
                     {"frame_interpolation_ms", r.frameGenMs},
                     {"recommended_tier", r.recommendedTier},
                     {"recommended_preset", toString(r.recommendedPreset)},
                     {"recommended_output", r.recommendedOutput},
                     {"recommended_frame_interpolation", toString(r.recommendedFrameGen)},
                     {"avg_ms", r.avgMs},
                     {"max_ms", r.maxMs},
                     {"date_utc", r.dateUtc}};
    return j.dump(2);
}

} // namespace bgn
