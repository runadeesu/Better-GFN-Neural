// Stream quality: compression blockiness on the 8x8 coding grid.
// Sums clipped luma steps across block boundaries (x % 8 == 7 -> x + 1) and
// inside blocks. Output (uint): [0] boundary sum * 4096, [1] boundary count,
// [2] interior sum * 4096, [3] interior count.
// gPassI = region origin x, y (multiples of 8), region w, h
#include "common.hlsli"

Texture2D<float4> gQualitySrc : register(t0);
RWByteAddressBuffer gQualityOut : register(u0);

groupshared uint sB, sBn, sI, sIn;

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    if (gi == 0) {
        sB = 0;
        sBn = 0;
        sI = 0;
        sIn = 0;
    }
    GroupMemoryBarrierWithGroupSync();
    int2 p = int2(gPassI.xy) + int2(id.xy);
    int2 size = int2(gIn.xy);
    if (all(id.xy < gPassI.zw) && all(p + 1 < size)) {
        float y0 = lumaY(gQualitySrc.Load(int3(p, 0)).rgb);
        float yx = lumaY(gQualitySrc.Load(int3(p + int2(1, 0), 0)).rgb);
        float yy = lumaY(gQualitySrc.Load(int3(p + int2(0, 1), 0)).rgb);
        // Clip so that real edges do not dominate the averages
        uint qx = uint(min(abs(yx - y0), 0.08) * 4096.0);
        uint qy = uint(min(abs(yy - y0), 0.08) * 4096.0);
        if ((p.x & 7) == 7) {
            InterlockedAdd(sB, qx);
            InterlockedAdd(sBn, 1u);
        } else {
            InterlockedAdd(sI, qx);
            InterlockedAdd(sIn, 1u);
        }
        if ((p.y & 7) == 7) {
            InterlockedAdd(sB, qy);
            InterlockedAdd(sBn, 1u);
        } else {
            InterlockedAdd(sI, qy);
            InterlockedAdd(sIn, 1u);
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if (gi == 0) {
        gQualityOut.InterlockedAdd(0, sB);
        gQualityOut.InterlockedAdd(4, sBn);
        gQualityOut.InterlockedAdd(8, sI);
        gQualityOut.InterlockedAdd(12, sIn);
    }
}
