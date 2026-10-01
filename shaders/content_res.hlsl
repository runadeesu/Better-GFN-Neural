// Content (stream) resolution analysis.
// When GeForce NOW scales a lower-resolution stream to a larger window, the top
// frequency octave of the captured frame is (almost) empty. We measure two
// difference-of-Gaussian band energies on a center crop:
//   e0 = mean |Y - G(0.6)|       (top band, near pixel Nyquist)
//   e3 = mean |G(1.2) - G(1.7)|  (a lower reference band)
// Native frames have e0/e3 >= ~1.1 (even after heavy compression); frames that
// were upscaled 2x have e0/e3 <= ~0.75 (see docs/ARCHITECTURE.md, calibration).
// gPassI.xy = region origin, gPassI.zw = region size
// Output (uint): [0] = sum e0 * 65536, [1] = sum e3 * 65536, [2] = count
#include "common.hlsli"

Texture2D<float4> gCur : register(t0);
RWByteAddressBuffer gOutBuf : register(u0);

#define TILE 16
#define APRON 6
#define TW (TILE + 2 * APRON)

groupshared float sY[TW * TW];
groupshared uint sAcc[3];

float gaussAt(int2 c, float sigma) {
    float acc = 0, wsum = 0;
    const float inv = -0.5 / (sigma * sigma);
    [loop] for (int j = -APRON; j <= APRON; ++j) {
        float wy = exp(float(j * j) * inv);
        [loop] for (int i = -APRON; i <= APRON; ++i) {
            float w = wy * exp(float(i * i) * inv);
            acc += w * sY[(c.y + j) * TW + (c.x + i)];
            wsum += w;
        }
    }
    return acc / wsum;
}

[numthreads(TILE, TILE, 1)]
void main(uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID, uint gi : SV_GroupIndex) {
    if (gi < 3) sAcc[gi] = 0;
    int2 tileOrigin = int2(gPassI.xy) + int2(gid.xy) * TILE - APRON;
    int2 size = int2(gIn.xy);
    for (uint k = gi; k < TW * TW; k += TILE * TILE) {
        int2 q = tileOrigin + int2(k % TW, k / TW);
        sY[k] = lumaY(gCur.Load(int3(clampCoord(q, size), 0)).rgb);
    }
    GroupMemoryBarrierWithGroupSync();
    int2 local = int2(gtid.xy) + APRON;
    int2 rel = int2(gid.xy) * TILE + int2(gtid.xy);
    if (all(rel < int2(gPassI.zw))) {
        float y = sY[local.y * TW + local.x];
        float g06 = gaussAt(local, 0.6);
        float g12 = gaussAt(local, 1.2);
        float g17 = gaussAt(local, 1.7);
        InterlockedAdd(sAcc[0], uint(abs(y - g06) * 65536.0));
        InterlockedAdd(sAcc[1], uint(abs(g12 - g17) * 65536.0));
        InterlockedAdd(sAcc[2], 1u);
    }
    GroupMemoryBarrierWithGroupSync();
    if (gi < 3) gOutBuf.InterlockedAdd(gi * 4, sAcc[gi]);
}
