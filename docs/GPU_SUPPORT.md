# 対応 GPU 一覧 / GPU support

Better GFN Neural uses only vendor-neutral Direct3D 11 (Feature Level 11_0) compute
and pixel shaders – no CUDA, TensorRT, OpenVINO or vendor SDK. Every GPU family
below runs the same pipeline; differences are the automatic starting tier and the
precision of the neural network shaders.

| Family | Auto Mode start tier | NSR shader precision | Notes |
|---|---|---|---|
| NVIDIA GeForce RTX 50 Series | 5–6 (Quality / Ultra) | FP16 (`min16float`) | |
| NVIDIA GeForce RTX 40 Series | 5–6 (RTX 4070+ → Ultra) | FP16 | |
| NVIDIA GeForce RTX 30 Series | 4–6 (3090 → Ultra, 3070/3080 → Quality) | FP16 | |
| NVIDIA GeForce RTX 20 Series | 4–5 | FP16 | |
| NVIDIA GeForce GTX 16 Series (Turing) | 3 | FP16 | |
| NVIDIA GeForce GTX 10 Series (Pascal) | 2–3 | FP32 (FP16 is slow on Pascal) | |
| Older NVIDIA GTX (Maxwell, Kepler FL11) | 1 | FP32 | |
| AMD Radeon RX 9000 Series | 5–6 | FP16 | |
| AMD Radeon RX 7000 Series | 5–6 | FP16 | |
| AMD Radeon RX 6000 Series | 4–5 | FP16 | |
| AMD Radeon RX 5000 Series | 3–4 | FP16 | |
| AMD Radeon RX 400/500 (Polaris), Vega | 2 | FP16 if the driver exposes 16-bit min precision | |
| AMD Radeon integrated (680M/780M/880M/890M) | 2 | FP16 | |
| Intel Arc B-Series (B570/B580) | 4–5 | FP16 | |
| Intel Arc A-Series (A380 … A770) | 2–5 | FP16 | |
| Intel Arc integrated (Meteor/Lunar/Arrow Lake) | 2 | FP16 | |
| Intel Iris Xe / UHD | 0–1 | FP16 | enhancement still active at the minimal tier |
| Microsoft Basic Render Driver (WARP, no GPU) | 0 | FP32 | used only as a fallback / in CI |

* The start tier is only a first guess (`settings/Presets.cpp::estimateTierForGpu`).
  Auto Mode measures the real GPU time every frame and moves up or down within
  seconds; the in-app **Benchmark** result replaces the guess when available.
* FP16 is used only when the GPU family benefits **and** the driver reports
  `D3D11_SHADER_MIN_PRECISION_16_BIT`; the self-test verifies both the FP16 and
  FP32 network shaders against the CPU reference.
* Laptops with hybrid graphics: the high-performance GPU is selected
  (`DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE`); Windows Graphics Capture handles the
  cross-adapter transfer when the display is driven by the integrated GPU.
* Desktop Duplication (fallback capture) requires the monitor to be driven by
  the processing GPU.

## Tested in this repository's CI

GitHub's Windows runners have no hardware GPU, so CI runs the complete GPU
self-test, benchmark and integration tests on **WARP** (the Direct3D 11
software rasterizer, which implements the same shader model). Hardware-specific
performance numbers therefore come from the in-app benchmark on your PC; see
[TEST_RESULTS.md](TEST_RESULTS.md) for exactly what was verified.
