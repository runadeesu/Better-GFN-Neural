# 機能詳細 / Feature details

Each section lists what is implemented, where, and how it adapts automatically.

## 1. Automatic flow (no hotkeys)

`app/Application.cpp` polls GeForce NOW every 0.4 s:

1. **GeForce NOW detection** – `gfn/GfnDetector` enumerates top-level windows,
   keeps only processes named `GeForceNOW.exe`, `GeForceNOWStreamer.exe` (plus
   user-configured names and, optionally, browsers showing "GeForce NOW").
   Only these windows' titles are read.
2. **Game window detection** – `gfn/GfnClassifier` decides *Waiting* (not running),
   *Connected* (launcher only) or *Streaming*: a visible ≥480×270 GFN window whose
   title is a game (`"<Game> on GeForce NOW"`, trademark symbols removed), or a
   launcher-titled window that covers its whole monitor (borderless stream).
3. **Profile** – `profiles/ProfileManager` maps the game to a profile (exact key,
   franchise prefix such as *Call of Duty: Black Ops 6 → callofduty*, or a built-in
   template); unknown games get a profile that follows the global settings.
4. **Engine** – the target window and an immutable config snapshot go to the
   engine thread, which starts capture, GPU processing and presentation.
5. When the game ends the engine returns to waiting; when GFN restarts it
   reconnects automatically. Resizing, moving the window to another monitor,
   fullscreen/windowed switches and monitor changes are followed every frame.

The overlay is shown only while GeForce NOW is the foreground window (Alt+Tab
hides it immediately) and never takes focus or input.

## 2. AUTO MODE

See [ARCHITECTURE.md](ARCHITECTURE.md#quality-tiers-and-auto-mode). Inputs:
GPU time per frame (timestamp queries, p95), frame-interpolation cost, VRAM
usage and OS budget (`IDXGIAdapter3::QueryVideoMemoryInfo`), GPU 3D-engine load
and CPU load (Windows performance counters), input FPS, output FPS, display
refresh rate (exact rational value from `QueryDisplayConfig`), capture → present
latency, dropped capture frames, input/output resolution (via cost).

| Tier | Name | Upscaler | Cleanup | Temporal | Deblur | Flow |
|---|---|---|---|---|---|---|
| 0 | Minimal | Lanczos-AR | deband | – | – | – |
| 1 | Light | **NSR-T** | deband + deblock | – | – | – |
| 2 | Performance | **NSR-T** | + denoise, dark-scene | flow | adaptive | fast |
| 3 | Balanced Lite | NSR-S | full | flow | + motion | fast |
| 4 | Balanced | NSR-S | full | flow | + motion | fast |
| 5 | Quality | NSR-L | high-quality (7×7) | flow | + motion | HQ (8 px cells) |
| 6 | Ultra | NSR-L | high-quality | flow | + motion | HQ |

Presets: **Auto** (0–6, starts from the GPU estimate or benchmark), **Ultra**
(starts at 6), **Quality** (≤5), **Balanced** (≤4), **Performance** (≤2),
**Low Latency** (≤3, no interpolation, 30 % budget, 6 ms latency budget).
Explicit per-feature choices act as upper bounds that Auto Mode may lower.

## 3. Neural Super Resolution

* CNN ×2 on luma + Catmull-Rom chroma, three trained models (T/S/L; NSR-T is a 336-parameter single-pass model for integrated / low-end GPUs) – [MODEL_CARD](../models/MODEL_CARD.md)
* Ratios other than 2× → NSR ×2 then Lanczos resample (e.g. 720p→1080p, 1080p→1440p, 1440p→4K)
* Output resolution: Auto = monitor (aspect-preserving letterbox), or 1080p/1440p/2160p/source
* **In-place reconstruction**: when GFN itself fills the screen with a lower
  stream resolution (e.g. 1080p stream on a 4K monitor) the stream resolution is
  detected from the picture (or set manually) and the frame is reconstructed
  from that resolution with NSR instead of GFN's own simple scaling
* Modes: Auto (tier), Quality (NSR-L), Balanced (NSR-S), Performance
  (NSR-T, single pass), Native (no upscaling)

## 4. Temporal reconstruction (`shaders/temporal.hlsl`)

Flow-reprojected history (point-sampled when static to keep detail), 3×3
YCoCg mean/variance box with a wider box for static pixels (anti-flicker on
thin lines, text, fences, foliage, distant geometry), confidence from flow match
error and texture, history weight reduced on fast motion and when the clipped
history disagrees (ghosting reduction), no history across disocclusions/borders.
The self-test measures the frame-to-frame flicker reduction on a static noisy scene.

## 5. Stream compression cleanup (`shaders/cleanup.hlsl`)

Edge-aware bilateral (5×5, 7×7 in HQ) whose range sigma combines: denoise
strength, a flat-area term for blocking/macroblock steps, a ring term near strong
edges for mosquito noise, and a dark-scene boost. f3kdb-style deband with random
rotated sample cross and thresholds that rise in dark regions; final triangular
dither in the present pass removes residual banding when quantizing.

## 6. Deblur (`shaders/deblur.hlsl`)

Band-split unsharp deconvolution (edge band and detail band with separate gains)
plus 1-D deconvolution along the optical-flow direction. Motion strength ramps
in from 0.75 px/frame, is scaled by flow confidence and backs off above
24–64 px/frame; everything is luma-only and clamped to the local range.

## 7. Adaptive sharpening (`shaders/finish.hlsl`)

Laplacian sharpening limited to the 3×3 min/max (+ small overshoot), reduced on
high-contrast edges, reduced with motion, boosted on static high-contrast micro
structure (text / HUD / UI), reduced on skin-tone pixels (YCbCr skin cluster) to
avoid unnatural facial outlines.

## 8. Frame interpolation (`shaders/interp.hlsl`, `framegen/`)

Hierarchical block-matching optical flow (1/32 → 1/4 or 1/2 resolution, ±192 px
range, temporal + spatial candidates, sub-pixel parabola fit, vector median),
bidirectional warping at t = 0.5 with a fixed-point mid-frame flow estimate,
mismatch/occlusion fallback to the newest frame, low-confidence fallback,
and exact passthrough of unchanged pixels (HUD / UI never warp).
Pacing (`framegen/FramePacing.h`): each half interval is held for
`k = floor(refresh / (2·fps) + 0.25)` vblanks (60→120 Hz: 1+1, 60→240 Hz: 2+2).
Auto enables it only when the display can show the extra frames and the
half-frame hold-back plus processing stays inside the latency budget; the
self-test checks that interpolation beats both frame repetition and naive
blending on panning content.

## 9. HDR+ / color (`shaders/finish.hlsl`)

* HDR display + SDR stream: SDR is mapped to the Windows SDR content brightness
  (paper white) and highlights are expanded towards the display peak (HDR+).
* HDR display + HDR stream (FP16 scRGB capture): processed in PQ space and
  output as scRGB.
* SDR display: SDR Enhancement – auto levels from the scene histogram (limited to
  4 % black / 6 % white so nothing is crushed), shadow lift that keeps pure black
  black, contrast S-curve, highlight-gradation shoulder, gamma, local contrast
  with halo control, saturation, vibrance (skin aware), white balance.

## 10. Display handling

Monitor enumeration with exact refresh rate (e.g. 59.94, 143.98, 165, 240 Hz),
HDR enabled/supported, luminance metadata, SDR white level, bit depth, DPI and
adapter. Output modes: Match window / Fullscreen (cursor confined to the GFN
area and rendered scaled) / Auto. Multi-monitor and preferred monitor selection.
VRR: reports DXGI tearing (VRR-capable) support; presentation is composed by DWM.

## 11. Statistics, benchmark, tray, startup, controllers

As listed in the README. The tray icon is drawn at runtime in four states
(Waiting – grey ring, Connected – indigo, Enhancing – mint, Paused – amber).
"Start with Windows" writes `HKCU\...\Run` with `--background`. Controllers are
listed via XInput and the raw-input HID device list without opening devices.

## 12. Safety & recovery

Capture failure → retry with back-off and fallback to Desktop Duplication;
device removed → re-created; display change / sleep → re-initialized;
unhandled exception → cursor restored, minidump, safe mode after two
consecutive crashes; corrupt settings → backup/defaults.

## 13. Low-spec PCs: realistic AI, low latency, smooth motion (v1.1)

* **NSR-T** (`shaders/nsr_tiny.hlsl`): 4-channel, one-hidden-layer CNN (336
  parameters) fused into a single dispatch with 16×16 groupshared tiles and a
  3-pixel halo. It replaces Lanczos-AR at tiers 1–2, so even integrated GPUs get
  learned edge/texture reconstruction (validation PSNR in
  [MODEL_CARD](../models/MODEL_CARD.md)).
* **Motion-compensated temporal from tier 2** (fast optical flow) to avoid the
  ghosting of the no-flow fallback.
* **Stutter smoothing** (`shaders/extrap.hlsl`): if the next stream frame is
  more than 1.6 intervals late, the current output is extrapolated ×0.5 along
  its flow and shown once. It never holds back a real frame (no added latency);
  the number of smoothed frames is shown on the Performance page and the OSD.
* **Battery saver** caps the tier on battery; the stream-quality monitor raises
  cleanup only when the stream is actually blocky.
* Recommended for low-spec PCs: preset **Auto** (or **Low Latency**), frame
  interpolation **Off** or **Auto**, stutter smoothing **On**.

## 14. Omakase mode (fully automatic, default) — `src/profiles/Omakase.cpp`

* `classifyGame()` maps the game key to Competitive / Cinematic / Racing /
  Blocky / General (keyword table, racing checked before cinematic so
  "forzahorizon" is racing). The built-in profiles use the same kinds.
* `planOmakase()` builds the engine configuration: the kind's tuning, Auto
  preset (Low Latency for competitive), Natural style, automatic upscaler,
  color and HDR, adaptive cleanup. Low Latency Mode, stutter smoothing and
  automatic output are forced on. Accessibility, OSD and battery settings are
  personal and kept.
* Learning: while enhancing on AC power, the time spent at each tier is
  accumulated per game; at the end of a session (≥ 60 s) the dominant tier is
  stored in the profile with the GPU name (`learned_tier`, `learned_gpu`) and
  used as the starting tier next time on the same GPU.
