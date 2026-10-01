#include "filters/ContentResolution.h"

#include <algorithm>
#include <cmath>

namespace bgn {

double estimateUpscaleFactor(const ContentResMeasurement& m, double minRefBand) {
    if (m.refBand < minRefBand) return 0.0;
    const double r = m.ratio();
    // Calibration (Kodak + synthetic HUD scenes, bilinear/Catmull-Rom/Lanczos
    // upscalers, JPEG q35..q85): native >= 1.07, 1.33x ~0.81-1.06, 1.5x ~0.73-0.95,
    // 2x ~0.56-0.74, 3x ~0.40-0.47. Piecewise-linear inverse of the median curve.
    struct P { double r, f; };
    static const P curve[] = {{0.42, 3.0}, {0.66, 2.0}, {0.85, 1.5}, {0.97, 1.33}, {1.10, 1.0}};
    if (r <= curve[0].r) return curve[0].f;
    for (size_t i = 1; i < sizeof(curve) / sizeof(curve[0]); ++i) {
        if (r <= curve[i].r) {
            double t = (r - curve[i - 1].r) / (curve[i].r - curve[i - 1].r);
            return curve[i - 1].f + t * (curve[i].f - curve[i - 1].f);
        }
    }
    return 1.0;
}

int streamHeightForFactor(int windowHeight, double factor) {
    if (factor < 1.6 || windowHeight <= 0) return 0;
    static const int common[] = {540, 720, 900, 1080, 1200, 1440, 1600};
    double target = windowHeight / factor;
    int best = 0;
    double bestErr = 1e9;
    for (int h : common) {
        if (h * 1.4 > windowHeight) continue;
        double err = std::fabs(std::log(double(h) / target));
        if (err < bestErr) {
            bestErr = err;
            best = h;
        }
    }
    return bestErr < 0.2 ? best : 0;
}

static std::vector<double> gaussKernel(double sigma) {
    std::vector<double> k(13);
    double s = 0;
    for (int i = -6; i <= 6; ++i) s += (k[i + 6] = std::exp(-0.5 * i * i / (sigma * sigma)));
    for (double& v : k) v /= s;
    return k;
}

static std::vector<double> blur(const std::vector<float>& img, int w, int h, double sigma) {
    auto k = gaussKernel(sigma);
    std::vector<double> tmp(img.size()), out(img.size());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            double a = 0;
            for (int i = -6; i <= 6; ++i) a += k[i + 6] * img[size_t(y) * w + std::clamp(x + i, 0, w - 1)];
            tmp[size_t(y) * w + x] = a;
        }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            double a = 0;
            for (int i = -6; i <= 6; ++i) a += k[i + 6] * tmp[size_t(std::clamp(y + i, 0, h - 1)) * w + x];
            out[size_t(y) * w + x] = a;
        }
    return out;
}

ContentResMeasurement measureContentResolutionCpu(const std::vector<float>& luma, int w, int h) {
    auto g06 = blur(luma, w, h, 0.6), g12 = blur(luma, w, h, 1.2), g17 = blur(luma, w, h, 1.7);
    ContentResMeasurement m;
    for (size_t i = 0; i < luma.size(); ++i) {
        m.topBand += std::fabs(luma[i] - g06[i]);
        m.refBand += std::fabs(g12[i] - g17[i]);
    }
    m.topBand /= double(luma.size());
    m.refBand /= double(luma.size());
    return m;
}

int ContentResTracker::push(int detected) {
    if (detected == accepted_) {
        pending_ = -1;
        count_ = 0;
        return accepted_;
    }
    if (detected != pending_) {
        pending_ = detected;
        count_ = 1;
    } else {
        ++count_;
    }
    if (count_ >= stable_) {
        accepted_ = pending_;
        pending_ = -1;
        count_ = 0;
    }
    return accepted_;
}

} // namespace bgn
