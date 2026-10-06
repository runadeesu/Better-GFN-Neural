# Changelog

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
- Modern custom UI (English / 日本語), first-run check, statistics page with live
  graphs, in-app benchmark with recommendations, system tray states, Start with
  Windows, controller detection, logs with privacy redaction.
- Robustness: device-loss recovery, capture reconnection, display/sleep handling,
  crash minidumps, safe mode, crash-safe settings with backup.
- Headless `--selftest` and `--benchmark`, integration tests with a scripted fake
  GeForce NOW window, CI packaging of EXE, installer, portable ZIP and
  SHA-256 checksums; draft GitHub Release on version tags.
