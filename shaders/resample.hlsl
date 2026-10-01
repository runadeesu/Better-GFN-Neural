// Resampling.
//  RESAMPLE_UP       : Lanczos-2 (4x4) with anti-ringing clamp ("Fast Reconstruct")
//  RESAMPLE_DOWN     : area-correct Lanczos-2 with the kernel widened by the scale
//  RESAMPLE_BILINEAR : plain bilinear (baseline / Native)
// gPassI.xy = destination size, gPassI.zw = source size
#include "common.hlsli"

Texture2D<float4> gSrcTex : register(t0);
RWTexture2D<float4> gDst : register(u0);

float lanczos2(float x) {
    x = abs(x);
    if (x < 1e-5) return 1.0;
    if (x >= 2.0) return 0.0;
    const float pi = 3.14159265;
    return 2.0 * sin(pi * x) * sin(pi * x * 0.5) / (pi * pi * x * x);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    int2 p = int2(id.xy);
    int2 dstSize = int2(gPassI.xy), srcSize = int2(gPassI.zw);
    if (any(p >= dstSize)) return;
    float2 scale = float2(srcSize) / float2(dstSize); // source px per destination px
    float2 srcPos = (float2(p) + 0.5) * scale;        // in source pixel units (centers at +0.5)

#if defined(RESAMPLE_BILINEAR)
    gDst[p] = float4(gSrcTex.SampleLevel(gLinearClamp, srcPos / float2(srcSize), 0).rgb, 1.0);
#elif defined(RESAMPLE_UP)
    float2 f = srcPos - 0.5;
    int2 b = int2(floor(f));
    float2 t = f - float2(b);
    float wx[4], wy[4];
    [unroll] for (int i = 0; i < 4; ++i) {
        wx[i] = lanczos2(float(i - 1) - t.x);
        wy[i] = lanczos2(float(i - 1) - t.y);
    }
    float3 acc = 0, mn = 1e9, mx = -1e9;
    float wsum = 0;
    [unroll] for (int j = 0; j < 4; ++j) {
        [unroll] for (int i2 = 0; i2 < 4; ++i2) {
            float3 s = gSrcTex.Load(int3(clampCoord(b + int2(i2 - 1, j - 1), srcSize), 0)).rgb;
            float w = wx[i2] * wy[j];
            acc += s * w;
            wsum += w;
            if (i2 >= 1 && i2 <= 2 && j >= 1 && j <= 2) {
                mn = min(mn, s);
                mx = max(mx, s);
            }
        }
    }
    float3 r = acc / wsum;
    // Anti-ringing: limit to the 2x2 neighbourhood range (soft, keeps some crispness)
    r = lerp(r, clamp(r, mn, mx), 0.8);
    gDst[p] = float4(r, 1.0);
#else // RESAMPLE_DOWN
    float2 support = 2.0 * max(scale, 1.0);
    int2 lo = int2(floor(srcPos - support)), hi = int2(ceil(srcPos + support));
    float3 acc = 0;
    float wsum = 0;
    [loop] for (int y = lo.y; y <= hi.y; ++y) {
        float wy = lanczos2((float(y) + 0.5 - srcPos.y) / max(scale.y, 1.0));
        if (wy == 0.0) continue;
        [loop] for (int x = lo.x; x <= hi.x; ++x) {
            float wx = lanczos2((float(x) + 0.5 - srcPos.x) / max(scale.x, 1.0));
            if (wx == 0.0) continue;
            float w = wx * wy;
            acc += gSrcTex.Load(int3(clampCoord(int2(x, y), srcSize), 0)).rgb * w;
            wsum += w;
        }
    }
    gDst[p] = float4(max(acc / max(wsum, 1e-5), 0.0), 1.0);
#endif
}
