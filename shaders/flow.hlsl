// Optical flow: coarse-to-fine block matching on the luma pyramid.
// Produces "backward" flow per cell: prev(p + v) ~= cur(p), v in INPUT pixels.
// Output texel: xy = flow (input px), z = mean abs difference of the best match,
//               w = local luma standard deviation (texture measure for confidence).
//
// gPassI.xy  = flow grid size at this level (cells of 4x4 level pixels)
// gPassI.zw  = level image size
// gPassF.x   = input pixels per level pixel (2^level)
// gPassF.yz  = coarser flow grid size
// gPassF.w   = 1 if this is the coarsest level (exhaustive search)
// gPassF2.x  = 1 if this is the final level (sub-pixel refinement)
// gPassF2.y  = 1 if the previous frame's flow (t3) is valid
// gPassF2.zw = previous flow grid size
#include "common.hlsli"

Texture2D<float> gCurY : register(t0);
Texture2D<float> gPrevY : register(t1);
Texture2D<float4> gCoarse : register(t2);
Texture2D<float4> gPrevFlow : register(t3);
RWTexture2D<float4> gFlowOut : register(u0);

#define WIN 6
#define SEARCH_R 6

static float sWin[WIN * WIN];

float costAt(int2 base, int2 d, int2 size) {
    float c = 0;
    [unroll] for (int j = 0; j < WIN; ++j) {
        [unroll] for (int i = 0; i < WIN; ++i) {
            float pv = gPrevY.Load(int3(clampCoord(base + int2(i, j) + d, size), 0));
            c += abs(sWin[j * WIN + i] - pv);
        }
    }
    return c * (1.0 / (WIN * WIN));
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    int2 cell = int2(id.xy);
    int2 grid = int2(gPassI.xy);
    if (any(cell >= grid)) return;
    int2 size = int2(gPassI.zw);
    float scale = gPassF.x; // input px per level px
    int2 base = cell * 4 - 1;

    float mean = 0, mean2 = 0;
    [unroll] for (int j = 0; j < WIN; ++j) {
        [unroll] for (int i = 0; i < WIN; ++i) {
            float v = gCurY.Load(int3(clampCoord(base + int2(i, j), size), 0));
            sWin[j * WIN + i] = v;
            mean += v;
            mean2 += v * v;
        }
    }
    mean /= WIN * WIN;
    float stdev = sqrt(max(mean2 / (WIN * WIN) - mean * mean, 0.0));

    int2 best = int2(0, 0);
    float bestCost = 1e9;

    if (gPassF.w > 0.5) {
        // Coarsest level: exhaustive search, slight bias towards zero motion.
        [loop] for (int dy = -SEARCH_R; dy <= SEARCH_R; ++dy) {
            [loop] for (int dx = -SEARCH_R; dx <= SEARCH_R; ++dx) {
                float c = costAt(base, int2(dx, dy), size) + 0.0008 * (abs(dx) + abs(dy));
                if (c < bestCost) { bestCost = c; best = int2(dx, dy); }
            }
        }
    } else {
        // Candidates: coarse vector (3 nearest coarse cells), zero, previous frame's flow.
        int2 cgrid = int2(gPassF.yz);
        int2 cc = cell / 2;
        int2 side = int2((cell.x & 1) ? 1 : -1, (cell.y & 1) ? 1 : -1);
        float2 cand[5];
        cand[0] = gCoarse.Load(int3(clampCoord(cc, cgrid), 0)).xy;
        cand[1] = gCoarse.Load(int3(clampCoord(cc + int2(side.x, 0), cgrid), 0)).xy;
        cand[2] = gCoarse.Load(int3(clampCoord(cc + int2(0, side.y), cgrid), 0)).xy;
        cand[3] = float2(0, 0);
        cand[4] = cand[0];
        if (gPassF2.y > 0.5) {
            int2 pgrid = int2(gPassF2.zw);
            float2 rel = (float2(cell) + 0.5) / float2(grid);
            cand[4] = gPrevFlow.Load(int3(clampCoord(int2(rel * float2(pgrid)), pgrid), 0)).xy;
        }
        int2 pred = int2(round(cand[0] / scale));
        [loop] for (int k = 0; k < 5; ++k) {
            int2 c0 = int2(round(cand[k] / scale));
            [unroll] for (int dy = -1; dy <= 1; ++dy) {
                [unroll] for (int dx = -1; dx <= 1; ++dx) {
                    int2 d = c0 + int2(dx, dy);
                    float c = costAt(base, d, size) + 0.0015 * (abs(d.x - pred.x) + abs(d.y - pred.y)) / max(scale * 0.25, 1.0);
                    if (c < bestCost) { bestCost = c; best = d; }
                }
            }
        }
    }

    float2 flow = float2(best);
    float rawCost = costAt(base, best, size);
    if (gPassF2.x > 0.5) {
        // Parabolic sub-pixel refinement
        float cx0 = costAt(base, best - int2(1, 0), size), cx1 = costAt(base, best + int2(1, 0), size);
        float cy0 = costAt(base, best - int2(0, 1), size), cy1 = costAt(base, best + int2(0, 1), size);
        float denx = cx0 - 2.0 * rawCost + cx1, deny = cy0 - 2.0 * rawCost + cy1;
        if (denx > 1e-5) flow.x += clamp((cx0 - cx1) / (2.0 * denx), -0.5, 0.5);
        if (deny > 1e-5) flow.y += clamp((cy0 - cy1) / (2.0 * deny), -0.5, 0.5);
    }
    gFlowOut[cell] = float4(flow * scale, rawCost, stdev);
}
