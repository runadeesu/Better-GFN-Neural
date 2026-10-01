// Stream compression cleanup:
//   * Compression noise / mosquito noise reduction (edge-aware bilateral whose
//     range sigma grows near strong edges where ringing lives)
//   * Blocking / macroblock cleanup (extra smoothing of small steps in flat areas)
//   * Banding + color banding reduction (f3kdb-style gradient reconstruction)
//   * Dark scene cleanup (stronger thresholds in low-luma regions)
// gCleanup = deblock, deband, denoise, noise sigma
#include "common.hlsli"

Texture2D<float4> gCur : register(t0);
RWTexture2D<float4> gClean : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    int2 p = int2(id.xy);
    int2 size = int2(gIn.xy);
    if (any(p >= size)) return;

    const float deblock = gCleanup.x, deband = gCleanup.y, denoise = gCleanup.z, noise = gCleanup.w;
    float3 c = gCur.Load(int3(p, 0)).rgb;
    float3 cy = rgbToYCoCg(c);

    const int R = hasFlag(BGN_FLAG_CLEANUP_HQ) ? 3 : 2;

    // Local structure: luma range in the full window and in the 3x3 core
    float ymin = cy.x, ymax = cy.x, ymin3 = cy.x, ymax3 = cy.x;
    [loop] for (int j = -R; j <= R; ++j) {
        [loop] for (int i = -R; i <= R; ++i) {
            float y = lumaY(gCur.Load(int3(clampCoord(p + int2(i, j), size), 0)).rgb);
            ymin = min(ymin, y);
            ymax = max(ymax, y);
            if (abs(i) <= 1 && abs(j) <= 1) {
                ymin3 = min(ymin3, y);
                ymax3 = max(ymax3, y);
            }
        }
    }
    float range = ymax - ymin, range3 = ymax3 - ymin3;
    float flat = 1.0 - smoothstep(0.03, 0.12, range);
    float edgeNear = smoothstep(0.12, 0.30, range);
    float mosquito = edgeNear * (1.0 - smoothstep(0.02, 0.07, range3));
    float dark = hasFlag(BGN_FLAG_DARK_CLEANUP) ? 1.0 - smoothstep(0.06, 0.25, cy.x) : 0.0;

    float sigmaR = denoise * (0.010 + 1.5 * noise) + deblock * 0.022 * flat + denoise * 0.035 * mosquito;
    sigmaR *= 1.0 + 0.8 * dark;
    float3 result = c;
    if (sigmaR > 1e-4) {
        const float sigmaS = R == 3 ? 1.9 : 1.4;
        float invS = -0.5 / (sigmaS * sigmaS), invR = -0.5 / (sigmaR * sigmaR);
        float3 acc = 0;
        float wsum = 0;
        [loop] for (int j2 = -R; j2 <= R; ++j2) {
            [loop] for (int i2 = -R; i2 <= R; ++i2) {
                float3 s = gCur.Load(int3(clampCoord(p + int2(i2, j2), size), 0)).rgb;
                float3 d = rgbToYCoCg(s) - cy;
                float dr = d.x * d.x + 0.6 * (d.y * d.y + d.z * d.z);
                float w = exp(float(i2 * i2 + j2 * j2) * invS + dr * invR);
                acc += s * w;
                wsum += w;
            }
        }
        result = acc / max(wsum, 1e-6);
    }

    if (deband > 0.0) {
        // Sample four points on a randomly rotated cross; if they are all within
        // the threshold the area is a smooth gradient => replace by their average.
        float rnd = hashFloat(uint2(p), gFrame.x & 7u);
        float radius = lerp(6.0, 14.0, rnd) * saturate(gIn.y / 1080.0 + 0.25);
        float ang = rnd * 6.2831853;
        float2 dir = float2(cos(ang), sin(ang)) * radius;
        float3 s0 = gCur.Load(int3(clampCoord(p + int2(dir), size), 0)).rgb;
        float3 s1 = gCur.Load(int3(clampCoord(p - int2(dir), size), 0)).rgb;
        float3 s2 = gCur.Load(int3(clampCoord(p + int2(-dir.y, dir.x), size), 0)).rgb;
        float3 s3 = gCur.Load(int3(clampCoord(p - int2(-dir.y, dir.x), size), 0)).rgb;
        float thr = deband * (0.0045 + 0.0045 * dark);
        float3 avg = (s0 + s1 + s2 + s3) * 0.25;
        float3 dmax = max(max(abs(s0 - result), abs(s1 - result)), max(abs(s2 - result), abs(s3 - result)));
        if (all(dmax < thr)) result = lerp(result, avg, 0.85);
    }
    gClean[p] = float4(result, 1.0);
}
