// Better GFN Neural - NSR (Neural Super Resolution) trainer
//
// Trains the tiny real-time x2 luma super-resolution CNNs that ship with
// Better GFN Neural, and exports them as:
//   * models/<name>.json                 (portable weights + metadata)
//   * shaders/generated/<name>.hlsli     (HLSL static weight tables)
//   * src/neural/generated/<name>.inc    (C++ weights for the CPU reference)
//
// The network operates on the YCoCg luma channel (Y = R/4 + G/2 + B/4) of
// gamma-encoded frames and predicts a residual on top of a Catmull-Rom x2
// upsample. Training pairs are generated from clean high-resolution images
// with a "cloud stream" degradation model: anti-aliased or aliased 2x
// downsampling, optional blur, JPEG-style 8x8 DCT block compression with 4:2:0
// chroma (a proxy for H.264/HEVC/AV1 macroblock artifacts) and mild noise.
//
// The trainer is dependency free (stb only) and runs on any C++17 compiler.
//
// Usage:
//   nsr_trainer --data <dir-with-png> --font <ttf> [--font <ttf> ...]
//               --name nsr_s_x2 --channels 8 --hidden 2 --iters 40000
//               --out-root <repo-root> [--threads N] [--seed S]

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"
#include "stb_truetype.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Basic image containers
// ---------------------------------------------------------------------------
struct ImageRGB {
    int w = 0, h = 0;
    std::vector<float> px; // interleaved RGB, gamma encoded, 0..1
    float* at(int x, int y) { return &px[(size_t(y) * w + x) * 3]; }
    const float* at(int x, int y) const { return &px[(size_t(y) * w + x) * 3]; }
    void alloc(int W, int H) { w = W; h = H; px.assign(size_t(W) * H * 3, 0.0f); }
};

struct Plane {
    int w = 0, h = 0;
    std::vector<float> v;
    void alloc(int W, int H) { w = W; h = H; v.assign(size_t(W) * H, 0.0f); }
    float& operator()(int x, int y) { return v[size_t(y) * w + x]; }
    float operator()(int x, int y) const { return v[size_t(y) * w + x]; }
    float clampAt(int x, int y) const {
        x = std::clamp(x, 0, w - 1);
        y = std::clamp(y, 0, h - 1);
        return v[size_t(y) * w + x];
    }
};

static inline float lumaYCoCg(const float* rgb) { return 0.25f * rgb[0] + 0.5f * rgb[1] + 0.25f * rgb[2]; }

static Plane toLuma(const ImageRGB& img) {
    Plane p;
    p.alloc(img.w, img.h);
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x) p(x, y) = lumaYCoCg(img.at(x, y));
    return p;
}

static bool loadPng(const std::string& path, ImageRGB& out) {
    int w, h, n;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &n, 3);
    if (!data) return false;
    out.alloc(w, h);
    for (size_t i = 0; i < size_t(w) * h * 3; ++i) out.px[i] = data[i] / 255.0f;
    stbi_image_free(data);
    return true;
}

// ---------------------------------------------------------------------------
// Catmull-Rom x2 upsampling (must match shaders/nsr_common.hlsli exactly)
// Even HR pixel 2i : taps i-2..i+1 weights (-0.0234375, 0.2265625, 0.8671875, -0.0703125)
// Odd  HR pixel 2i+1: taps i-1..i+2 weights (-0.0703125, 0.8671875, 0.2265625, -0.0234375)
// ---------------------------------------------------------------------------
static const float kCrEven[4] = {-0.0234375f, 0.2265625f, 0.8671875f, -0.0703125f};
static const float kCrOdd[4] = {-0.0703125f, 0.8671875f, 0.2265625f, -0.0234375f};

static Plane catmullRomX2(const Plane& lr) {
    Plane tmp; // horizontal pass
    tmp.alloc(lr.w * 2, lr.h);
    for (int y = 0; y < lr.h; ++y)
        for (int xh = 0; xh < lr.w * 2; ++xh) {
            int i = xh >> 1;
            const float* wts = (xh & 1) ? kCrOdd : kCrEven;
            int base = (xh & 1) ? i - 1 : i - 2;
            float s = 0;
            for (int k = 0; k < 4; ++k) s += wts[k] * lr.clampAt(base + k, y);
            tmp(xh, y) = s;
        }
    Plane out;
    out.alloc(lr.w * 2, lr.h * 2);
    for (int yh = 0; yh < lr.h * 2; ++yh) {
        int i = yh >> 1;
        const float* wts = (yh & 1) ? kCrOdd : kCrEven;
        int base = (yh & 1) ? i - 1 : i - 2;
        for (int x = 0; x < tmp.w; ++x) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += wts[k] * tmp.clampAt(x, base + k);
            out(x, yh) = s;
        }
    }
    return out;
}

static Plane bilinearX2(const Plane& lr) {
    Plane out;
    out.alloc(lr.w * 2, lr.h * 2);
    for (int yh = 0; yh < out.h; ++yh)
        for (int xh = 0; xh < out.w; ++xh) {
            float u = (xh + 0.5f) * 0.5f - 0.5f, v = (yh + 0.5f) * 0.5f - 0.5f;
            int x0 = int(std::floor(u)), y0 = int(std::floor(v));
            float fx = u - x0, fy = v - y0;
            float a = lr.clampAt(x0, y0), b = lr.clampAt(x0 + 1, y0), c = lr.clampAt(x0, y0 + 1), d = lr.clampAt(x0 + 1, y0 + 1);
            out(xh, yh) = (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
        }
    return out;
}

// ---------------------------------------------------------------------------
// Degradation model
// ---------------------------------------------------------------------------
enum class DownKernel { Box, Mitchell, CatmullRom, Lanczos2, Gaussian, Point };

static float kernelWeight(DownKernel k, float x) {
    x = std::fabs(x);
    switch (k) {
    case DownKernel::Box: return x < 0.5f ? 1.0f : 0.0f;
    case DownKernel::Mitchell: {
        const float B = 1.0f / 3.0f, C = 1.0f / 3.0f;
        if (x < 1) return ((12 - 9 * B - 6 * C) * x * x * x + (-18 + 12 * B + 6 * C) * x * x + (6 - 2 * B)) / 6;
        if (x < 2) return ((-B - 6 * C) * x * x * x + (6 * B + 30 * C) * x * x + (-12 * B - 48 * C) * x + (8 * B + 24 * C)) / 6;
        return 0;
    }
    case DownKernel::CatmullRom: {
        if (x < 1) return 1.5f * x * x * x - 2.5f * x * x + 1;
        if (x < 2) return -0.5f * x * x * x + 2.5f * x * x - 4 * x + 2;
        return 0;
    }
    case DownKernel::Lanczos2: {
        if (x < 1e-6f) return 1;
        if (x >= 2) return 0;
        const float pi = 3.14159265358979f;
        return 2 * std::sin(pi * x) * std::sin(pi * x / 2) / (pi * pi * x * x);
    }
    case DownKernel::Gaussian: return std::exp(-x * x / (2 * 0.45f * 0.45f));
    case DownKernel::Point: return 0;
    }
    return 0;
}

// 2x downsample of an RGB image with the given kernel (support scaled by 2).
static ImageRGB downsample2(const ImageRGB& hr, DownKernel k, std::mt19937& rng) {
    ImageRGB lr;
    lr.alloc(hr.w / 2, hr.h / 2);
    if (k == DownKernel::Point) {
        int ox = rng() & 1, oy = rng() & 1;
        for (int y = 0; y < lr.h; ++y)
            for (int x = 0; x < lr.w; ++x) {
                const float* s = hr.at(std::min(2 * x + ox, hr.w - 1), std::min(2 * y + oy, hr.h - 1));
                float* d = lr.at(x, y);
                d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
            }
        return lr;
    }
    const int R = (k == DownKernel::Box) ? 1 : 4;
    // Separable: horizontal then vertical
    std::vector<float> tmp(size_t(lr.w) * hr.h * 3);
    for (int y = 0; y < hr.h; ++y)
        for (int x = 0; x < lr.w; ++x) {
            float c = 2 * x + 0.5f; // center in HR pixel coordinates
            float acc[3] = {0, 0, 0}, ws = 0;
            for (int xi = int(std::floor(c)) - R; xi <= int(std::floor(c)) + R + 1; ++xi) {
                float wgt = kernelWeight(k, (xi - c) * 0.5f);
                if (wgt == 0) continue;
                const float* s = hr.at(std::clamp(xi, 0, hr.w - 1), y);
                for (int ch = 0; ch < 3; ++ch) acc[ch] += wgt * s[ch];
                ws += wgt;
            }
            for (int ch = 0; ch < 3; ++ch) tmp[(size_t(y) * lr.w + x) * 3 + ch] = acc[ch] / ws;
        }
    for (int y = 0; y < lr.h; ++y)
        for (int x = 0; x < lr.w; ++x) {
            float c = 2 * y + 0.5f;
            float acc[3] = {0, 0, 0}, ws = 0;
            for (int yi = int(std::floor(c)) - R; yi <= int(std::floor(c)) + R + 1; ++yi) {
                float wgt = kernelWeight(k, (yi - c) * 0.5f);
                if (wgt == 0) continue;
                const float* s = &tmp[(size_t(std::clamp(yi, 0, hr.h - 1)) * lr.w + x) * 3];
                for (int ch = 0; ch < 3; ++ch) acc[ch] += wgt * s[ch];
                ws += wgt;
            }
            float* d = lr.at(x, y);
            for (int ch = 0; ch < 3; ++ch) d[ch] = std::clamp(acc[ch] / ws, 0.0f, 1.0f);
        }
    return lr;
}

static void gaussianBlurRGB(ImageRGB& img, float sigma) {
    if (sigma < 0.05f) return;
    int r = int(std::ceil(sigma * 3));
    std::vector<float> k(2 * r + 1);
    float s = 0;
    for (int i = -r; i <= r; ++i) s += (k[i + r] = std::exp(-i * i / (2 * sigma * sigma)));
    for (auto& v : k) v /= s;
    ImageRGB tmp = img;
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x)
            for (int ch = 0; ch < 3; ++ch) {
                float a = 0;
                for (int i = -r; i <= r; ++i) a += k[i + r] * img.at(std::clamp(x + i, 0, img.w - 1), y)[ch];
                tmp.at(x, y)[ch] = a;
            }
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x)
            for (int ch = 0; ch < 3; ++ch) {
                float a = 0;
                for (int i = -r; i <= r; ++i) a += k[i + r] * tmp.at(x, std::clamp(y + i, 0, img.h - 1))[ch];
                img.at(x, y)[ch] = a;
            }
}

static void jpegWriteFunc(void* ctx, void* data, int size) {
    auto* v = static_cast<std::vector<unsigned char>*>(ctx);
    v->insert(v->end(), (unsigned char*)data, (unsigned char*)data + size);
}

static void jpegRoundTrip(ImageRGB& img, int quality) {
    std::vector<unsigned char> rgb8(size_t(img.w) * img.h * 3);
    for (size_t i = 0; i < rgb8.size(); ++i) rgb8[i] = (unsigned char)std::clamp(int(img.px[i] * 255.0f + 0.5f), 0, 255);
    std::vector<unsigned char> enc;
    stbi_write_jpg_to_func(jpegWriteFunc, &enc, img.w, img.h, 3, rgb8.data(), quality);
    int w, h, n;
    unsigned char* dec = stbi_load_from_memory(enc.data(), int(enc.size()), &w, &h, &n, 3);
    if (!dec) return;
    for (size_t i = 0; i < rgb8.size(); ++i) img.px[i] = dec[i] / 255.0f;
    stbi_image_free(dec);
}

static void quantize8(ImageRGB& img) {
    for (auto& v : img.px) v = std::round(std::clamp(v, 0.0f, 1.0f) * 255.0f) / 255.0f;
}

// ---------------------------------------------------------------------------
// Synthetic "game-like" content: HUD text, thin lines/fences, grids, foliage
// strokes, gradients. Rendered supersampled for clean ground truth.
// ---------------------------------------------------------------------------
struct FontSet {
    std::vector<std::vector<unsigned char>> blobs;
    std::vector<stbtt_fontinfo> infos;
    bool add(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        std::vector<unsigned char> d((std::istreambuf_iterator<char>(f)), {});
        blobs.push_back(std::move(d));
        stbtt_fontinfo fi;
        if (!stbtt_InitFont(&fi, blobs.back().data(), stbtt_GetFontOffsetForIndex(blobs.back().data(), 0))) {
            blobs.pop_back();
            return false;
        }
        infos.push_back(fi);
        // Re-point all infos since vector growth may move blobs' buffers? (vector<vector> moves keep data pointers)
        return true;
    }
};

static void blendPixel(ImageRGB& img, int x, int y, const float c[3], float a) {
    if (x < 0 || y < 0 || x >= img.w || y >= img.h || a <= 0) return;
    float* p = img.at(x, y);
    a = std::min(a, 1.0f);
    for (int ch = 0; ch < 3; ++ch) p[ch] = p[ch] * (1 - a) + c[ch] * a;
}

static void drawText(ImageRGB& img, FontSet& fonts, std::mt19937& rng, int x, int y, float px, const float col[3], const std::u32string& text, float alpha) {
    if (fonts.infos.empty()) return;
    stbtt_fontinfo& fi = fonts.infos[rng() % fonts.infos.size()];
    float scale = stbtt_ScaleForPixelHeight(&fi, px);
    int ascent, descent, gap;
    stbtt_GetFontVMetrics(&fi, &ascent, &descent, &gap);
    int baseline = y + int(ascent * scale);
    float xpos = float(x);
    for (char32_t cp : text) {
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&fi, int(cp), &adv, &lsb);
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBoxSubpixel(&fi, int(cp), scale, scale, xpos - std::floor(xpos), 0, &x0, &y0, &x1, &y1);
        int bw = x1 - x0, bh = y1 - y0;
        if (bw > 0 && bh > 0) {
            std::vector<unsigned char> bmp(size_t(bw) * bh);
            stbtt_MakeCodepointBitmapSubpixel(&fi, bmp.data(), bw, bh, bw, scale, scale, xpos - std::floor(xpos), 0, int(cp));
            for (int j = 0; j < bh; ++j)
                for (int i = 0; i < bw; ++i)
                    blendPixel(img, int(std::floor(xpos)) + x0 + i, baseline + y0 + j, col, alpha * bmp[size_t(j) * bw + i] / 255.0f);
        }
        xpos += adv * scale;
        if (xpos > img.w) break;
    }
}

static float urand(std::mt19937& rng, float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); }

// Anti-aliased capsule (thick line) via 4x4 supersampled coverage.
static void drawLine(ImageRGB& img, float x0, float y0, float x1, float y1, float width, const float col[3], float alpha) {
    int minx = int(std::floor(std::min(x0, x1) - width - 1)), maxx = int(std::ceil(std::max(x0, x1) + width + 1));
    int miny = int(std::floor(std::min(y0, y1) - width - 1)), maxy = int(std::ceil(std::max(y0, y1) + width + 1));
    minx = std::max(minx, 0); miny = std::max(miny, 0);
    maxx = std::min(maxx, img.w - 1); maxy = std::min(maxy, img.h - 1);
    float dx = x1 - x0, dy = y1 - y0, len2 = std::max(dx * dx + dy * dy, 1e-6f), hw = width * 0.5f;
    for (int y = miny; y <= maxy; ++y)
        for (int x = minx; x <= maxx; ++x) {
            int hits = 0;
            for (int sy = 0; sy < 4; ++sy)
                for (int sx = 0; sx < 4; ++sx) {
                    float px = x + (sx + 0.5f) / 4, py = y + (sy + 0.5f) / 4;
                    float t = std::clamp(((px - x0) * dx + (py - y0) * dy) / len2, 0.0f, 1.0f);
                    float ex = x0 + t * dx - px, ey = y0 + t * dy - py;
                    if (ex * ex + ey * ey <= hw * hw) ++hits;
                }
            if (hits) blendPixel(img, x, y, col, alpha * hits / 16.0f);
        }
}

static void randomColor(std::mt19937& rng, float c[3]) {
    for (int i = 0; i < 3; ++i) c[i] = urand(rng, 0, 1);
}

static ImageRGB makeSynthetic(FontSet& fonts, std::mt19937& rng, int W, int H) {
    ImageRGB img;
    img.alloc(W, H);
    // Background gradient (with optional dark scene)
    float c0[3], c1[3];
    randomColor(rng, c0);
    randomColor(rng, c1);
    float dark = (rng() % 3 == 0) ? urand(rng, 0.05f, 0.3f) : 1.0f;
    float ang = urand(rng, 0, 6.2831f), ca = std::cos(ang), sa = std::sin(ang);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            float t = std::clamp(0.5f + ((x - W / 2) * ca + (y - H / 2) * sa) / float(W + H), 0.0f, 1.0f);
            float* p = img.at(x, y);
            for (int ch = 0; ch < 3; ++ch) p[ch] = dark * (c0[ch] * (1 - t) + c1[ch] * t);
        }
    // Foliage-like strokes
    int strokes = rng() % 2 ? int(urand(rng, 200, 900)) : 0;
    float baseG[3] = {urand(rng, 0.1f, 0.4f), urand(rng, 0.3f, 0.7f), urand(rng, 0.05f, 0.3f)};
    for (int i = 0; i < strokes; ++i) {
        float x = urand(rng, 0, float(W)), y = urand(rng, float(H) * 0.3f, float(H));
        float l = urand(rng, 3, 14), a = urand(rng, -1.9f, -1.2f);
        float c[3] = {baseG[0] * urand(rng, 0.6f, 1.4f), baseG[1] * urand(rng, 0.6f, 1.4f), baseG[2] * urand(rng, 0.6f, 1.4f)};
        drawLine(img, x, y, x + l * std::cos(a), y + l * std::sin(a), urand(rng, 0.6f, 1.6f), c, 1.0f);
    }
    // Shapes
    int shapes = int(urand(rng, 3, 14));
    for (int i = 0; i < shapes; ++i) {
        float c[3];
        randomColor(rng, c);
        float cx = urand(rng, 0, float(W)), cy = urand(rng, 0, float(H)), r = urand(rng, 6, 80);
        if (rng() % 2) {
            for (int y = std::max(0, int(cy - r)); y < std::min(H, int(cy + r)); ++y)
                for (int x = std::max(0, int(cx - r)); x < std::min(W, int(cx + r)); ++x) {
                    int hits = 0;
                    for (int s = 0; s < 16; ++s) {
                        float px = x + ((s & 3) + 0.5f) / 4 - cx, py = y + ((s >> 2) + 0.5f) / 4 - cy;
                        if (px * px + py * py <= r * r) ++hits;
                    }
                    blendPixel(img, x, y, c, hits / 16.0f);
                }
        } else {
            float rw = urand(rng, 4, 120), rh = urand(rng, 4, 80);
            for (int y = std::max(0, int(cy)); y < std::min(H, int(cy + rh)); ++y)
                for (int x = std::max(0, int(cx)); x < std::min(W, int(cx + rw)); ++x) blendPixel(img, x, y, c, 1.0f);
        }
    }
    // Fences / wires / grids
    int fences = int(urand(rng, 1, 5));
    for (int f = 0; f < fences; ++f) {
        float c[3];
        randomColor(rng, c);
        float a = urand(rng, 0, 3.14159f), spacing = urand(rng, 3, 18), width = urand(rng, 0.5f, 2.5f);
        float ox = urand(rng, 0, float(W)), oy = urand(rng, 0, float(H)), extent = urand(rng, 40, 260);
        int count = int(urand(rng, 3, 25));
        float dx = std::cos(a), dy = std::sin(a);
        for (int i = 0; i < count; ++i) {
            float px = ox + -dy * spacing * i, py = oy + dx * spacing * i;
            drawLine(img, px, py, px + dx * extent, py + dy * extent, width, c, 1.0f);
        }
        if (rng() % 2) { // cross-hatch => chain-link fence
            for (int i = 0; i < count; ++i) {
                float px = ox + dx * spacing * i, py = oy + dy * spacing * i;
                drawLine(img, px, py, px - dy * extent, py + dx * extent, width, c, 1.0f);
            }
        }
    }
    // HUD / UI text
    static const char32_t* samples[] = {U"HP 100/100", U"AMMO 30 | 120", U"Press A to continue", U"Objective: Reach the tower", U"FPS 60  PING 18ms",
                                        U"Inventory", U"Settings > Graphics > Quality", U"LAP 2/3  01:24.517", U"SCORE 004250", U"Level 37",
                                        U"Quest updated", U"QUICK MATCH", U"ミッション開始", U"装備を確認してください", U"体力 87%", U"Ready? 3...2...1",
                                        U"The quick brown fox jumps over the lazy dog", U"0123456789 +-*/%", U"Map  Skills  Crafting", U"WASD Move  SPACE Jump"};
    int texts = int(urand(rng, 4, 16));
    for (int i = 0; i < texts; ++i) {
        float c[3];
        randomColor(rng, c);
        float size = urand(rng, 8, 44);
        int x = int(urand(rng, -20, float(W) * 0.8f)), y = int(urand(rng, 0, float(H) - size));
        const char32_t* s = samples[rng() % (sizeof(samples) / sizeof(samples[0]))];
        if (rng() % 3 == 0) { // drop shadow / outline like game HUDs
            float k[3] = {0, 0, 0};
            drawText(img, fonts, rng, x + 1, y + 1, size, k, s, 0.8f);
        }
        drawText(img, fonts, rng, x, y, size, c, s, 1.0f);
    }
    return img;
}

// ---------------------------------------------------------------------------
// Network
// ---------------------------------------------------------------------------
static const float kLeak = 0.1f;

struct ConvLayer {
    int cin = 0, cout = 0;
    std::vector<float> w, b;     // w[o][i][ky][kx]
    std::vector<float> mw, vw, mb, vb;
    void init(int ci, int co, std::mt19937& rng, float gainScale) {
        cin = ci; cout = co;
        w.resize(size_t(co) * ci * 9);
        b.assign(co, 0.0f);
        float stdv = std::sqrt(2.0f / (ci * 9.0f)) * gainScale;
        std::normal_distribution<float> nd(0, stdv);
        for (auto& x : w) x = nd(rng);
        mw.assign(w.size(), 0); vw.assign(w.size(), 0);
        mb.assign(co, 0); vb.assign(co, 0);
    }
};

struct Model {
    std::string name;
    int channels = 8, hidden = 2;
    std::vector<ConvLayer> layers; // first: 1->C, hidden: C->C, last: C->4
    void init(int C, int D, std::mt19937& rng) {
        channels = C; hidden = D;
        layers.clear();
        ConvLayer l;
        l.init(1, C, rng, 1.0f);
        layers.push_back(l);
        for (int i = 0; i < D; ++i) { l.init(C, C, rng, 1.0f); layers.push_back(l); }
        l.init(C, 4, rng, 0.1f);
        layers.push_back(l);
    }
    size_t paramCount() const {
        size_t n = 0;
        for (auto& l : layers) n += l.w.size() + l.b.size();
        return n;
    }
};

struct Grads {
    std::vector<std::vector<float>> dw, db;
    void init(const Model& m) {
        dw.resize(m.layers.size());
        db.resize(m.layers.size());
        for (size_t i = 0; i < m.layers.size(); ++i) {
            dw[i].assign(m.layers[i].w.size(), 0);
            db[i].assign(m.layers[i].b.size(), 0);
        }
    }
    void zero() {
        for (auto& v : dw) std::fill(v.begin(), v.end(), 0.0f);
        for (auto& v : db) std::fill(v.begin(), v.end(), 0.0f);
    }
};

// Tensor: C x H x W, contiguous
struct Tensor {
    int c = 0, h = 0, w = 0;
    std::vector<float> d;
    void alloc(int C, int H, int W) { c = C; h = H; w = W; d.assign(size_t(C) * H * W, 0.0f); }
    float* ch(int i) { return &d[size_t(i) * h * w]; }
    const float* ch(int i) const { return &d[size_t(i) * h * w]; }
};

// Replicate-pad by one pixel: C x (H+2) x (W+2)
static void padReplicate(const Tensor& in, Tensor& out) {
    out.alloc(in.c, in.h + 2, in.w + 2);
    for (int c = 0; c < in.c; ++c) {
        const float* s = in.ch(c);
        float* d = out.ch(c);
        for (int y = 0; y < in.h + 2; ++y) {
            int sy = std::clamp(y - 1, 0, in.h - 1);
            for (int x = 0; x < in.w + 2; ++x) d[size_t(y) * (in.w + 2) + x] = s[size_t(sy) * in.w + std::clamp(x - 1, 0, in.w - 1)];
        }
    }
}

static void convForward(const ConvLayer& L, const Tensor& padded, Tensor& out) {
    const int H = padded.h - 2, W = padded.w - 2, PW = padded.w;
    out.alloc(L.cout, H, W);
    for (int o = 0; o < L.cout; ++o) {
        float* od = out.ch(o);
        std::fill(od, od + size_t(H) * W, L.b[o]);
        for (int i = 0; i < L.cin; ++i) {
            const float* pd = padded.ch(i);
            for (int ky = 0; ky < 3; ++ky)
                for (int kx = 0; kx < 3; ++kx) {
                    const float wv = L.w[((size_t(o) * L.cin + i) * 3 + ky) * 3 + kx];
                    for (int y = 0; y < H; ++y) {
                        const float* srow = pd + size_t(y + ky) * PW + kx;
                        float* orow = od + size_t(y) * W;
                        for (int x = 0; x < W; ++x) orow[x] += wv * srow[x];
                    }
                }
        }
    }
}

// dOut -> dW, dB, dPadded
static void convBackward(const ConvLayer& L, const Tensor& padded, const Tensor& dOut, std::vector<float>& dw, std::vector<float>& db, Tensor* dPadded) {
    const int H = dOut.h, W = dOut.w, PW = padded.w;
    if (dPadded) dPadded->alloc(L.cin, padded.h, padded.w);
    for (int o = 0; o < L.cout; ++o) {
        const float* g = dOut.ch(o);
        double bsum = 0;
        for (size_t k = 0; k < size_t(H) * W; ++k) bsum += g[k];
        db[o] += float(bsum);
        for (int i = 0; i < L.cin; ++i) {
            const float* pd = padded.ch(i);
            float* dpd = dPadded ? dPadded->ch(i) : nullptr;
            for (int ky = 0; ky < 3; ++ky)
                for (int kx = 0; kx < 3; ++kx) {
                    const size_t widx = ((size_t(o) * L.cin + i) * 3 + ky) * 3 + kx;
                    const float wv = L.w[widx];
                    float acc = 0;
                    for (int y = 0; y < H; ++y) {
                        const float* srow = pd + size_t(y + ky) * PW + kx;
                        const float* grow = g + size_t(y) * W;
                        float rowAcc = 0;
                        for (int x = 0; x < W; ++x) rowAcc += grow[x] * srow[x];
                        acc += rowAcc;
                        if (dpd) {
                            float* drow = dpd + size_t(y + ky) * PW + kx;
                            for (int x = 0; x < W; ++x) drow[x] += wv * grow[x];
                        }
                    }
                    dw[widx] += acc;
                }
        }
    }
}

// Fold padded gradient back onto the unpadded tensor (replicate padding adjoint)
static void foldPadGrad(const Tensor& dPadded, Tensor& dIn) {
    const int H = dPadded.h - 2, W = dPadded.w - 2;
    dIn.alloc(dPadded.c, H, W);
    for (int c = 0; c < dPadded.c; ++c) {
        const float* s = dPadded.ch(c);
        float* d = dIn.ch(c);
        for (int y = 0; y < H + 2; ++y) {
            int ty = std::clamp(y - 1, 0, H - 1);
            for (int x = 0; x < W + 2; ++x) d[size_t(ty) * W + std::clamp(x - 1, 0, W - 1)] += s[size_t(y) * (W + 2) + x];
        }
    }
}

static inline float leaky(float x) { return x > 0 ? x : kLeak * x; }

struct ForwardCache {
    std::vector<Tensor> padded; // input of each layer (padded)
    std::vector<Tensor> pre;    // pre-activation output of each layer
};

// Runs the network on an LR luma plane; returns residual tensor (4 x H x W)
static Tensor runModel(const Model& m, const Plane& lrY, ForwardCache* cache) {
    Tensor x;
    x.alloc(1, lrY.h, lrY.w);
    std::copy(lrY.v.begin(), lrY.v.end(), x.d.begin());
    ForwardCache local;
    ForwardCache& c = cache ? *cache : local;
    c.padded.resize(m.layers.size());
    c.pre.resize(m.layers.size());
    for (size_t li = 0; li < m.layers.size(); ++li) {
        padReplicate(x, c.padded[li]);
        convForward(m.layers[li], c.padded[li], c.pre[li]);
        if (li + 1 < m.layers.size()) {
            x = c.pre[li];
            for (auto& v : x.d) v = leaky(v);
        } else {
            x = c.pre[li];
        }
    }
    return x;
}

static Plane assembleHR(const Plane& base, const Tensor& res) {
    Plane out = base;
    for (int y = 0; y < res.h; ++y)
        for (int x = 0; x < res.w; ++x)
            for (int k = 0; k < 4; ++k) out(2 * x + (k & 1), 2 * y + (k >> 1)) += res.ch(k)[size_t(y) * res.w + x];
    return out;
}

// One training sample: forward + backward, returns loss sum
static double trainSample(const Model& m, const Plane& lrY, const Plane& hrY, int border, Grads& g) {
    ForwardCache cache;
    Tensor res = runModel(m, lrY, &cache);
    Plane base = catmullRomX2(lrY);
    Tensor dRes;
    dRes.alloc(4, res.h, res.w);
    double loss = 0;
    const float eps = 1e-3f;
    const int Hh = hrY.h, Wh = hrY.w;
    const float norm = 1.0f / float((Hh - 2 * border) * (Wh - 2 * border));
    for (int yh = border; yh < Hh - border; ++yh)
        for (int xh = border; xh < Wh - border; ++xh) {
            int x = xh >> 1, y = yh >> 1, k = (yh & 1) * 2 + (xh & 1);
            float pred = base(xh, yh) + res.ch(k)[size_t(y) * res.w + x];
            float d = pred - hrY(xh, yh);
            float s = std::sqrt(d * d + eps * eps);
            loss += s;
            dRes.ch(k)[size_t(y) * res.w + x] = d / s * norm;
        }
    Tensor dOut = dRes;
    for (int li = int(m.layers.size()) - 1; li >= 0; --li) {
        if (li + 1 < int(m.layers.size())) {
            // activation backward
            const Tensor& pre = cache.pre[li];
            for (size_t k = 0; k < dOut.d.size(); ++k)
                if (pre.d[k] <= 0) dOut.d[k] *= kLeak;
        }
        Tensor dPad;
        convBackward(m.layers[li], cache.padded[li], dOut, g.dw[li], g.db[li], li > 0 ? &dPad : nullptr);
        if (li > 0) {
            Tensor dIn;
            foldPadGrad(dPad, dIn);
            dOut = std::move(dIn);
        }
    }
    return loss * norm;
}

static void adamStep(Model& m, const Grads& g, float lr, int t, float scale) {
    const float b1 = 0.9f, b2 = 0.999f, e = 1e-8f;
    const float c1 = 1 - std::pow(b1, float(t)), c2 = 1 - std::pow(b2, float(t));
    for (size_t li = 0; li < m.layers.size(); ++li) {
        auto& L = m.layers[li];
        for (size_t k = 0; k < L.w.size(); ++k) {
            float gr = g.dw[li][k] * scale;
            L.mw[k] = b1 * L.mw[k] + (1 - b1) * gr;
            L.vw[k] = b2 * L.vw[k] + (1 - b2) * gr * gr;
            L.w[k] -= lr * (L.mw[k] / c1) / (std::sqrt(L.vw[k] / c2) + e);
        }
        for (size_t k = 0; k < L.b.size(); ++k) {
            float gr = g.db[li][k] * scale;
            L.mb[k] = b1 * L.mb[k] + (1 - b1) * gr;
            L.vb[k] = b2 * L.vb[k] + (1 - b2) * gr * gr;
            L.b[k] -= lr * (L.mb[k] / c1) / (std::sqrt(L.vb[k] / c2) + e);
        }
    }
}

// ---------------------------------------------------------------------------
// Sample generation
// ---------------------------------------------------------------------------
struct Dataset {
    std::vector<ImageRGB> train, val;
    std::vector<std::string> valNames;
};

static ImageRGB cropAug(const ImageRGB& src, int size, std::mt19937& rng) {
    ImageRGB out;
    out.alloc(size, size);
    int x0 = int(rng() % unsigned(src.w - size + 1)), y0 = int(rng() % unsigned(src.h - size + 1));
    bool fx = rng() & 1, fy = rng() & 1, tr = rng() & 1;
    float gain = urand(rng, 0.75f, 1.08f);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            int sx = tr ? y : x, sy = tr ? x : y;
            if (fx) sx = size - 1 - sx;
            if (fy) sy = size - 1 - sy;
            const float* s = src.at(x0 + sx, y0 + sy);
            float* d = out.at(x, y);
            for (int ch = 0; ch < 3; ++ch) d[ch] = std::clamp(s[ch] * gain, 0.0f, 1.0f);
        }
    return out;
}

struct DegradeParams {
    DownKernel kernel = DownKernel::Box;
    float blur = 0;
    int jpegQuality = 0; // 0 = none
    float noise = 0;
};

static DegradeParams randomDegrade(std::mt19937& rng) {
    DegradeParams p;
    static const DownKernel ks[] = {DownKernel::Box, DownKernel::Mitchell, DownKernel::CatmullRom, DownKernel::Lanczos2, DownKernel::Gaussian, DownKernel::Point};
    float r = urand(rng, 0, 1);
    p.kernel = r < 0.12f ? DownKernel::Point : ks[rng() % 5];
    if (urand(rng, 0, 1) < 0.25f) p.blur = urand(rng, 0.2f, 0.7f);
    if (urand(rng, 0, 1) < 0.75f) p.jpegQuality = int(urand(rng, 30, 95));
    if (urand(rng, 0, 1) < 0.15f) p.noise = urand(rng, 0.002f, 0.012f);
    return p;
}

static ImageRGB degrade(const ImageRGB& hr, const DegradeParams& p, std::mt19937& rng) {
    ImageRGB lr = downsample2(hr, p.kernel, rng);
    if (p.blur > 0) gaussianBlurRGB(lr, p.blur);
    if (p.noise > 0) {
        std::normal_distribution<float> nd(0, p.noise);
        for (auto& v : lr.px) v = std::clamp(v + nd(rng), 0.0f, 1.0f);
    }
    quantize8(lr);
    if (p.jpegQuality > 0) jpegRoundTrip(lr, p.jpegQuality);
    return lr;
}

static Plane cropPlane(const Plane& p, int x0, int y0, int w, int h) {
    Plane o;
    o.alloc(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) o(x, y) = p(x0 + x, y0 + y);
    return o;
}

// ---------------------------------------------------------------------------
// Metrics
// ---------------------------------------------------------------------------
static double psnr(const Plane& a, const Plane& b, int border) {
    double se = 0;
    size_t n = 0;
    for (int y = border; y < a.h - border; ++y)
        for (int x = border; x < a.w - border; ++x) {
            double d = std::clamp(a(x, y), 0.0f, 1.0f) - b(x, y);
            se += d * d;
            ++n;
        }
    double mse = se / std::max<size_t>(n, 1);
    return 10.0 * std::log10(1.0 / std::max(mse, 1e-12));
}

struct ValResult {
    double bilinear = 0, catmull = 0, net = 0;
};

static ValResult validate(const Model& m, const Dataset& ds, bool withJpeg) {
    ValResult r;
    int n = 0;
    for (size_t i = 0; i < ds.val.size(); ++i) {
        std::mt19937 rng(1234 + unsigned(i));
        DegradeParams p;
        p.kernel = DownKernel::Mitchell;
        p.jpegQuality = withJpeg ? 60 : 0;
        const ImageRGB& hr = ds.val[i];
        ImageRGB lr = degrade(hr, p, rng);
        Plane lrY = toLuma(lr), hrY = toLuma(hr);
        // hr may have odd size; crop to 2*lr
        hrY = cropPlane(hrY, 0, 0, lrY.w * 2, lrY.h * 2);
        Plane bl = bilinearX2(lrY), cr = catmullRomX2(lrY);
        Tensor res = runModel(m, lrY, nullptr);
        Plane net = assembleHR(cr, res);
        r.bilinear += psnr(bl, hrY, 8);
        r.catmull += psnr(cr, hrY, 8);
        r.net += psnr(net, hrY, 8);
        ++n;
    }
    if (n) { r.bilinear /= n; r.catmull /= n; r.net /= n; }
    return r;
}

// ---------------------------------------------------------------------------
// Export
// ---------------------------------------------------------------------------
static std::string fmt(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.8g", v);
    std::string s(buf);
    if (s.find('.') == std::string::npos && s.find('e') == std::string::npos) s += ".0";
    return s;
}

static void exportModel(const Model& m, const std::string& root, const ValResult& vClean, const ValResult& vJpeg, int iters, unsigned seed, double seconds) {
    const std::string upper = [&] { std::string u = m.name; for (auto& ch : u) ch = char(std::toupper((unsigned char)ch)); return u; }();
    fs::create_directories(fs::path(root) / "models");
    fs::create_directories(fs::path(root) / "shaders" / "generated");
    fs::create_directories(fs::path(root) / "src" / "neural" / "generated");
    // JSON
    {
        std::ofstream f(fs::path(root) / "models" / (m.name + ".json"));
        f << "{\n  \"format\": \"bgn-nsr\",\n  \"format_version\": 1,\n  \"name\": \"" << m.name << "\",\n";
        f << "  \"scale\": 2,\n  \"input\": \"YCoCg luma (Y = R/4 + G/2 + B/4), gamma-encoded, replicate padding\",\n";
        f << "  \"base\": \"catmull-rom x2\",\n  \"output\": \"4 sub-pixel residuals (k = dy*2 + dx)\",\n";
        f << "  \"activation\": \"leaky_relu(0.1)\",\n  \"channels\": " << m.channels << ",\n  \"hidden_layers\": " << m.hidden << ",\n";
        f << "  \"parameters\": " << m.paramCount() << ",\n  \"training\": { \"iterations\": " << iters << ", \"seed\": " << seed
          << ", \"seconds\": " << int(seconds) << " },\n";
        f << "  \"validation_psnr_y\": {\n";
        f << "    \"clean\": { \"bilinear\": " << vClean.bilinear << ", \"catmull_rom\": " << vClean.catmull << ", \"nsr\": " << vClean.net << " },\n";
        f << "    \"jpeg_q60\": { \"bilinear\": " << vJpeg.bilinear << ", \"catmull_rom\": " << vJpeg.catmull << ", \"nsr\": " << vJpeg.net << " }\n  },\n";
        f << "  \"layers\": [\n";
        for (size_t li = 0; li < m.layers.size(); ++li) {
            const auto& L = m.layers[li];
            f << "    { \"cin\": " << L.cin << ", \"cout\": " << L.cout << ", \"kernel\": 3,\n      \"weights\": [";
            for (size_t k = 0; k < L.w.size(); ++k) f << (k ? "," : "") << fmt(L.w[k]);
            f << "],\n      \"bias\": [";
            for (size_t k = 0; k < L.b.size(); ++k) f << (k ? "," : "") << fmt(L.b[k]);
            f << "] }" << (li + 1 < m.layers.size() ? "," : "") << "\n";
        }
        f << "  ]\n}\n";
    }
    // HLSL
    {
        std::ofstream f(fs::path(root) / "shaders" / "generated" / (m.name + ".hlsli"));
        f << "// AUTO-GENERATED by tools/nsr_trainer. Do not edit.\n// Model: " << m.name << "  params: " << m.paramCount() << "\n";
        f << "#define " << upper << "_CHANNELS " << m.channels << "\n#define " << upper << "_HIDDEN " << m.hidden << "\n";
        for (size_t li = 0; li < m.layers.size(); ++li) {
            const auto& L = m.layers[li];
            const int G = L.cout / 4;
            if (L.cin == 1) {
                f << "static const float4 " << upper << "_L" << li << "_W[" << G * 9 << "] = {\n";
                for (int gI = 0; gI < G; ++gI)
                    for (int t = 0; t < 9; ++t) {
                        f << "  float4(";
                        for (int j = 0; j < 4; ++j) f << (j ? ", " : "") << fmt(L.w[(size_t(4 * gI + j) * 1 + 0) * 9 + t]);
                        f << "),\n";
                    }
                f << "};\n";
            } else {
                const int K = L.cin / 4;
                f << "static const float4x4 " << upper << "_L" << li << "_W[" << G * 9 * K << "] = {\n";
                for (int gI = 0; gI < G; ++gI)
                    for (int t = 0; t < 9; ++t)
                        for (int k = 0; k < K; ++k) {
                            f << "  float4x4(";
                            for (int r = 0; r < 4; ++r)
                                for (int c = 0; c < 4; ++c)
                                    f << ((r | c) ? ", " : "") << fmt(L.w[(size_t(4 * gI + c) * L.cin + (4 * k + r)) * 9 + t]);
                            f << "),\n";
                        }
                f << "};\n";
            }
            f << "static const float4 " << upper << "_L" << li << "_B[" << G << "] = {\n";
            for (int gI = 0; gI < G; ++gI) {
                f << "  float4(";
                for (int j = 0; j < 4; ++j) f << (j ? ", " : "") << fmt(L.b[4 * gI + j]);
                f << "),\n";
            }
            f << "};\n";
        }
    }
    // C++
    {
        std::ofstream f(fs::path(root) / "src" / "neural" / "generated" / (m.name + ".inc"));
        f << "// AUTO-GENERATED by tools/nsr_trainer. Do not edit.\n";
        f << "// Included inside a function/namespace that defines NSR_LAYER(cin, cout, weights, bias).\n";
        for (size_t li = 0; li < m.layers.size(); ++li) {
            const auto& L = m.layers[li];
            f << "NSR_LAYER(" << L.cin << ", " << L.cout << ",\n  NSR_ARR(";
            for (size_t k = 0; k < L.w.size(); ++k) f << (k ? (k % 8 == 0 ? ",\n  " : ", ") : "") << fmt(L.w[k]) << "f";
            f << "),\n  NSR_ARR(";
            for (size_t k = 0; k < L.b.size(); ++k) f << (k ? ", " : "") << fmt(L.b[k]) << "f";
            f << "))\n";
        }
    }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    std::string dataDir, outRoot = ".", name = "nsr_s_x2";
    std::vector<std::string> fontPaths;
    int channels = 8, hidden = 2, iters = 20000, batch = 16, threads = int(std::max(1u, std::thread::hardware_concurrency()));
    int synthCount = 48;
    unsigned seed = 20261001;
    float lrMax = 2e-3f;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--data") dataDir = next();
        else if (a == "--font") fontPaths.push_back(next());
        else if (a == "--out-root") outRoot = next();
        else if (a == "--name") name = next();
        else if (a == "--channels") channels = std::stoi(next());
        else if (a == "--hidden") hidden = std::stoi(next());
        else if (a == "--iters") iters = std::stoi(next());
        else if (a == "--batch") batch = std::stoi(next());
        else if (a == "--threads") threads = std::stoi(next());
        else if (a == "--seed") seed = unsigned(std::stoul(next()));
        else if (a == "--synthetic") synthCount = std::stoi(next());
        else if (a == "--lr") lrMax = std::stof(next());
    }
    if (channels % 4 != 0) { std::fprintf(stderr, "channels must be a multiple of 4\n"); return 2; }

    std::mt19937 rng(seed);
    Dataset ds;
    std::vector<std::string> files;
    if (!dataDir.empty())
        for (auto& e : fs::directory_iterator(dataDir))
            if (e.path().extension() == ".png") files.push_back(e.path().string());
    std::sort(files.begin(), files.end());
    for (auto& fpath : files) {
        ImageRGB img;
        if (!loadPng(fpath, img)) { std::fprintf(stderr, "failed to load %s\n", fpath.c_str()); continue; }
        std::string base = fs::path(fpath).filename().string();
        // Hold out three photographs for validation
        if (base == "kodim03.png" || base == "kodim14.png" || base == "kodim23.png") {
            ds.val.push_back(img);
            ds.valNames.push_back(base);
        } else {
            ds.train.push_back(img);
        }
    }
    FontSet fonts;
    for (auto& fp : fontPaths)
        if (!fonts.add(fp)) std::fprintf(stderr, "warning: could not load font %s\n", fp.c_str());
    // stb_truetype stores raw pointers into the blobs; re-init after all loads (vector growth may reallocate)
    for (size_t i = 0; i < fonts.blobs.size(); ++i)
        stbtt_InitFont(&fonts.infos[i], fonts.blobs[i].data(), stbtt_GetFontOffsetForIndex(fonts.blobs[i].data(), 0));
    for (int i = 0; i < synthCount; ++i) {
        ImageRGB s = makeSynthetic(fonts, rng, 512, 512);
        if (i < 2 && std::getenv("NSR_DUMP_SYNTHETIC")) {
            std::vector<unsigned char> rgb8(s.px.size());
            for (size_t k = 0; k < rgb8.size(); ++k) rgb8[k] = (unsigned char)std::clamp(int(s.px[k] * 255.0f + 0.5f), 0, 255);
            stbi_write_png(("synthetic_" + std::to_string(i) + ".png").c_str(), s.w, s.h, 3, rgb8.data(), s.w * 3);
        }
        if (i < 4) { ds.val.push_back(s); ds.valNames.push_back("synthetic" + std::to_string(i)); }
        else ds.train.push_back(std::move(s));
    }
    std::printf("dataset: %zu train images, %zu validation images, %zu fonts\n", ds.train.size(), ds.val.size(), fonts.infos.size());
    if (ds.train.empty()) return 1;

    Model model;
    model.name = name;
    model.init(channels, hidden, rng);
    std::printf("model %s: C=%d hidden=%d params=%zu\n", name.c_str(), channels, hidden, model.paramCount());

    const int LR = 48, HRS = LR * 2, MARGIN = 8; // HR crop has 8px (4 LR px) extra on each side
    auto t0 = std::chrono::steady_clock::now();
    std::vector<Grads> tg(threads);
    for (auto& g : tg) g.init(model);
    Grads total;
    total.init(model);
    double emaLoss = 0;
    for (int it = 1; it <= iters; ++it) {
        for (auto& g : tg) g.zero();
        std::vector<double> losses(threads, 0.0);
        std::vector<std::thread> pool;
        for (int t = 0; t < threads; ++t) {
            pool.emplace_back([&, t]() {
                std::mt19937 trng(seed * 7919u + unsigned(it) * 104729u + unsigned(t) * 13u);
                for (int s = t; s < batch; s += threads) {
                    const ImageRGB& src = ds.train[trng() % ds.train.size()];
                    ImageRGB hr = cropAug(src, HRS + 2 * MARGIN, trng);
                    DegradeParams p = randomDegrade(trng);
                    ImageRGB lr = degrade(hr, p, trng);
                    Plane lrY = cropPlane(toLuma(lr), MARGIN / 2, MARGIN / 2, LR, LR);
                    Plane hrY = cropPlane(toLuma(hr), MARGIN, MARGIN, HRS, HRS);
                    losses[t] += trainSample(model, lrY, hrY, 8, tg[t]);
                }
            });
        }
        for (auto& th : pool) th.join();
        total.zero();
        double l = 0;
        for (int t = 0; t < threads; ++t) {
            l += losses[t];
            for (size_t li = 0; li < total.dw.size(); ++li) {
                for (size_t k = 0; k < total.dw[li].size(); ++k) total.dw[li][k] += tg[t].dw[li][k];
                for (size_t k = 0; k < total.db[li].size(); ++k) total.db[li][k] += tg[t].db[li][k];
            }
        }
        l /= batch;
        emaLoss = it == 1 ? l : emaLoss * 0.98 + l * 0.02;
        // warmup + cosine decay
        float warm = std::min(1.0f, it / 500.0f);
        float cosv = 0.5f * (1 + std::cos(3.14159265f * float(it) / float(iters)));
        float lr = lrMax * warm * (0.01f + 0.99f * cosv);
        adamStep(model, total, lr, it, 1.0f / batch);
        if (it % 500 == 0 || it == iters) {
            double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            std::printf("iter %d/%d loss %.5f lr %.2e  %.1fs\n", it, iters, emaLoss, lr, secs);
            std::fflush(stdout);
        }
        if (it % 5000 == 0 || it == iters) {
            ValResult v = validate(model, ds, true);
            std::printf("  val(jpeg q60) PSNR-Y: bilinear %.3f  catmull %.3f  nsr %.3f\n", v.bilinear, v.catmull, v.net);
            std::fflush(stdout);
        }
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    ValResult vClean = validate(model, ds, false), vJpeg = validate(model, ds, true);
    std::printf("final clean PSNR-Y: bilinear %.3f catmull %.3f nsr %.3f\n", vClean.bilinear, vClean.catmull, vClean.net);
    std::printf("final jpeg  PSNR-Y: bilinear %.3f catmull %.3f nsr %.3f\n", vJpeg.bilinear, vJpeg.catmull, vJpeg.net);
    exportModel(model, outRoot, vClean, vJpeg, iters, seed, secs);
    std::printf("exported %s\n", name.c_str());
    return 0;
}
