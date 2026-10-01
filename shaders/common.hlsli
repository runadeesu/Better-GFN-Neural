// Better GFN Neural - shared HLSL helpers
#ifndef BGN_COMMON_HLSLI
#define BGN_COMMON_HLSLI

#include "ShaderShared.h"

SamplerState gLinearClamp : register(s0);
SamplerState gPointClamp : register(s1);

// YCoCg luma. This exact definition is what the NSR networks were trained on.
float lumaY(float3 c) { return dot(c, float3(0.25, 0.5, 0.25)); }

float3 rgbToYCoCg(float3 c) {
    return float3(dot(c, float3(0.25, 0.5, 0.25)), dot(c, float3(0.5, 0.0, -0.5)), dot(c, float3(-0.25, 0.5, -0.25)));
}
float3 yCoCgToRgb(float3 v) { return float3(v.x + v.y - v.z, v.x + v.z, v.x - v.y - v.z); }

float lumaRec709(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }

float3 srgbToLinear(float3 c) {
    c = max(c, 0.0);
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}
float3 linearToSrgb(float3 c) {
    c = max(c, 0.0);
    return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}

// SMPTE ST 2084 (PQ). Input/Output in units of 10000 nits.
float3 pqEncode(float3 n) {
    const float m1 = 0.1593017578125, m2 = 78.84375, c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
    float3 p = pow(max(n, 0.0), m1);
    return pow((c1 + c2 * p) / (1.0 + c3 * p), m2);
}
float3 pqDecode(float3 e) {
    const float m1 = 0.1593017578125, m2 = 78.84375, c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
    float3 p = pow(saturate(e), 1.0 / m2);
    return pow(max(p - c1, 0.0) / (c2 - c3 * p), 1.0 / m1);
}

// Working space: SDR input is processed in gamma-encoded sRGB; HDR (scRGB) input
// is converted to PQ so that all filters operate on perceptually uniform values.
float3 inputToWorking(float3 c, uint inputEncoding) {
    if (inputEncoding == BGN_INPUT_SCRGB) return pqEncode(max(c, 0.0) * (80.0 / 10000.0));
    return saturate(c);
}

uint hash32(uint x) {
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
float hashFloat(uint2 p, uint frame) { return float(hash32(p.x * 1973u + p.y * 9277u + frame * 26699u) & 0xFFFFFFu) / 16777216.0; }

bool hasFlag(uint f) { return (gFrame.w & f) != 0u; }

int2 clampCoord(int2 p, int2 size) { return clamp(p, int2(0, 0), size - 1); }

// Skin tone likelihood (YCbCr cluster heuristic). Input: gamma-encoded RGB.
float skinLikelihood(float3 c) {
    float y = dot(c, float3(0.299, 0.587, 0.114));
    float cb = 0.5 + dot(c, float3(-0.168736, -0.331264, 0.5));
    float cr = 0.5 + dot(c, float3(0.5, -0.418688, -0.081312));
    float a = smoothstep(0.03, 0.0, abs(cb - 0.40) - 0.10);  // Cb 77..127 / 255
    float b = smoothstep(0.03, 0.0, abs(cr - 0.60) - 0.08);  // Cr 133..173 / 255
    float l = smoothstep(0.12, 0.25, y) * smoothstep(0.98, 0.85, y);
    return a * b * l;
}

#endif
