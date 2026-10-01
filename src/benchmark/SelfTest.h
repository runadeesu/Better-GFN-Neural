#pragma once
// Headless GPU self-test (BetterGFNNeural.exe --selftest). Verifies on the
// actual GPU (or WARP) that every shader compiles into a working pipeline,
// that the NSR shaders reproduce the CPU reference of the trained network,
// that optical-flow interpolation beats naive blending on moving content, and
// that every quality tier produces valid output at common resolutions.

#include <string>

namespace bgn {

struct SelfTestOptions {
    bool forceWarp = false;
    bool quick = false;
};

// Returns true if all checks passed. |json| receives a machine-readable report.
bool runSelfTest(const SelfTestOptions& opt, std::string& json);

} // namespace bgn
