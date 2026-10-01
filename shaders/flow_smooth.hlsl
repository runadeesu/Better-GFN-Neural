// Flow post-filter: 3x3 vector median (robust outlier removal), keeps the
// match cost of the chosen vector's cell. gPassI.xy = grid size.
#include "common.hlsli"

Texture2D<float4> gFlowIn : register(t0);
RWTexture2D<float4> gFlowOut : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    int2 p = int2(id.xy);
    int2 grid = int2(gPassI.xy);
    if (any(p >= grid)) return;
    float4 v[9];
    [unroll] for (int j = 0; j < 3; ++j)
        [unroll] for (int i = 0; i < 3; ++i) v[j * 3 + i] = gFlowIn.Load(int3(clampCoord(p + int2(i - 1, j - 1), grid), 0));
    int bestIdx = 4;
    float bestSum = 1e9;
    [unroll] for (int a = 0; a < 9; ++a) {
        float s = 0;
        [unroll] for (int b = 0; b < 9; ++b) s += abs(v[a].x - v[b].x) + abs(v[a].y - v[b].y);
        // Prefer the center vector on ties
        s -= (a == 4) ? 1e-3 : 0.0;
        if (s < bestSum) { bestSum = s; bestIdx = a; }
    }
    float4 c = v[4];
    float4 m = v[bestIdx];
    // Keep the center if it is close to the median (preserves detail at motion boundaries)
    float dev = abs(c.x - m.x) + abs(c.y - m.y);
    gFlowOut[p] = dev < 1.5 ? c : float4(m.xy, max(c.z, m.z), c.w);
}
