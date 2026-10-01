// Present: blit the final (or interpolated) frame into the swap chain back
// buffer with letterboxing, optional before/after split view and dithering.
#include "common.hlsli"

Texture2D<float4> gFrameTex : register(t0);
Texture2D<float4> gOriginal : register(t1); // ingest (working space, input res) for the split view

struct VsOut {
    float4 pos : SV_Position;
};

VsOut vsMain(uint vid : SV_VertexID) {
    VsOut o;
    float2 uv = float2((vid << 1) & 2, vid & 2);
    o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

float3 workingToOutput(float3 c) {
    if (gFrame.z == BGN_OUTPUT_SCRGB) {
        if (gFrame.y == BGN_INPUT_SCRGB) return pqDecode(saturate(c)) * (10000.0 / 80.0);
        return srgbToLinear(saturate(c)) * (gHdr.x / 80.0);
    }
    if (gFrame.y == BGN_INPUT_SCRGB) return linearToSrgb(saturate(pqDecode(saturate(c)) * (10000.0 / 200.0)));
    return saturate(c);
}

float4 psMain(VsOut i) : SV_Target {
    float2 px = i.pos.xy;
    float2 rel = (px - gPresent.xy) / gPresent.zw;
    if (any(rel < 0.0) || any(rel > 1.0)) return float4(0, 0, 0, 1);
    float3 c;
    if (hasFlag(BGN_FLAG_COMPARE_SPLIT) && rel.x < gPresent2.w) {
        c = workingToOutput(gOriginal.SampleLevel(gLinearClamp, rel, 0).rgb);
    } else {
        c = gFrameTex.SampleLevel(gLinearClamp, rel, 0).rgb;
    }
    if (hasFlag(BGN_FLAG_COMPARE_SPLIT) && abs(rel.x - gPresent2.w) * gPresent.z < 1.0) c = float3(0.46, 0.85, 0.0) * (gFrame.z == BGN_OUTPUT_SCRGB ? 2.5 : 1.0);
    // Triangular dither (removes residual banding when quantizing to 8/10 bit)
    if (gPresent2.z > 0.0) {
        uint2 ip = uint2(px);
        float n = hashFloat(ip, gFrame.x) + hashFloat(ip + 7919u, gFrame.x) - 1.0;
        c += n * gPresent2.z;
    }
    return float4(c, 1.0);
}
