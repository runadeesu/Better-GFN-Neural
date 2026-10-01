// Synthetic "cloud game" test scene for the benchmark and self-test:
// panning gradient sky + scrolling fence lines + moving discs + static HUD
// glyph blocks + 8x8 block quantization artifacts. Deterministic per frame.
// gPassI.xy = size, gPassF.x = time (s), gPassF.y = pan speed (px/s)
#include "common.hlsli"

RWTexture2D<float4> gOutTex : register(u0);

float glyph(float2 q, uint seed) {
    // 5x7 pseudo glyph from hash bits
    int2 c = int2(floor(q));
    if (c.x < 0 || c.x >= 5 || c.y < 0 || c.y >= 7) return 0;
    return (hash32(seed * 131u + uint(c.y) * 5u + uint(c.x)) & 1u) ? 1.0 : 0.0;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    int2 p = int2(id.xy);
    int2 size = int2(gPassI.xy);
    if (any(p >= size)) return;
    float t = gPassF.x;
    float2 uv = (float2(p) + 0.5) / float2(size);
    float pan = t * gPassF.y;
    float2 world = float2(p) + float2(pan, 0);

    float3 sky = lerp(float3(0.20, 0.35, 0.65), float3(0.85, 0.75, 0.55), uv.y);
    float3 c = sky;
    // Ground with texture noise
    if (uv.y > 0.6) {
        float n = hashFloat(uint2(world * 0.5), 0);
        c = lerp(float3(0.18, 0.32, 0.10), float3(0.30, 0.45, 0.15), n);
    }
    // Fence: thin vertical lines scrolling with the camera
    float fx = frac(world.x / 23.0);
    float fence = smoothstep(0.06, 0.0, abs(fx - 0.5) - 0.02);
    if (uv.y > 0.45 && uv.y < 0.75) c = lerp(c, float3(0.55, 0.52, 0.48), fence);
    // Wires
    float wire = smoothstep(1.2, 0.0, abs(float(p.y) - (0.3 * size.y + 6.0 * sin(world.x * 0.01))));
    c = lerp(c, float3(0.1, 0.1, 0.1), wire);
    // Moving discs
    [unroll] for (int k = 0; k < 3; ++k) {
        float2 center = float2(frac(0.2 + 0.13 * k + t * (0.05 + 0.03 * k)) * size.x, (0.4 + 0.15 * k) * size.y);
        float d = length(float2(p) - center) - size.y * 0.05;
        c = lerp(c, float3(0.9 - 0.3 * k, 0.3 + 0.2 * k, 0.2), saturate(0.5 - d));
    }
    // Static HUD text blocks (top-left)
    float2 hud = float2(p) - float2(24, 24);
    if (hud.x >= 0 && hud.y >= 0 && hud.x < 360 && hud.y < 28) {
        uint ch = uint(hud.x / 12.0);
        float g = glyph(float2(fmod(hud.x, 12.0) / 2.0, hud.y / 3.5), ch + 7u);
        c = lerp(c * 0.35, float3(1, 1, 1), g);
    }
    // Simulated 8x8 block quantization (cloud stream artifacts)
    int2 blk = p / 8;
    float q = 1.0 / 48.0;
    float3 blockBias = (float3(hashFloat(uint2(blk), uint(t * 60.0)), hashFloat(uint2(blk) + 3u, 1), hashFloat(uint2(blk) + 9u, 2)) - 0.5) * q;
    c = floor(saturate(c + blockBias) * 255.0 + 0.5) / 255.0;
    gOutTex[p] = float4(c, 1.0);
}
