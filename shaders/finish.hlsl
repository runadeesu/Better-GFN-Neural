// Finish pass at output resolution:
//   1. Adaptive Sharpening (content aware: edge contrast, motion, text/HUD, skin)
//   2. HDR+ / SDR Enhancement color pipeline: auto levels, black/white level,
//      shadow detail, highlight recovery, contrast, gamma, local contrast,
//      saturation, vibrance, color temperature
//   3. Output encoding: gamma SDR, scRGB HDR (with SDR->HDR highlight expansion),
//      or HDR->SDR tone mapping
#include "common.hlsli"

Texture2D<float4> gUp : register(t0);
Texture2D<float4> gFlowTex : register(t1);
Texture2D<float> gBlurLuma : register(t2); // low-res luma (input/8) for local contrast
RWTexture2D<float4> gFinal : register(u0);

float hermite(float p0, float p1, float m0, float m1, float t) {
    float t2 = t * t, t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * p0 + (t3 - 2 * t2 + t) * m0 + (-2 * t3 + 3 * t2) * p1 + (t3 - t2) * m1;
}

float toneCurve(float L, bool hdrInput) {
    const float shadow = gColor2.x, contrast = gColor0.z, gammaV = gColor0.w, highlight = gColor1.w;
    // Shadow detail: lift near-blacks without raising pure black (no washed-out blacks)
    L = L + shadow * 0.55 * L * pow(saturate(1.0 - L), 5.0);
    if (!hdrInput) {
        // Contrast S-curve, monotonic for |k| < 1
        float k = contrast * 0.3;
        L = L - k * sin(6.2831853 * L) / 6.2831853;
        // Highlight recovery: expand gradation just below white (Hermite shoulder)
        const float knee = 0.72;
        if (L > knee && L < 1.0) {
            float t = (L - knee) / (1.0 - knee);
            L = hermite(knee, 1.0, 1.0 - knee, (1.0 - knee) * (1.0 + highlight), t);
        }
    }
    L = pow(saturate(L), 1.0 / gammaV);
    return L;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    int2 p = int2(id.xy);
    int2 size = int2(gOut.xy);
    if (any(p >= size)) return;
    const bool hdrInput = gFrame.y == BGN_INPUT_SCRGB;
    float3 c = gUp.Load(int3(p, 0)).rgb;

    // ---------------- Adaptive sharpening --------------------------------
    float amount = gSharpen.x;
    float motionIn = 0.0;
    float2 inPos = (float2(p) + 0.5) / gSharpen.y; // position in input pixels
    if (hasFlag(BGN_FLAG_FLOW_VALID)) {
        float4 f = gFlowTex.SampleLevel(gLinearClamp, inPos / gFlow.z / gFlow.xy, 0);
        motionIn = length(f.xy);
    }
    if (amount > 0.0) {
        float y = lumaY(c);
        float n0 = lumaY(gUp.Load(int3(clampCoord(p + int2(1, 0), size), 0)).rgb);
        float n1 = lumaY(gUp.Load(int3(clampCoord(p + int2(-1, 0), size), 0)).rgb);
        float n2 = lumaY(gUp.Load(int3(clampCoord(p + int2(0, 1), size), 0)).rgb);
        float n3 = lumaY(gUp.Load(int3(clampCoord(p + int2(0, -1), size), 0)).rgb);
        float d0 = lumaY(gUp.Load(int3(clampCoord(p + int2(1, 1), size), 0)).rgb);
        float d1 = lumaY(gUp.Load(int3(clampCoord(p + int2(-1, -1), size), 0)).rgb);
        float d2 = lumaY(gUp.Load(int3(clampCoord(p + int2(1, -1), size), 0)).rgb);
        float d3 = lumaY(gUp.Load(int3(clampCoord(p + int2(-1, 1), size), 0)).rgb);
        float mn = min(y, min(min(min(n0, n1), min(n2, n3)), min(min(d0, d1), min(d2, d3))));
        float mx = max(y, max(max(max(n0, n1), max(n2, n3)), max(max(d0, d1), max(d2, d3))));
        float blur = (4.0 * y + 2.0 * (n0 + n1 + n2 + n3) + (d0 + d1 + d2 + d3)) / 16.0;
        float contrast = mx - mn;
        if (hasFlag(BGN_FLAG_ADAPTIVE_SHARPEN)) {
            float a0 = amount;
            amount *= 1.0 - 0.65 * smoothstep(0.2, 0.75, contrast);      // strong edges are already crisp
            amount *= 1.0 - 0.6 * smoothstep(4.0, 24.0, motionIn);       // fast motion: avoid over-sharpening
            if (hasFlag(BGN_FLAG_TEXT_BOOST)) {
                // Static, high-contrast micro structure = text / HUD / UI
                float text = (1.0 - smoothstep(0.15, 0.6, motionIn)) * smoothstep(0.22, 0.45, contrast);
                amount = lerp(amount, a0 * (1.0 + gSharpen.z), text);
            }
            if (hasFlag(BGN_FLAG_SKIN_PROTECT)) amount *= 1.0 - gSharpen.w * skinLikelihood(saturate(c));
        }
        float ys = y + amount * 1.6 * (y - blur);
        ys = clamp(ys, mn - 0.012, mx + 0.012);
        c += ys - y;
    }

    // ---------------- Color / HDR+ ---------------------------------------
    if (gColor2.w > 0.5) {
        float3 x = c;
        // Levels (auto + manual)
        float black = gAuto.x * gAuto.z + gColor0.x * 0.08;
        float white = 1.0 - (1.0 - gAuto.y) * gAuto.z - gColor0.y * 0.08;
        if (hdrInput) { black = gColor0.x * 0.03; white = 1.0; }
        x = (x - black) / max(white - black, 0.5);

        float L0 = max(lumaRec709(x), 1e-4);
        float L1 = toneCurve(saturate(L0), hdrInput);
        // Local contrast (clarity) with halo control
        if (gColor2.y > 0.0) {
            float lb = gBlurLuma.SampleLevel(gLinearClamp, inPos * gIn.zw, 0);
            float detail = L1 - lb;
            L1 += gColor2.y * 0.6 * detail * (1.0 - smoothstep(0.12, 0.45, abs(detail)));
        }
        x = x * (max(L1, 0.0) / L0);
        if (L0 >= 1.0) x = c; // keep fully clipped pixels untouched

        // Saturation + vibrance (skin-aware)
        float Lr = lumaRec709(x);
        float sat = max(x.r, max(x.g, x.b)) - min(x.r, min(x.g, x.b));
        float vib = gColor1.y * pow(saturate(1.0 - sat * 1.4), 2.0) * (1.0 - 0.6 * skinLikelihood(saturate(x)));
        float gain = (1.0 + gColor1.x * 0.5) * (1.0 + vib * 0.55);
        x = Lr + (x - Lr) * gain;

        // Color temperature (in linear light, luminance preserving)
        if (abs(gColor1.z) > 1e-3) {
            float3 lin = srgbToLinear(saturate(x));
            float t = gColor1.z;
            float3 wb = float3(1.0 + 0.09 * t, 1.0 + 0.01 * t, 1.0 - 0.11 * t);
            float3 lin2 = lin * wb;
            lin2 *= lumaRec709(lin) / max(lumaRec709(lin2), 1e-5);
            x = linearToSrgb(lin2);
        }
        // Soft gamut/clip protection
        x = max(x, 0.0);
        float mxc = max(x.r, max(x.g, x.b));
        if (mxc > 1.0 && !hdrInput) x = lerp(x, x / mxc, 0.5);
        c = x;
    }

    // ---------------- Visual style & accessibility -----------------------
    // Split toning and monochrome work on perceptual values; color vision
    // correction (daltonization) and the night light filter in linear light.
    const float mono = gStyle.x, split = gStyle.y, night = gStyle.z;
    const uint cvd = uint(gStyle.w + 0.5);
    const bool cvdOn = cvd > 0u && gStyle2.x > 0.0;
    if (split > 0.0) {
        float Lp = saturate(lumaRec709(c));
        c += split * ((1.0 - Lp) * (1.0 - Lp) * float3(-0.025, 0.005, 0.035) + Lp * Lp * float3(0.035, 0.012, -0.03));
    }
    if (mono > 0.0) c = lerp(c, lumaRec709(c).xxx, mono);
    if (night > 0.0 || cvdOn) {
        float3 lin = hdrInput ? pqDecode(saturate(c)) : srgbToLinear(saturate(c));
        if (cvdOn) {
            // Machado et al. 2009 simulation matrices (severity 1.0), linear RGB
            float3x3 sim = float3x3(0.152286, 1.052583, -0.204868, 0.114503, 0.786281, 0.099216, -0.003882, -0.048116, 1.051998);
            if (cvd == 2u) sim = float3x3(0.367322, 0.860646, -0.227968, 0.280085, 0.672501, 0.047413, -0.011820, 0.042940, 0.968881);
            if (cvd == 3u) sim = float3x3(1.255528, -0.076749, -0.178779, -0.078411, 0.930809, 0.147602, 0.004733, 0.691367, 0.303900);
            float3 err = lin - mul(sim, lin);
            // Fidaner daltonization: move the information the viewer cannot see
            // into channels they can distinguish.
            float3 corr = cvd == 3u ? float3(err.r + 0.7 * err.b, err.g + 0.7 * err.b, 0.0) : float3(0.0, 0.7 * err.r + err.g, 0.7 * err.r + err.b);
            lin = max(lin + gStyle2.x * corr, 0.0);
        }
        if (night > 0.0) lin *= float3(1.0, 1.0 - 0.18 * night, 1.0 - 0.6 * night);
        c = hdrInput ? pqEncode(lin) : linearToSrgb(lin);
    }

    // ---------------- Output encoding ------------------------------------
    float3 o;
    const float paper = gHdr.x, peak = gHdr.y;
    if (gFrame.z == BGN_OUTPUT_SCRGB) {
        if (hdrInput) {
            o = pqDecode(saturate(c)) * (10000.0 / 80.0);
        } else {
            float3 lin = srgbToLinear(saturate(c));
            float3 nits = lin * paper;
            if (gHdr.w > 0.5 && peak > paper) {
                // HDR+: expand bright SDR highlights toward the display peak (monotonic)
                float L = lumaRec709(lin);
                float e = smoothstep(0.55, 1.0, L);
                float boost = 1.0 + gHdr.z * (peak / paper - 1.0) * e * e * e;
                nits = min(nits * boost, peak);
            }
            o = nits / 80.0;
        }
    } else {
        if (hdrInput) {
            float3 nits = pqDecode(saturate(c)) * 10000.0;
            float3 v = nits / 200.0;
            if (gColor2.z > 0.5) {
                float W = max(peak / 200.0, 1.5);
                v = v * (1.0 + v / (W * W)) / (1.0 + v);
            }
            o = linearToSrgb(saturate(v));
        } else {
            o = saturate(c);
        }
    }
    gFinal[p] = float4(o, 1.0);
}
