# Changelog

## 1.1.0 — 2026-10-06

Focus: **low-spec PCs** — realistic AI correction with very low latency and
smooth, natural motion.

### Added — low-spec / latency / smoothness
- **NSR-T**: a new 336-parameter neural upscaler that runs as one fused compute
  pass (groupshared tiles), so integrated GPUs (Intel UHD / Iris Xe, AMD Vega /
  RDNA iGPUs) and older GTX cards get AI reconstruction instead of plain
  Lanczos. Auto Mode tiers 1–2 and the Performance upscale mode now use it.
- **Ghost-free temporal at tier 2**: low tiers now use motion-compensated
  (optical-flow) temporal reconstruction instead of the no-flow fallback, which
  removes trailing ghosts in motion on weak GPUs.
- **Stutter smoothing**: when a stream frame arrives late (> 1.6 frame
  intervals), the last frame is extrapolated half a frame along its motion so
  the picture keeps moving instead of freezing. Real frames are never delayed,
  so it adds no latency.
- GPU tier estimates for integrated graphics tuned so first launch starts at a
  tier that holds the frame rate.

### Added — features
- Visual styles (Natural / Vivid / Cinematic / Competitive / Monochrome).
- Accessibility: color-vision support (protan / deutan / tritan daltonization)
  and night light (blue-light reduction).
- Stream quality monitor (blockiness + stutter → 0–100 score) with adaptive
  compression cleanup.
- Screenshots of the enhanced picture (optional side-by-side before/after),
  tray command and `--screenshot-at`.
- Session history page with per-session summaries and CSV export.
- In-game OSD (FPS, GPU time, added latency, quality, tier, upscaler) drawn by
  the present shader, with position and size options.
- My presets (save / apply / delete enhancement settings), settings export /
  import, diagnostics report for support.
- Battery-aware power mode: caps the tier on battery power.

## 1.0.0 — 2026-10-06

First public release of Better GFN Neural, an unofficial GeForce NOW companion
that enhances the GeForce NOW picture locally on the GPU.

### Added
- Fully automatic flow: GeForce NOW detection, game-window detection, capture,
  GPU processing and output without hotkeys; "Launch GeForce NOW" button.
- Zero-copy Direct3D 11 pipeline: Windows Graphics Capture (DXGI Desktop
  Duplication fallback) → GPU processing → DirectComposition flip-model overlay.
- Neural Super Resolution: two CNNs trained for this project (NSR-S 1,540 /
  NSR-L 7,700 parameters) running in HLSL compute shaders, FP16 where beneficial;
  modes Auto / Quality / Balanced / Performance / Native; automatic output
  resolution; automatic stream-resolution detection for in-place reconstruction.
- Temporal reconstruction (anti-flicker, stabilization, edge stability, ghosting
  reduction) driven by GPU optical flow.
- Stream compression cleanup: deblocking/macroblock, banding, mosquito and
  compression noise, dark-scene cleanup, output dithering.
- Adaptive + motion deblur, content-adaptive sharpening (motion, text/HUD, skin).
- Optical-flow frame interpolation (Off / Auto / 2x) with HUD protection and
  refresh-rate-aware pacing.
- Low Latency Mode, HDR+ (SDR→HDR highlight expansion, HDR passthrough) and SDR
  Enhancement color pipeline with Auto color.
- AUTO MODE controller with 7 quality tiers, presets Auto / Ultra / Quality /
  Balanced / Performance / Low Latency, VRAM/latency/GPU-load governor.
- Game profiles with automatic recognition and built-in templates for 10 titles.
- Modern custom UI in English, 日本語 or Japanese + English (bilingual labels),
  switchable from the title bar; first-run check, statistics page with live
  graphs, in-app benchmark with recommendations, system tray states, Start with
  Windows, controller detection, logs with privacy redaction.
- Robustness: device-loss recovery, capture reconnection, display/sleep handling,
  crash minidumps, safe mode, crash-safe settings with backup.
- Headless `--selftest` and `--benchmark`, integration tests with a scripted fake
  GeForce NOW window, CI packaging of EXE, installer, portable ZIP and
  SHA-256 checksums; draft GitHub Release on version tags.
