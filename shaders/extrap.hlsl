// Stutter concealment: motion-extrapolated frame shown only when the next
// stream frame is late (network hiccup). It continues the current motion for
// a fraction of a frame instead of freezing, and never delays real frames.
//  * Static pixels (HUD, UI, still background) are copied untouched
//  * Low flow confidence or large motion falls back to the current frame
// gInterp.x = e (fraction of a frame to extrapolate, 0..1)
#include "common.hlsli"

Texture2D<float4> gCurFinal : register(t0);
Texture2D<float4> gFlowTex : register(t1);
RWTexture2D<float4> gExtra : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    int2 p = int2(id.xy);
    int2 size = int2(gOut.xy);
    if (any(p >= size)) return;
    float3 cur = gCurFinal.Load(int3(p, 0)).rgb;
    const float e = gInterp.x;
    const float s = gSharpen.y; // output px per input px
    float2 outPos = float2(p) + 0.5;
    float2 inPos = outPos / s;
    // flow f: displacement from the current frame to the previous one (input px).
    // Moving on, a pixel at outPos came from outPos + f * e in the current frame.
    float4 f0 = gFlowTex.SampleLevel(gLinearClamp, inPos / gFlow.z / gFlow.xy, 0);
    float4 f = gFlowTex.SampleLevel(gLinearClamp, (inPos + f0.xy * e) / gFlow.z / gFlow.xy, 0);
    float conf = exp(-f.z / (0.008 + 0.3 * f.w));
    float speed = length(f.xy);
    if (speed < 0.25) {
        gExtra[p] = float4(cur, 1.0);
        return;
    }
    float2 src = outPos + f.xy * s * e;
    float3 moved = gCurFinal.SampleLevel(gLinearClamp, src * gOut.zw, 0).rgb;
    // Leaving the image or unreliable / very fast motion => keep the real frame
    float trust = saturate(conf * 1.5) * (1.0 - smoothstep(24.0, 48.0, speed));
    if (any(src < 0.0) || any(src > gOut.xy)) trust = 0.0;
    gExtra[p] = float4(lerp(cur, moved, trust), 1.0);
}
