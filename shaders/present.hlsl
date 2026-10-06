// Present: blit the final (or interpolated) frame into the swap chain back
// buffer with letterboxing, optional before/after split view and dithering.
#include "common.hlsli"
#include "generated/osd_font.hlsli"

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
    float3 o = saturate(c);
    if (gFrame.z == BGN_OUTPUT_SCRGB) {
        o = (gFrame.y == BGN_INPUT_SCRGB) ? pqDecode(saturate(c)) * (10000.0 / 80.0) : srgbToLinear(saturate(c)) * (gHdr.x / 80.0);
    } else if (gFrame.y == BGN_INPUT_SCRGB) {
        o = linearToSrgb(saturate(pqDecode(saturate(c)) * (10000.0 / 200.0)));
    }
    return o;
}

uint osdChar(uint i) {
    uint4 v = gOsdText[i / 16u];
    uint k = (i % 16u) / 4u;
    uint word = k == 0u ? v.x : (k == 1u ? v.y : (k == 2u ? v.z : v.w));
    return (word >> ((i % 4u) * 8u)) & 0xFFu;
}

// In-game OSD: semi-transparent box with bitmap text (gOsd: origin, pixel scale, enabled;
// gOsd2: columns, rows, background opacity). White = SDR paper white on HDR outputs.
float3 drawOsd(float2 px, float3 c) {
    const float sc = max(gOsd.z, 1.0);
    const float2 o = (px - gOsd.xy) / sc;           // in font pixels
    const float2 box = float2(gOsd2.x * OSD_GLYPH_W + 10.0, gOsd2.y * OSD_GLYPH_H + 8.0);
    if (any(o < 0.0) || any(o >= box)) return c;
    const float white = gFrame.z == BGN_OUTPUT_SCRGB ? gHdr.x / 80.0 : 1.0;
    c = lerp(c, float3(0.02, 0.03, 0.05) * white, gOsd2.z);
    int2 t = int2(floor(o - float2(5.0, 4.0)));
    if (any(t < 0)) return c;
    uint col = uint(t.x) / OSD_GLYPH_W, row = uint(t.y) / OSD_GLYPH_H;
    if (col >= uint(gOsd2.x) || row >= uint(gOsd2.y)) return c;
    uint ch = osdChar(row * uint(gOsd2.x) + col);
    if (ch < 32u || ch > 126u) return c;
    uint gx = uint(t.x) % OSD_GLYPH_W, gy = uint(t.y) % OSD_GLYPH_H;
    uint bits = (kOsdFont[(ch - 32u) * 3u + gy / 4u] >> ((gy % 4u) * 8u)) & 0xFFu;
    if ((bits >> gx) & 1u) c = (row == 0u ? float3(0.42, 0.98, 0.82) : float3(0.95, 0.97, 1.0)) * white;
    return c;
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
    if (gOsd.w > 0.5) c = drawOsd(px, c);
    // Triangular dither (removes residual banding when quantizing to 8/10 bit)
    if (gPresent2.z > 0.0) {
        uint2 ip = uint2(px);
        float n = hashFloat(ip, gFrame.x) + hashFloat(ip + 7919u, gFrame.x) - 1.0;
        c += n * gPresent2.z;
    }
    return float4(c, 1.0);
}
