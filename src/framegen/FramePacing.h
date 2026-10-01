#pragma once
// Portable frame pacing math for interpolated presentation (unit tested).

#include <algorithm>
#include <cmath>

namespace bgn {

struct PacingPlan {
    int syncIntervalMid = 1;   // vblanks to hold the interpolated frame
    int syncIntervalReal = 1;  // vblanks to hold the real frame
    bool useInterpolation = false;
};

// With 2x interpolation each input frame interval is split in two halves. Each
// half is held for k vblanks where k = floor(refresh / (2 * inputFps) + 0.25), k >= 1.
// 60 fps @ 120 Hz => 1+1 vblanks, 60 fps @ 240 Hz => 2+2,
// 30 fps @ 144 Hz => 2+2, 60 fps @ 60 Hz => interpolation not used.
inline PacingPlan computePacing(double inputFps, double refreshHz, bool enabled) {
    PacingPlan r;
    if (!enabled || inputFps < 1.0 || refreshHz < 1.0) return r;
    double ratio = refreshHz / (2.0 * inputFps);
    if (ratio < 0.9) return r; // the display cannot show the extra frames
    int k = std::max(1, int(std::floor(ratio + 0.25)));
    r.syncIntervalMid = r.syncIntervalReal = std::min(k, 4);
    r.useInterpolation = true;
    return r;
}

} // namespace bgn
