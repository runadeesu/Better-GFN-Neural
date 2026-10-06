// Neural Super Resolution "Tiny" (NSR-T) x2 - single-pass fused inference for
// low-end and integrated GPUs.
//
// Same network family as nsr.hlsl (luma CNN, residual over Catmull-Rom x2), but
// with 4 channels and one hidden layer (336 parameters). All three layers run in
// one dispatch: a 16x16 tile of low-res pixels plus a 3 pixel halo is processed
// in group shared memory, so no activation textures are written or read. This
// keeps memory traffic close to a plain bicubic upscale, which is what limits
// integrated GPUs.
//
// Replicate padding at the image border matches the trainer: every activation
// slot holds the activation of the clamped position.
// gPassI.xy = low-res size
#include "common.hlsli"
#include "generated/nsr_t_x2.hlsli"

Texture2D<float4> gSrcColor : register(t0);
RWTexture2D<float4> gHr : register(u0);

#define TILE 16
#define R0 (TILE + 6) // luma: tile + 3 px halo
#define R1 (TILE + 4) // layer 0 activations
#define R2 (TILE + 2) // layer 1 activations

groupshared float sLuma[R0 * R0];
groupshared float4 sAct0[R1 * R1];
groupshared float4 sAct1[R2 * R2];

static const float kCrEven[4] = {-0.0234375, 0.2265625, 0.8671875, -0.0703125};
static const float kCrOdd[4] = {-0.0703125, 0.8671875, 0.2265625, -0.0234375};

float4 leaky(float4 x) { return max(x, x * 0.1); }

[numthreads(TILE, TILE, 1)]
void main(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID, uint gi : SV_GroupIndex) {
    const int2 size = int2(gPassI.xy);
    const int2 origin = int2(gid.xy) * TILE; // first output pixel of the tile

    // ---- luma of the tile + 3 px halo (clamped = replicate padding)
    for (uint i = gi; i < R0 * R0; i += TILE * TILE) {
        int2 q = origin - 3 + int2(i % R0, i / R0);
        sLuma[i] = lumaY(gSrcColor.Load(int3(clampCoord(q, size), 0)).rgb);
    }
    GroupMemoryBarrierWithGroupSync();

    // ---- layer 0: conv3x3 1->4 on tile + 2 px
    for (uint j = gi; j < R1 * R1; j += TILE * TILE) {
        int2 q = origin - 2 + int2(j % R1, j / R1);
        int2 l = clampCoord(q, size) - (origin - 3); // position in sLuma
        float4 acc = NSR_T_X2_L0_B[0];
        [unroll] for (int t = 0; t < 9; ++t) {
            int2 o = l + int2(t % 3 - 1, t / 3 - 1);
            acc += sLuma[o.y * R0 + o.x] * NSR_T_X2_L0_W[t];
        }
        sAct0[j] = leaky(acc);
    }
    GroupMemoryBarrierWithGroupSync();

    // ---- layer 1: conv3x3 4->4 on tile + 1 px
    for (uint k = gi; k < R2 * R2; k += TILE * TILE) {
        int2 q = origin - 1 + int2(k % R2, k / R2);
        int2 l = clampCoord(q, size) - (origin - 2); // position in sAct0
        float4 acc = NSR_T_X2_L1_B[0];
        [unroll] for (int t = 0; t < 9; ++t) {
            int2 o = l + int2(t % 3 - 1, t / 3 - 1);
            acc += mul(sAct0[o.y * R1 + o.x], NSR_T_X2_L1_W[t]);
        }
        sAct1[k] = leaky(acc);
    }
    GroupMemoryBarrierWithGroupSync();

    // ---- output layer: conv3x3 4->4 sub-pixel residuals + Catmull-Rom base
    const int2 p = origin + int2(tid.xy);
    if (any(p >= size)) return;
    float4 res = NSR_T_X2_L2_B[0];
    [unroll] for (int t = 0; t < 9; ++t) {
        int2 l = clampCoord(p + int2(t % 3 - 1, t / 3 - 1), size) - (origin - 1); // position in sAct1
        res += mul(sAct1[l.y * R2 + l.x], NSR_T_X2_L2_W[t]);
    }
    float3 rows[5][2];
    [unroll] for (int r = 0; r < 5; ++r) {
        float3 s[5];
        [unroll] for (int c = 0; c < 5; ++c) s[c] = gSrcColor.Load(int3(clampCoord(p + int2(c - 2, r - 2), size), 0)).rgb;
        rows[r][0] = kCrEven[0] * s[0] + kCrEven[1] * s[1] + kCrEven[2] * s[2] + kCrEven[3] * s[3];
        rows[r][1] = kCrOdd[0] * s[1] + kCrOdd[1] * s[2] + kCrOdd[2] * s[3] + kCrOdd[3] * s[4];
    }
    [unroll] for (int dy = 0; dy < 2; ++dy) {
        [unroll] for (int dx = 0; dx < 2; ++dx) {
            float3 base = dy == 0 ? (kCrEven[0] * rows[0][dx] + kCrEven[1] * rows[1][dx] + kCrEven[2] * rows[2][dx] + kCrEven[3] * rows[3][dx])
                                  : (kCrOdd[0] * rows[1][dx] + kCrOdd[1] * rows[2][dx] + kCrOdd[2] * rows[3][dx] + kCrOdd[3] * rows[4][dx]);
            float rr = dy == 0 ? (dx == 0 ? res.x : res.y) : (dx == 0 ? res.z : res.w);
            gHr[p * 2 + int2(dx, dy)] = float4(base + rr, 1.0);
        }
    }
}
