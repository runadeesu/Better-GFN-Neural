# Model card – Neural Super Resolution (NSR) x2

Better GFN Neural's "Neural Super Resolution" is a real convolutional neural
network trained for this project (`tools/nsr_trainer/nsr_trainer.cpp`) and
executed in Direct3D 11 compute shaders (`shaders/nsr.hlsl`). It is **not**
NVIDIA DLSS and does not use game motion vectors or depth (GeForce NOW does not
expose them to local applications).

## Files

| File | Content |
|---|---|
| `nsr_t_x2.json` | NSR-T (Tiny) weights + metadata + validation metrics |
| `nsr_s_x2.json` | NSR-S weights + metadata + validation metrics |
| `nsr_l_x2.json` | NSR-L weights + metadata + validation metrics |
| `training_log_nsr_*.txt` | full training logs |
| `../shaders/generated/nsr_*.hlsli` | the same weights as HLSL constant tables (compiled into the EXE) |
| `../src/neural/generated/nsr_*.inc` | the same weights for the CPU reference (`--selftest` compares GPU vs CPU) |

## Architecture

* Input: YCoCg luma `Y = R/4 + G/2 + B/4` of the low-resolution, gamma-encoded frame
* `conv3x3(1→C) → LeakyReLU(0.1) → [conv3x3(C→C) → LeakyReLU] × D → conv3x3(C→4)`
* The 4 outputs are sub-pixel luma residuals (depth-to-space ×2) added to a
  Catmull-Rom ×2 upsample; chroma uses Catmull-Rom only
* Replicate padding (clamped texture loads)

| | C | D | Parameters | MAC per low-res pixel |
|---|---|---|---|---|
| NSR-T | 4 | 1 | 336 | ≈0.3 k |
| NSR-S | 8 | 2 | 1,540 | ≈1.5 k |
| NSR-L | 16 | 3 | 7,700 | ≈7.6 k |

NSR-T is meant for integrated and older GPUs (quality tiers 1-2). It runs all
three layers in one compute pass (`shaders/nsr_tiny.hlsl`): a 16×16 tile plus a
3 px halo is processed in group-shared memory, so no activation textures are
written or read and the memory traffic stays close to a bicubic upscale.

## Training data

* 21 photographs of the Kodak Lossless True Color Image Suite (3 held out for validation)
* 44 procedurally generated "game-like" scenes (+4 held out): HUD/UI text in Latin
  and Japanese scripts with outlines/shadows, thin lines and chain-link fences,
  grids, foliage strokes, flat shapes, gradients, dark scenes
* Augmentation: random crops, flips, transposition, gain

## Degradation model ("cloud stream")

Random 2× downsampling kernel (box, Mitchell, Catmull-Rom, Lanczos-2, Gaussian,
or aliased point sampling), optional blur (σ 0.2–0.7), optional noise, 8-bit
quantization, then JPEG round trip at quality 30–95 with 4:2:0 chroma (75 % of
samples) as a proxy for H.264/HEVC/AV1 block artifacts.

## Training

Charbonnier loss on Y, Adam (lr 2e-3, 500-step warm-up, cosine decay), batch 16,
48×48 low-res patches. NSR-S: 60 000 iterations (~13 min), NSR-L: 50 000 iterations
(~40 min) on a 4-core CPU. Seed 20261001.

## Validation (PSNR on Y, 8 px border excluded)

| | Clean 2× (Mitchell) | 2× + JPEG q60 |
|---|---|---|
| Bilinear | 29.96 dB | 28.88 dB |
| Catmull-Rom | 30.92 dB | 29.19 dB |
| **NSR-T** | **31.38 dB** | **29.43 dB** |
| **NSR-S** | **32.03 dB** | **29.72 dB** |
| **NSR-L** | **32.70 dB** | **30.12 dB** |

## Limitations

* Trained ×2 only; other ratios are produced by NSR ×2 followed by a
  Lanczos resample (down or up).
* Small networks (real-time budget on GTX-class GPUs); gains are moderate and
  largest on edges, text and fine structure.
* Luma-only network; chroma is interpolated.
* The training set is small; unusual art styles may benefit less.

## Reproducing

```
g++ -std=c++17 -O3 -march=native -ffast-math -pthread -Ithird_party/stb tools/nsr_trainer/nsr_trainer.cpp -o nsr_trainer
./nsr_trainer --data <kodak png dir> --font DejaVuSans.ttf --font <japanese font> \
    --name nsr_s_x2 --channels 8 --hidden 2 --iters 60000 --out-root .
./nsr_trainer ... --name nsr_l_x2 --channels 16 --hidden 3 --iters 50000 --out-root .
./nsr_trainer ... --name nsr_t_x2 --channels 4 --hidden 1 --iters 60000 --out-root .
```
