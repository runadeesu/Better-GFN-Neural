// Luma pyramid: 2x2 box reduction. With LUMA_FROM_COLOR the source is an RGBA
// working-space texture, otherwise a single-channel luma level.
// gPassI.xy = destination size, gPassI.zw = source size
#include "common.hlsli"

#ifdef LUMA_FROM_COLOR
Texture2D<float4> gSrcTex : register(t0);
float fetch(int2 p) { return lumaY(gSrcTex.Load(int3(p, 0)).rgb); }
#else
Texture2D<float> gSrcTex : register(t0);
float fetch(int2 p) { return gSrcTex.Load(int3(p, 0)); }
#endif
RWTexture2D<float> gDst : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    int2 p = int2(id.xy);
    if (any(p >= int2(gPassI.xy))) return;
    int2 s = p * 2;
    int2 srcSize = int2(gPassI.zw);
    float v = fetch(clampCoord(s, srcSize)) + fetch(clampCoord(s + int2(1, 0), srcSize)) + fetch(clampCoord(s + int2(0, 1), srcSize)) +
              fetch(clampCoord(s + int2(1, 1), srcSize));
    gDst[p] = v * 0.25;
}
