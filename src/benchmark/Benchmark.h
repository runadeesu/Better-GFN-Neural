#pragma once
// In-app benchmark: renders a synthetic cloud-game scene on the GPU and runs
// the full processing pipeline at every quality tier, measuring GPU time with
// timestamp queries. Produces a recommended preset / output / interpolation.

#include <atomic>
#include <functional>
#include <string>

#include "settings/Settings.h"

namespace bgn {

struct BenchmarkOptions {
    int inW = 1920, inH = 1080;
    int outW = 3840, outH = 2160;
    int framesPerTier = 90;
    int warmupFrames = 12;
    double refreshHz = 60.0;
    double streamFps = 60.0;
    bool forceWarp = false;
    std::function<void(float, const std::string&)> progress;
    std::atomic<bool>* cancel = nullptr;
};

// Runs synchronously (call from a worker thread). Creates its own GPU device.
BenchmarkResult runBenchmark(const BenchmarkOptions& opt, std::string& error);

std::string benchmarkResultToJson(const BenchmarkResult& r);

} // namespace bgn
