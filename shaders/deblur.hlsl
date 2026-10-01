// Deblur: Adaptive Deblur, Motion Deblur, Edge Recovery, Detail Recovery.
//  * Isotropic part: band-split unsharp deconvolution (separate edge / detail gains)
//  * Motion part: 1-D deconvolution along the optical-flow direction; strength
//    follows motion magnitude and backs off for very fast motion (artifact risk)
//  * Luma-only, overshoot-limited to the local min/max (no halos / color fringes)
// gDeblur = strength, motion strength, edge weight, detail weight
#include "common.hlsli"

Texture2D<float4> gStable : register(t0);
Texture2D<float4> gFlowTex : register(t1);
RWTexture2D<float4> gOutTex : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    int2 p = int2(id.xy);
    int2 size = int2(gIn.xy);
    if (any(p >= size)) return;
    float3 c = gStable.Load(int3(p, 0)).rgb;
    float strength = gDeblur.x;
    if (strength <= 0.0) {
        gOutTex[p] = float4(c, 1.0);
        return;
    }
    float y = lumaY(c);
    // 3x3 gaussian blur + min/max
    const float k[3] = {0.25, 0.5, 0.25};
    float blur = 0, ymin = y, ymax = y;
    [unroll] for (int j = -1; j <= 1; ++j) {
        [unroll] for (int i = -1; i <= 1; ++i) {
            float s = lumaY(gStable.Load(int3(clampCoord(p + int2(i, j), size), 0)).rgb);
            blur += s * k[i + 1] * k[j + 1];
            ymin = min(ymin, s);
            ymax = max(ymax, s);
        }
    }
    // Wider ring for detail band (radius 2 cross)
    float ring = 0;
    ring += lumaY(gStable.Load(int3(clampCoord(p + int2(2, 0), size), 0)).rgb);
    ring += lumaY(gStable.Load(int3(clampCoord(p + int2(-2, 0), size), 0)).rgb);
    ring += lumaY(gStable.Load(int3(clampCoord(p + int2(0, 2), size), 0)).rgb);
    ring += lumaY(gStable.Load(int3(clampCoord(p + int2(0, -2), size), 0)).rgb);
    float wide = 0.6 * blur + 0.4 * ring * 0.25;

    float range = ymax - ymin;
    float edgeW = smoothstep(0.06, 0.25, range);
    float detailW = smoothstep(0.008, 0.045, range) * (1.0 - edgeW);
    float delta = strength * (gDeblur.z * edgeW * (y - blur) * 1.2 + gDeblur.w * detailW * (y - wide) * 1.6);

    if (hasFlag(BGN_FLAG_MOTION_DEBLUR) && hasFlag(BGN_FLAG_FLOW_VALID)) {
        float2 uvf = (float2(p) + 0.5) / gFlow.z / gFlow.xy;
        float4 f = gFlowTex.SampleLevel(gLinearClamp, uvf, 0);
        float m = length(f.xy);
        if (m > 0.75) {
            float conf = exp(-f.z / (0.008 + 0.3 * f.w));
            float2 dir = f.xy / m;
            float L = clamp(m * 0.35, 0.75, 3.0);
            float2 uv0 = (float2(p) + 0.5 + dir * L) * gIn.zw, uv1 = (float2(p) + 0.5 - dir * L) * gIn.zw;
            float bdir = 0.5 * (lumaY(gStable.SampleLevel(gLinearClamp, uv0, 0).rgb) + lumaY(gStable.SampleLevel(gLinearClamp, uv1, 0).rgb));
            // Adaptive: ramps in with motion, backs off for very fast motion
            float km = gDeblur.y * strength * smoothstep(0.75, 6.0, m) * (1.0 - 0.6 * smoothstep(24.0, 64.0, m)) * conf;
            delta += km * (y - bdir) * 1.5;
        }
    }
    float overshoot = 0.02 + 0.03 * (1.0 - edgeW);
    float yNew = clamp(y + delta, ymin - overshoot, ymax + overshoot);
    gOutTex[p] = float4(c + (yNew - y), 1.0);
}
