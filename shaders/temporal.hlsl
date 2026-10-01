// Temporal reconstruction: Temporal Anti-Flicker, Temporal Stabilization,
// Edge Stability and Ghosting Reduction.
// History is motion-compensated with the optical flow, clipped to the current
// frame's local color distribution (variance clipping in YCoCg) and blended
// with a confidence-driven weight. Static regions (HUD, text, distant geometry,
// fences, foliage) receive the strongest accumulation.
// gTemporal = strength, clamp gamma, static boost
#include "common.hlsli"

Texture2D<float4> gClean : register(t0);
Texture2D<float4> gHistory : register(t1);
Texture2D<float4> gFlowTex : register(t2);
RWTexture2D<float4> gStable : register(u0);

float3 clipAabb(float3 q, float3 bmin, float3 bmax) {
    float3 center = 0.5 * (bmax + bmin);
    float3 extents = max(0.5 * (bmax - bmin), 1e-5);
    float3 offs = q - center;
    float3 ts = abs(extents / max(abs(offs), 1e-6));
    float t = saturate(min(ts.x, min(ts.y, ts.z)));
    return center + offs * t;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    int2 p = int2(id.xy);
    int2 size = int2(gIn.xy);
    if (any(p >= size)) return;
    float3 c = gClean.Load(int3(p, 0)).rgb;
    if (!hasFlag(BGN_FLAG_HISTORY_VALID) || gTemporal.x <= 0.0) {
        gStable[p] = float4(c, 1.0);
        return;
    }

    float3 cy = rgbToYCoCg(c);
    float3 m1 = 0, m2 = 0, bmin = cy, bmax = cy;
    [unroll] for (int j = -1; j <= 1; ++j) {
        [unroll] for (int i = -1; i <= 1; ++i) {
            float3 s = rgbToYCoCg(gClean.Load(int3(clampCoord(p + int2(i, j), size), 0)).rgb);
            m1 += s;
            m2 += s * s;
            bmin = min(bmin, s);
            bmax = max(bmax, s);
        }
    }
    m1 /= 9.0;
    float3 sd = sqrt(max(m2 / 9.0 - m1 * m1, 0.0));

    float2 v = 0;
    float conf = 1.0;
    if (hasFlag(BGN_FLAG_FLOW_VALID)) {
        float2 uvf = (float2(p) + 0.5) / gFlow.z / gFlow.xy;
        float4 f = gFlowTex.SampleLevel(gLinearClamp, uvf, 0);
        v = f.xy;
        conf = exp(-f.z / (0.008 + 0.3 * f.w));
    }
    float motion = length(v);
    bool isStatic = motion < 0.3;

    float2 prevPos = float2(p) + 0.5 + v;
    bool inside = all(prevPos >= 0.0) && all(prevPos <= gIn.xy);
    float3 h = isStatic ? gHistory.Load(int3(p, 0)).rgb : gHistory.SampleLevel(gLinearClamp, prevPos * gIn.zw, 0).rgb;
    float3 hy = rgbToYCoCg(h);

    // Static content tolerates a wider box => strong anti-flicker on thin details
    float gamma = gTemporal.y * (isStatic ? 1.0 + 0.5 * gTemporal.z : 1.0);
    float3 lo = max(bmin, m1 - gamma * sd), hi = min(bmax, m1 + gamma * sd);
    lo = min(lo, cy);
    hi = max(hi, cy);
    float3 clipped = clipAabb(hy, lo, hi);

    float strength = gTemporal.x;
    float alpha = strength * (isStatic ? 0.9 : 0.78);
    alpha *= lerp(0.35, 1.0, conf);                       // unreliable motion => trust current frame
    alpha *= 1.0 - 0.6 * smoothstep(10.0, 60.0, motion);  // very fast motion => less history
    float ghost = saturate(length(hy - clipped) / (length(sd) + 0.02));
    alpha *= 1.0 - 0.65 * ghost;                          // history disagreed => ghosting risk
    if (!inside) alpha = 0.0;

    float3 outY = lerp(cy, clipped, alpha);
    gStable[p] = float4(yCoCgToRgb(outY), 1.0);
}
