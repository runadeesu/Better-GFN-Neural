#include "benchmark/GpuTestUtil.h"

#include <cstring>

namespace bgn {

void renderSyntheticScene(const GpuContext& g, ID3D11UnorderedAccessView* uav, int w, int h, float t, float pan) {
    gpu::PassCB p{};
    p.gPassI = u4(uint32_t(w), uint32_t(h));
    p.gPassF = f4(t, pan);
    g.setPass(p);
    dispatchCompute(g.ctx, g.cs(ShaderId::synthetic_cs), {}, {uav}, groups(w, 8), groups(h, 8));
}

float halfToFloat(uint16_t h) {
    uint32_t sign = (h & 0x8000u) << 16, exp = (h >> 10) & 0x1Fu, mant = h & 0x3FFu;
    uint32_t bits;
    if (exp == 0) {
        if (mant == 0) {
            bits = sign;
        } else {
            exp = 127 - 15 + 1;
            while (!(mant & 0x400u)) {
                mant <<= 1;
                --exp;
            }
            mant &= 0x3FFu;
            bits = sign | (exp << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        bits = sign | 0x7F800000u | (mant << 13);
    } else {
        bits = sign | ((exp + 127 - 15) << 23) | (mant << 13);
    }
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

uint16_t floatToHalf(float f) {
    uint32_t x;
    std::memcpy(&x, &f, 4);
    uint32_t sign = (x >> 16) & 0x8000u;
    int exp = int((x >> 23) & 0xFFu) - 127 + 15;
    uint32_t mant = x & 0x7FFFFFu;
    if (exp <= 0) return uint16_t(sign);
    if (exp >= 31) return uint16_t(sign | 0x7C00u);
    // round to nearest
    uint32_t r = (uint32_t(exp) << 10) | (mant >> 13);
    if (mant & 0x1000u) ++r;
    return uint16_t(sign | r);
}

bool readbackTexture(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D* tex, std::vector<float>& rgba, int& w, int& h) {
    D3D11_TEXTURE2D_DESC d{};
    tex->GetDesc(&d);
    D3D11_TEXTURE2D_DESC s = d;
    s.Usage = D3D11_USAGE_STAGING;
    s.BindFlags = 0;
    s.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    s.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(dev->CreateTexture2D(&s, nullptr, &staging))) return false;
    ctx->CopyResource(staging.Get(), tex);
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) return false;
    w = int(d.Width);
    h = int(d.Height);
    rgba.resize(size_t(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        const uint8_t* row = static_cast<const uint8_t*>(m.pData) + size_t(y) * m.RowPitch;
        for (int x = 0; x < w; ++x) {
            float* o = &rgba[(size_t(y) * w + x) * 4];
            if (d.Format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
                const uint16_t* p = reinterpret_cast<const uint16_t*>(row) + x * 4;
                for (int c = 0; c < 4; ++c) o[c] = halfToFloat(p[c]);
            } else if (d.Format == DXGI_FORMAT_B8G8R8A8_UNORM) {
                const uint8_t* p = row + x * 4;
                o[0] = p[2] / 255.0f;
                o[1] = p[1] / 255.0f;
                o[2] = p[0] / 255.0f;
                o[3] = p[3] / 255.0f;
            } else {
                const uint8_t* p = row + x * 4;
                for (int c = 0; c < 4; ++c) o[c] = p[c] / 255.0f;
            }
        }
    }
    ctx->Unmap(staging.Get(), 0);
    return true;
}

} // namespace bgn
