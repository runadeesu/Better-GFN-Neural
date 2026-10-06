#pragma once
// Stream quality monitor (portable).
//
// Two signals describe how good the GeForce NOW stream currently looks:
//   * compression: blockiness on the 8x8 coding grid, measured on the GPU
//     (shaders/quality.hlsl) as the ratio of luma steps across block
//     boundaries to steps inside blocks. ~1.0 = clean, >1.3 = visible blocks.
//   * smoothness: how regular the arrival of new frames is. Network hiccups
//     show up as frame intervals much longer than the typical one (stutter).
// They are combined into a 0..100 score shown in the UI and recorded in the
// session history; the blockiness also drives the adaptive cleanup.

#include <deque>
#include <string>

namespace bgn {

// Converts the GPU sums into a 0..1 blockiness value.
double blockinessFromMeans(double boundaryMeanStep, double interiorMeanStep);

struct StreamQuality {
    bool valid = false;
    double blockiness = 0;   // 0 clean .. 1 heavy blocking
    double stutter = 0;      // fraction of frame intervals > 1.8x the median (0..1)
    double score = 0;        // 0..100
    const char* label() const; // "Excellent", "Good", "Fair", "Poor" (English, translate in UI)
};

double qualityScore(double blockiness, double stutter);

class StreamQualityTracker {
public:
    void reset();
    void addBlockinessSample(double boundaryMeanStep, double interiorMeanStep);
    void addFrameInterval(double ms);
    StreamQuality current() const;

private:
    double blockiness_ = 0;
    bool haveBlockiness_ = false;
    std::deque<double> intervals_; // last ~4 s of input frame intervals
};

} // namespace bgn
