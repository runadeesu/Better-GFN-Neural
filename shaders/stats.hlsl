// Scene statistics for Auto Color: 64-bin luma histogram + average luma,
// computed on the 1/8 resolution luma pyramid level (cheap).
// Output buffer layout (uint): [0..63] histogram, [64] luma sum * 1024, [65] count
// gPassI.xy = luma level size
#include "common.hlsli"

Texture2D<float> gLuma : register(t0);
RWByteAddressBuffer gStats : register(u0);

groupshared uint sHist[64];
groupshared uint sSum;
groupshared uint sCount;

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    if (gi < 64) sHist[gi] = 0;
    if (gi == 0) { sSum = 0; sCount = 0; }
    GroupMemoryBarrierWithGroupSync();
    int2 p = int2(id.xy);
    if (all(p < int2(gPassI.xy))) {
        float y = saturate(gLuma.Load(int3(p, 0)));
        uint bin = min(uint(y * 64.0), 63u);
        InterlockedAdd(sHist[bin], 1u);
        InterlockedAdd(sSum, uint(y * 1024.0));
        InterlockedAdd(sCount, 1u);
    }
    GroupMemoryBarrierWithGroupSync();
    if (gi < 64 && sHist[gi] != 0) gStats.InterlockedAdd(gi * 4, sHist[gi]);
    if (gi == 0) {
        gStats.InterlockedAdd(64 * 4, sSum);
        gStats.InterlockedAdd(65 * 4, sCount);
    }
}
