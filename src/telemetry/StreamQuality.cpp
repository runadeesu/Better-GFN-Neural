#include "telemetry/StreamQuality.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace bgn {

double blockinessFromMeans(double boundary, double interior) {
    if (!(boundary >= 0) || !(interior >= 0)) return 0.0;
    // The epsilon keeps flat, noise-free content (menus, sky) from producing
    // large ratios out of tiny numbers.
    constexpr double eps = 0.002;
    const double ratio = (boundary + eps) / (interior + eps);
    return std::clamp((ratio - 1.0) / 0.8, 0.0, 1.0);
}

double qualityScore(double blockiness, double stutter) {
    const double s = 100.0 - 60.0 * std::clamp(blockiness, 0.0, 1.0) - 40.0 * std::clamp(stutter * 4.0, 0.0, 1.0);
    return std::clamp(s, 0.0, 100.0);
}

const char* StreamQuality::label() const {
    if (!valid) return "-";
    if (score >= 80) return "Excellent";
    if (score >= 60) return "Good";
    if (score >= 40) return "Fair";
    return "Poor";
}

void StreamQualityTracker::reset() {
    blockiness_ = 0;
    haveBlockiness_ = false;
    intervals_.clear();
}

void StreamQualityTracker::addBlockinessSample(double boundary, double interior) {
    const double b = blockinessFromMeans(boundary, interior);
    if (!haveBlockiness_) {
        blockiness_ = b;
        haveBlockiness_ = true;
    } else {
        blockiness_ += (b - blockiness_) * 0.2; // ~1 s smoothing at 4 samples/s
    }
}

void StreamQualityTracker::addFrameInterval(double ms) {
    if (!(ms > 0) || ms > 2000) return; // paused / unfocused gaps are not stutter
    intervals_.push_back(ms);
    while (intervals_.size() > 240) intervals_.pop_front();
}

StreamQuality StreamQualityTracker::current() const {
    StreamQuality q;
    q.valid = haveBlockiness_ || intervals_.size() >= 30;
    q.blockiness = blockiness_;
    if (intervals_.size() >= 30) {
        std::vector<double> v(intervals_.begin(), intervals_.end());
        std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
        const double median = v[v.size() / 2];
        size_t longOnes = 0;
        for (double x : intervals_)
            if (x > median * 1.8) ++longOnes;
        q.stutter = double(longOnes) / double(intervals_.size());
    }
    q.score = qualityScore(q.blockiness, q.stutter);
    return q;
}

} // namespace bgn
