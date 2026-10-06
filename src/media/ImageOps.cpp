#include "media/ImageOps.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string_view>

#include "stb_image_write.h"

namespace bgn {

namespace {

float srgbEncode(float v) {
    v = std::clamp(v, 0.0f, 1.0f);
    return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

float pqDecode(float e) {
    const float m1 = 0.1593017578125f, m2 = 78.84375f, c1 = 0.8359375f, c2 = 18.8515625f, c3 = 18.6875f;
    float p = std::pow(std::clamp(e, 0.0f, 1.0f), 1.0f / m2);
    return std::pow(std::max(p - c1, 0.0f) / (c2 - c3 * p), 1.0f / m1); // units of 10000 nits
}

uint8_t to8(float v) { return uint8_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); }

} // namespace

Image8 encodeToSdr8(const std::vector<float>& rgba, int w, int h, PixelEncoding enc, float sdrWhiteNits) {
    Image8 out;
    if (w <= 0 || h <= 0 || rgba.size() < size_t(w) * h * 4) return out;
    out.w = w;
    out.h = h;
    out.rgb.resize(size_t(w) * h * 3);
    const float white = std::max(sdrWhiteNits, 40.0f);
    for (size_t i = 0; i < size_t(w) * h; ++i) {
        float c[3] = {rgba[i * 4], rgba[i * 4 + 1], rgba[i * 4 + 2]};
        for (float& v : c)
            if (!std::isfinite(v)) v = 0.0f;
        if (enc != PixelEncoding::SdrGamma) {
            // to linear, relative to SDR white
            for (float& v : c) v = enc == PixelEncoding::ScRgbLinear ? std::max(v, 0.0f) * 80.0f / white : pqDecode(v) * 10000.0f / white;
            // Luminance based extended Reinhard (white point at 4x SDR white)
            const float L = 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
            if (L > 1e-6f) {
                const float Lw = 4.0f, Ld = L * (1.0f + L / (Lw * Lw)) / (1.0f + L);
                for (float& v : c) v *= Ld / L;
            }
            for (float& v : c) v = srgbEncode(v);
        }
        for (int k = 0; k < 3; ++k) out.rgb[i * 3 + k] = to8(c[k]);
    }
    return out;
}

Image8 resizeBilinear(const Image8& src, int w, int h) {
    Image8 out;
    if (src.empty() || w <= 0 || h <= 0) return out;
    out.w = w;
    out.h = h;
    out.rgb.resize(size_t(w) * h * 3);
    const float sx = float(src.w) / float(w), sy = float(src.h) / float(h);
    for (int y = 0; y < h; ++y) {
        float fy = std::clamp((y + 0.5f) * sy - 0.5f, 0.0f, float(src.h - 1));
        int y0 = int(fy), y1 = std::min(y0 + 1, src.h - 1);
        float ty = fy - y0;
        for (int x = 0; x < w; ++x) {
            float fx = std::clamp((x + 0.5f) * sx - 0.5f, 0.0f, float(src.w - 1));
            int x0 = int(fx), x1 = std::min(x0 + 1, src.w - 1);
            float tx = fx - x0;
            for (int k = 0; k < 3; ++k) {
                auto px = [&](int xx, int yy) { return float(src.rgb[(size_t(yy) * src.w + xx) * 3 + k]); };
                float v = (px(x0, y0) * (1 - tx) + px(x1, y0) * tx) * (1 - ty) + (px(x0, y1) * (1 - tx) + px(x1, y1) * tx) * ty;
                out.rgb[(size_t(y) * w + x) * 3 + k] = uint8_t(std::lround(std::clamp(v, 0.0f, 255.0f)));
            }
        }
    }
    return out;
}

Image8 sideBySide(const Image8& left, const Image8& right, int gap) {
    Image8 out;
    if (left.empty() || right.empty()) return out;
    const int lw = std::max(1, int(std::lround(double(left.w) * right.h / left.h)));
    Image8 l = (left.h == right.h && left.w == lw) ? left : resizeBilinear(left, lw, right.h);
    gap = std::max(gap, 0);
    out.w = l.w + gap + right.w;
    out.h = right.h;
    out.rgb.assign(size_t(out.w) * out.h * 3, 0);
    for (int y = 0; y < out.h; ++y) {
        std::copy_n(&l.rgb[size_t(y) * l.w * 3], size_t(l.w) * 3, &out.rgb[size_t(y) * out.w * 3]);
        for (int x = 0; x < gap; ++x) {
            uint8_t* p = &out.rgb[(size_t(y) * out.w + l.w + x) * 3];
            p[0] = 45;  // divider in the app accent color
            p[1] = 226;
            p[2] = 176;
        }
        std::copy_n(&right.rgb[size_t(y) * right.w * 3], size_t(right.w) * 3, &out.rgb[(size_t(y) * out.w + l.w + gap) * 3]);
    }
    return out;
}

bool writePng(const std::filesystem::path& path, const Image8& img) {
    if (img.empty()) return false;
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    auto sink = [](void* ctx, void* data, int size) { static_cast<std::ofstream*>(ctx)->write(static_cast<const char*>(data), size); };
    const int ok = stbi_write_png_to_func(sink, &f, img.w, img.h, 3, img.rgb.data(), img.w * 3);
    f.close();
    return ok != 0 && !f.fail();
}

std::string screenshotBaseName(const std::string& game, const std::tm& t) {
    std::string name;
    for (char ch : game) {
        const unsigned char u = static_cast<unsigned char>(ch);
        if (u < 32 || std::string_view("<>:\"/\\|?*").find(ch) != std::string_view::npos) name += '_';
        else name += ch;
    }
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) name.pop_back();
    while (!name.empty() && name.front() == ' ') name.erase(name.begin());
    if (name.size() > 60) {
        name.resize(60);
        // do not cut a UTF-8 sequence in half
        while (!name.empty() && (static_cast<unsigned char>(name.back()) & 0xC0) == 0x80) name.pop_back();
        if (!name.empty() && (static_cast<unsigned char>(name.back()) & 0x80)) name.pop_back();
    }
    if (name.empty()) name = "GeForce NOW";
    char stamp[64];
    std::snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d_%02d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    return name + "_" + stamp;
}

} // namespace bgn
