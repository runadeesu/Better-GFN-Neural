// Frame interpolation (optical flow, t = 0.5) on final output frames.
//  * Bidirectional motion-compensated blend using the backward flow with one
//    fixed-point iteration to approximate mid-frame flow
//  * HUD/UI protection: pixels that did not change between the two frames are
//    copied untouched (static overlays stay perfectly crisp)
//  * Occlusion / mismatch handling: falls back to the warped current frame
//  * Low flow confidence: falls back towards the current frame
// gInterp = t, static threshold, occlusion threshold
#include "common.hlsli"

Texture2D<float4> gPrevFinal : register(t0);
Texture2D<float4> gCurFinal : register(t1);
Texture2D<float4> gFlowTex : register(t2);
RWTexture2D<float4> gMid : register(u0);

float4 flowAt(float2 inPos) { return gFlowTex.SampleLevel(gLinearClamp, inPos / gFlow.z / gFlow.xy, 0); }

float relDiff(float3 a, float3 b) {
    float3 d = abs(a - b);
    float m = max(max(a.r, a.g), max(max(a.b, b.r), max(b.g, b.b)));
    return max(d.r, max(d.g, d.b)) / (0.05 + 0.25 * m);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    int2 p = int2(id.xy);
    int2 size = int2(gOut.xy);
    if (any(p >= size)) return;
    float3 cCur = gCurFinal.Load(int3(p, 0)).rgb;
    float3 cPrev = gPrevFinal.Load(int3(p, 0)).rgb;
    const float t = gInterp.x;

    if (relDiff(cCur, cPrev) < gInterp.y) {
        gMid[p] = float4(cCur, 1.0); // static pixel (HUD, UI, still background)
        return;
    }
    const float s = gSharpen.y; // output px per input px
    float2 outPos = float2(p) + 0.5;
    float2 inPos = outPos / s;
    float4 f0 = flowAt(inPos);
    // fixed-point iteration: flow at the position the mid-frame pixel came from
    float4 f = flowAt(inPos - f0.xy * t);
    float conf = exp(-f.z / (0.008 + 0.3 * f.w));
    float2 vo = f.xy * s;
    float2 posCur = outPos - vo * t;          // where the pixel is in the current frame
    float2 posPrev = outPos + vo * (1.0 - t); // where it was in the previous frame
    float3 a = gCurFinal.SampleLevel(gLinearClamp, posCur * gOut.zw, 0).rgb;
    float3 b = gPrevFinal.SampleLevel(gLinearClamp, posPrev * gOut.zw, 0).rgb;
    float mismatch = relDiff(a, b);
    float3 blended = lerp(a, b, 1.0 - t);
    float occ = saturate((mismatch - gInterp.z) / max(gInterp.z, 1e-3));
    float3 mid = lerp(blended, a, occ);
    // Unreliable flow => prefer the newest real frame (never invent detail)
    mid = lerp(cCur, mid, saturate(conf * 1.5));
    gMid[p] = float4(mid, 1.0);
}
