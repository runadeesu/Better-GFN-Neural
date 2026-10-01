// Ingest: crop the captured surface to the game's client area and convert it
// to the working color space (FP16). GPU only, the capture texture is read in place.
#include "common.hlsli"

Texture2D<float4> gCapture : register(t0);
RWTexture2D<float4> gCur : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    int2 p = int2(id.xy);
    if (any(p >= int2(gIn.xy))) return;
    int2 src = clampCoord(p + int2(gSrc.xy), int2(gSrc.zw));
    float3 c = gCapture.Load(int3(src, 0)).rgb;
    gCur[p] = float4(inputToWorking(c, gFrame.y), 1.0);
}
