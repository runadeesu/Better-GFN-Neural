#include "neural/NsrReference.h"

#include <algorithm>

namespace bgn {

#define NSR_ARR(...) std::vector<float>{__VA_ARGS__}
#define NSR_LAYER(ci, co, W, B) m.layers.push_back(NsrLayer{ci, co, W, B});

const NsrModelData& nsrModelS() {
    static const NsrModelData model = [] {
        NsrModelData m;
        m.name = "nsr_s_x2";
#include "neural/generated/nsr_s_x2.inc"
        return m;
    }();
    return model;
}

const NsrModelData& nsrModelL() {
    static const NsrModelData model = [] {
        NsrModelData m;
        m.name = "nsr_l_x2";
#include "neural/generated/nsr_l_x2.inc"
        return m;
    }();
    return model;
}

#undef NSR_LAYER
#undef NSR_ARR

namespace {
inline float leaky(float x) { return x > 0 ? x : 0.1f * x; }
constexpr float kCrEven[4] = {-0.0234375f, 0.2265625f, 0.8671875f, -0.0703125f};
constexpr float kCrOdd[4] = {-0.0703125f, 0.8671875f, 0.2265625f, -0.0234375f};
} // namespace

void nsrUpscaleReference(const NsrModelData& model, const std::vector<float>& rgb, int w, int h, std::vector<float>& out) {
    auto at = [&](int x, int y, int c) {
        x = std::clamp(x, 0, w - 1);
        y = std::clamp(y, 0, h - 1);
        return rgb[(size_t(y) * w + x) * 3 + c];
    };
    // Layer input: luma
    std::vector<float> act(size_t(w) * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) act[size_t(y) * w + x] = 0.25f * at(x, y, 0) + 0.5f * at(x, y, 1) + 0.25f * at(x, y, 2);
    int cin = 1;
    for (size_t li = 0; li < model.layers.size(); ++li) {
        const NsrLayer& L = model.layers[li];
        std::vector<float> next(size_t(L.cout) * w * h);
        for (int o = 0; o < L.cout; ++o)
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    float acc = L.b[o];
                    for (int i = 0; i < cin; ++i)
                        for (int ky = 0; ky < 3; ++ky)
                            for (int kx = 0; kx < 3; ++kx) {
                                int sx = std::clamp(x + kx - 1, 0, w - 1), sy = std::clamp(y + ky - 1, 0, h - 1);
                                acc += L.w[((size_t(o) * cin + i) * 3 + ky) * 3 + kx] * act[(size_t(i) * h + sy) * w + sx];
                            }
                    next[(size_t(o) * h + y) * w + x] = li + 1 < model.layers.size() ? leaky(acc) : acc;
                }
        act.swap(next);
        cin = L.cout;
    }
    // act now holds 4 residual planes
    out.assign(size_t(w) * h * 4 * 3, 0.0f);
    const int W2 = w * 2;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx) {
                    const float* wy = dy ? kCrOdd : kCrEven;
                    const float* wx = dx ? kCrOdd : kCrEven;
                    int by = dy ? y - 1 : y - 2, bx = dx ? x - 1 : x - 2;
                    float r = act[(size_t(dy * 2 + dx) * h + y) * w + x];
                    for (int c = 0; c < 3; ++c) {
                        float s = 0;
                        for (int j = 0; j < 4; ++j) {
                            float row = 0;
                            for (int i = 0; i < 4; ++i) row += wx[i] * at(bx + i, by + j, c);
                            s += wy[j] * row;
                        }
                        out[(size_t(2 * y + dy) * W2 + (2 * x + dx)) * 3 + c] = s + r;
                    }
                }
}

} // namespace bgn
