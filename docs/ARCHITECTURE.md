# Architecture

Better GFN Neural is a native C++20 / Direct3D 11 application. Two threads do
all the work:

| Thread | Owns | Never blocks on |
|---|---|---|
| **UI / app thread** (`app/Application`, `ui/UiApp`) | settings, GeForce NOW detection, game profiles, tray, main window (own small D3D11 device), benchmark worker | the engine |
| **Engine thread** (`engine/Engine`, MMCSS "Games" priority) | processing D3D11 device, capture session, GPU pipeline, Auto Mode, overlay window + swap chain | the UI |

They exchange only immutable snapshots: `EngineConfig` (UI → engine, versioned
by `revision`) and `EngineStats` (engine → UI, ~4 Hz). A frozen UI therefore
cannot stall the video path and vice versa.

## Source layout

```
src/
  app/        entry point, Application orchestrator, command line
  automode/   AutoModeController (portable, unit tested)
  benchmark/  in-app benchmark, headless GPU self-test, GPU test helpers
  capture/    Windows.Graphics.Capture (primary), DXGI Desktop Duplication (fallback)
  core/       logging (rotation + privacy redaction), strings, version
  engine/     real-time engine thread
  filters/    Pipeline (processing graph), content-resolution analysis
  framegen/   optical flow, frame interpolation, frame pacing
  gfn/        GeForce NOW window classifier (portable) + Win32 detector/launcher
  neural/     NSR GPU upscaler + CPU reference + generated weights
  platform/   displays/HDR/refresh, startup registry, tray, controllers,
              system monitor (PDH), crash handler, cursor control, paths
  profiles/   game title parsing, per-game profiles + built-in templates
  renderer/   device creation, shader library, GPU timers, resources, overlay presenter
  settings/   settings model + JSON, crash-safe store, presets/tiers
  telemetry/  rolling statistics
  ui/         ImGui-based UI: theme, widgets, pages, i18n (EN/JA)
shaders/      HLSL compute/pixel shaders (+ generated NSR weight tables)
models/       trained NSR weights (JSON) + training logs
tools/        nsr_trainer (C++ CNN trainer), make_icon.py
tests/        unit tests (portable), FakeGfn + PowerShell integration tests
installer/    NSIS script
packaging/    release packaging script
```

## Frame flow (zero CPU copies)

```
GeForce NOW window
   │  Windows.Graphics.Capture (frame pool on our D3D11 device, free-threaded)
   ▼
capture texture ──CopySubresourceRegion (GPU)──► client-area copy
   │ ingest.hlsl            crop + convert to working space (sRGB gamma, or PQ for HDR scRGB input)
   │ [resample down]        only when reconstructing from a detected lower stream resolution
   ▼
cur (RGBA16F) ──► luma pyramid 1/2…1/32 (luma_down.hlsl)
   │                 └─► optical flow: coarse-to-fine block matching (flow.hlsl) + vector median (flow_smooth.hlsl)
   │                 └─► scene histogram (stats.hlsl) → auto levels (async readback)
   │                 └─► content resolution bands (content_res.hlsl) → stream-resolution detector
   ▼
cleanup.hlsl       deblock / deband / mosquito + compression noise / dark-scene cleanup
temporal.hlsl      flow-reprojected history, YCoCg variance clipping, anti-flicker, ghost rejection
deblur.hlsl        adaptive deblur (edge/detail bands) + motion deblur along the flow
upscale            NSR CNN x2 (nsr.hlsl, S or L model) [+ resample to exact size] or Lanczos-AR
finish.hlsl        adaptive sharpening + HDR+/SDR color pipeline + output encoding
[interp.hlsl]      optical-flow frame interpolation (t = 0.5) between the last two outputs
present.hlsl       letterbox, before/after split, dither → swap-chain back buffer
   ▼
Overlay window (WS_EX_LAYERED | TRANSPARENT | NOACTIVATE | TOPMOST, DirectComposition flip model)
```

Every stage is timed with D3D11 timestamp queries (`GpuTimer`), read back
without stalls a few frames later.

## Quality tiers and Auto Mode

`settings/Presets.cpp` maps a tier (0 Minimal … 6 Ultra) to concrete stage
settings. Presets bound the tiers (e.g. Performance ≤ 2, Low Latency ≤ 3 and no
interpolation). `AutoModeController`:

* budget = share of the input frame interval (Auto 50 %, Ultra 65 %, Low Latency 30 %)
* p95 GPU time over budget for ≥1 s → pause interpolation first, then −1 tier;
  > 1.6× budget → −2 tiers immediately
* VRAM > 92 % of the OS budget → step down
* sustained headroom (predicted next-tier cost < 85 % budget, GPU load < 90 %)
  for 4 s (12 s if that tier recently failed) → +1 tier
* interpolation (Auto) only when `refresh ≥ 1.8 × input fps`, input ≤ 125 fps,
  hold-back + processing within the latency budget, and GPU headroom
* the safety governor stays active with Auto Mode off (it can lower, then recover
  up to the fixed tier) so weak hardware never "falls over"

The minimum tier still performs deband, Lanczos reconstruction, sharpening and
color – enhancement is reduced, never switched off.

## Output geometry

* **Match window** – overlay = GFN client rectangle, same size. Mouse input falls
  through to GFN at identical coordinates.
* **Fullscreen** – overlay = whole monitor, picture upscaled (aspect preserved,
  letterboxed). The real cursor is confined to the GFN client area and hidden; the
  captured cursor is drawn scaled inside the output.
* **Auto** – Match window when GFN already fills the monitor, Fullscreen otherwise.
* In Match-window mode with a lower **stream resolution** (detected or set), the
  frame is first reduced to that resolution and reconstructed with NSR.

## Content (stream) resolution detection

GeForce NOW scales its stream to the window itself. The detector measures two
difference-of-Gaussian band energies on a 512×512 center crop every 60 frames:
`top = mean|Y − G0.6|`, `ref = mean|G1.2 − G1.7|`. Calibration on the Kodak set
and synthetic HUD scenes with bilinear / Catmull-Rom / Lanczos upscalers and JPEG
q35–q85 gave ratio ≥ 1.07 for native content and 0.56–0.74 for 2× upscaled
content. Only factors ≥ 1.6 are acted upon and a decision must repeat three times
before it is applied (`ContentResTracker`).

## Neural Super Resolution models

| Model | Layers | Channels | Params | MAC / LR pixel | Validation PSNR-Y (Kodak hold-out + synthetic, JPEG q60) |
|---|---|---|---|---|---|
| bilinear | – | – | – | – | 28.88 dB |
| Catmull-Rom | – | – | – | – | 29.19 dB |
| NSR-S | 4 conv3x3 | 8 | 1,540 | ~1.5 k | 29.72 dB |
| NSR-L | 5 conv3x3 | 16 | 7,700 | ~7.6 k | 30.12 dB |

Without compression (clean downsampling) NSR-S/L reach 32.03 / 32.70 dB vs.
30.92 dB for Catmull-Rom. See `models/MODEL_CARD.md` and `tools/nsr_trainer`.

## Robustness

* device removed/reset → full teardown and re-creation (1 s back-off)
* capture failure → retry with back-off; after 3 failures switch to Desktop Duplication
* window closed / GFN exit → session ends, engine waits; restart reconnects automatically
* `WM_DISPLAYCHANGE`, `WM_DPICHANGED`, `WM_DEVICECHANGE` → monitors re-enumerated, overlay follows
* `WM_POWERBROADCAST` suspend/resume → engine suspended / resumed
* overlay only visible while GFN is the foreground window (Alt+Tab safe)
* unhandled exceptions → cursor restored, minidump (no heap) in `logs/`, crash recorded;
  two consecutive abnormal exits → safe mode (tier ≤ 3, no interpolation)
* settings: atomic write + `.bak`; corrupt file → backup → defaults (`settings.json.corrupt` kept)
