// Neural Super Resolution (NSR) x2 - real-time CNN inference in compute shaders.
//
// Network (trained by tools/nsr_trainer, weights in generated/*.hlsli):
//   L0: conv3x3 1->C  (input: YCoCg luma of the low-res frame), leaky ReLU
//   L1..LD: conv3x3 C->C, leaky ReLU
//   L(D+1): conv3x3 C->4 => 4 sub-pixel luma residuals (depth-to-space x2)
//   HR = CatmullRom_x2(LR) + residual (added equally to R, G, B)
// Activations are stored as C/4 RGBA16F textures. Replicate padding (clamped loads)
// matches the trainer. Compile-time variants:
//   NSR_MODEL_L (16 ch, 3 hidden) or default S (8 ch, 2 hidden)
//   NSR_STAGE_FIRST / NSR_STAGE_HIDDEN / NSR_STAGE_LAST, NSR_LAYER = layer index
//   NSR_HALF => min16float math on GPUs with fast FP16
// gPassI.xy = low-res size
#include "common.hlsli"

#ifdef NSR_MODEL_L
#include "generated/nsr_l_x2.hlsli"
#define NSR_PREFIX NSR_L_X2
#define NSR_C 16
#else
#include "generated/nsr_s_x2.hlsli"
#define NSR_PREFIX NSR_S_X2
#define NSR_C 8
#endif
#define NSR_G (NSR_C / 4)

#define NSR_NAME2(p, l, s) p##_L##l##_##s
#define NSR_NAME(p, l, s) NSR_NAME2(p, l, s)
#define NSR_W NSR_NAME(NSR_PREFIX, NSR_LAYER, W)
#define NSR_B NSR_NAME(NSR_PREFIX, NSR_LAYER, B)

#ifdef NSR_HALF
#define nfloat min16float
#define nfloat4 min16float4
#define nfloat4x4 min16float4x4
#else
#define nfloat float
#define nfloat4 float4
#define nfloat4x4 float4x4
#endif

nfloat4 leaky(nfloat4 x) { return max(x, x * (nfloat)0.1); }

// ---------------------------------------------------------------------------
#if defined(NSR_STAGE_FIRST)
Texture2D<float4> gSrcColor : register(t0);
RWTexture2D<float4> gAct0 : register(u0);
RWTexture2D<float4> gAct1 : register(u1);
#if NSR_G > 2
RWTexture2D<float4> gAct2 : register(u2);
RWTexture2D<float4> gAct3 : register(u3);
#endif

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    int2 p = int2(id.xy);
    int2 size = int2(gPassI.xy);
    if (any(p >= size)) return;
    nfloat4 acc[NSR_G];
    [unroll] for (int g = 0; g < NSR_G; ++g) acc[g] = (nfloat4)NSR_B[g];
    [unroll] for (int ky = 0; ky < 3; ++ky) {
        [unroll] for (int kx = 0; kx < 3; ++kx) {
            nfloat y = (nfloat)lumaY(gSrcColor.Load(int3(clampCoord(p + int2(kx - 1, ky - 1), size), 0)).rgb);
            [unroll] for (int g2 = 0; g2 < NSR_G; ++g2) acc[g2] += y * (nfloat4)NSR_W[g2 * 9 + ky * 3 + kx];
        }
    }
    gAct0[p] = leaky(acc[0]);
    gAct1[p] = leaky(acc[1]);
#if NSR_G > 2
    gAct2[p] = leaky(acc[2]);
    gAct3[p] = leaky(acc[3]);
#endif
}

// ---------------------------------------------------------------------------
#elif defined(NSR_STAGE_HIDDEN) || defined(NSR_STAGE_LAST)
Texture2D<float4> gIn0 : register(t0);
Texture2D<float4> gIn1 : register(t1);
#if NSR_G > 2
Texture2D<float4> gIn2 : register(t2);
Texture2D<float4> gIn3 : register(t3);
#endif

nfloat4 loadAct(int k, int2 q) {
#if NSR_G > 2
    if (k == 0) return (nfloat4)gIn0.Load(int3(q, 0));
    if (k == 1) return (nfloat4)gIn1.Load(int3(q, 0));
    if (k == 2) return (nfloat4)gIn2.Load(int3(q, 0));
    return (nfloat4)gIn3.Load(int3(q, 0));
#else
    if (k == 0) return (nfloat4)gIn0.Load(int3(q, 0));
    return (nfloat4)gIn1.Load(int3(q, 0));
#endif
}

#if defined(NSR_STAGE_HIDDEN)
#define OUT_G NSR_G
RWTexture2D<float4> gAct0 : register(u0);
RWTexture2D<float4> gAct1 : register(u1);
#if NSR_G > 2
RWTexture2D<float4> gAct2 : register(u2);
RWTexture2D<float4> gAct3 : register(u3);
#endif
#else
#define OUT_G 1
Texture2D<float4> gSrcColor : register(t4);
RWTexture2D<float4> gHr : register(u0);
static const float kCrEven[4] = {-0.0234375, 0.2265625, 0.8671875, -0.0703125};
static const float kCrOdd[4] = {-0.0703125, 0.8671875, 0.2265625, -0.0234375};
#endif

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    int2 p = int2(id.xy);
    int2 size = int2(gPassI.xy);
    if (any(p >= size)) return;
    nfloat4 acc[OUT_G];
    [unroll] for (int g = 0; g < OUT_G; ++g) acc[g] = (nfloat4)NSR_B[g];
    [unroll] for (int t = 0; t < 9; ++t) {
        int2 q = clampCoord(p + int2(int(uint(t) % 3u) - 1, int(uint(t) / 3u) - 1), size);
        [unroll] for (int k = 0; k < NSR_G; ++k) {
            nfloat4 v = loadAct(k, q);
            [unroll] for (int g2 = 0; g2 < OUT_G; ++g2) acc[g2] += mul(v, (nfloat4x4)NSR_W[(g2 * 9 + t) * NSR_G + k]);
        }
    }
#if defined(NSR_STAGE_HIDDEN)
    gAct0[p] = leaky(acc[0]);
    gAct1[p] = leaky(acc[1]);
#if NSR_G > 2
    gAct2[p] = leaky(acc[2]);
    gAct3[p] = leaky(acc[3]);
#endif
#else
    // Catmull-Rom x2 base for the 2x2 output block (5x5 low-res neighbourhood)
    float3 rows[5][2]; // horizontally filtered rows for even/odd output columns
    [unroll] for (int r = 0; r < 5; ++r) {
        float3 s[5];
        [unroll] for (int i = 0; i < 5; ++i) s[i] = gSrcColor.Load(int3(clampCoord(p + int2(i - 2, r - 2), size), 0)).rgb;
        rows[r][0] = kCrEven[0] * s[0] + kCrEven[1] * s[1] + kCrEven[2] * s[2] + kCrEven[3] * s[3];
        rows[r][1] = kCrOdd[0] * s[1] + kCrOdd[1] * s[2] + kCrOdd[2] * s[3] + kCrOdd[3] * s[4];
    }
    float4 res = (float4)acc[0];
    [unroll] for (int dy = 0; dy < 2; ++dy) {
        [unroll] for (int dx = 0; dx < 2; ++dx) {
            float3 base = dy == 0 ? (kCrEven[0] * rows[0][dx] + kCrEven[1] * rows[1][dx] + kCrEven[2] * rows[2][dx] + kCrEven[3] * rows[3][dx])
                                  : (kCrOdd[0] * rows[1][dx] + kCrOdd[1] * rows[2][dx] + kCrOdd[2] * rows[3][dx] + kCrOdd[3] * rows[4][dx]);
            float r = dy == 0 ? (dx == 0 ? res.x : res.y) : (dx == 0 ? res.z : res.w);
            gHr[p * 2 + int2(dx, dy)] = float4(base + r, 1.0);
        }
    }
#endif
}
#endif
