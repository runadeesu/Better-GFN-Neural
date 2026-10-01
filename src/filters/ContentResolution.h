#pragma once
// Stream ("content") resolution detection. When GeForce NOW runs fullscreen it
// scales a lower resolution stream (e.g. 1080p on a 4K monitor) itself; the
// captured window then contains soft, already-upscaled pixels. This module
// decides from band-energy measurements (shaders/content_res.hlsl, CPU
// reference below) whether the content is effectively lower resolution, so
// Neural Super Resolution can reconstruct from the true resolution. Portable.

#include <vector>

namespace bgn {

struct ContentResMeasurement {
    double topBand = 0;  // mean |Y - G(0.6)|
    double refBand = 0;  // mean |G(1.2) - G(1.7)|
    double ratio() const { return refBand > 0 ? topBand / refBand : 0.0; }
};

// Estimated upscale factor already applied to the content (1.0 = native).
// Returns 0 when the frame has too little texture to decide.
double estimateUpscaleFactor(const ContentResMeasurement& m, double minRefBand = 0.0015);

// Chooses the stream height (from common GFN stream heights) closest to
// windowHeight / factor. Returns 0 (= native) for factors below 1.6, where the
// band measurement cannot reliably tell a mild upscale from compressed native content.
int streamHeightForFactor(int windowHeight, double factor);

// CPU implementation of the shader measurement (used by tests / calibration).
ContentResMeasurement measureContentResolutionCpu(const std::vector<float>& luma, int w, int h);

// Debounces per-measurement decisions: switches only after |stable| equal results.
class ContentResTracker {
public:
    explicit ContentResTracker(int stable = 3) : stable_(stable) {}
    int push(int detectedHeight); // returns the accepted stream height (0 = native)
    int current() const { return accepted_; }
    void reset() { accepted_ = 0; pending_ = -1; count_ = 0; }

private:
    int stable_;
    int accepted_ = 0;
    int pending_ = -1;
    int count_ = 0;
};

} // namespace bgn
