# 既知の問題と制限 / Known issues and limitations

## Depends on GeForce NOW / Windows behaviour

* **Window title format.** Game names are taken from the GeForce NOW window title
  (`"<Game> on GeForce NOW"` and similar). If a GeForce NOW update changes the title
  format, the game is still enhanced as an unnamed stream when the window is
  borderless-fullscreen, but the per-game profile cannot be chosen automatically.
  Extra process names can be added in `settings.json` (`gfn.extra_process_names`).
* **Capture protection.** If GeForce NOW (or a game) marks its window as excluded
  from capture (`WDA_EXCLUDEFROMCAPTURE`), Windows delivers black frames. Better
  GFN Neural does not circumvent this; the picture would be black and enhancement
  should be paused for that title.
* **Yellow capture border.** Windows 10 versions before 20348 always draw a
  border around captured windows; Windows 11 allows it to be hidden (done
  automatically).
* **Capture frame-rate cap.** Some Windows 10 builds limit Windows Graphics Capture
  to the monitor refresh rate; Windows 11 24H2+ allows a 1 ms minimum update interval
  (enabled automatically when the SDK and OS support it).
* **VRR.** The output is a click-through overlay composed by the Desktop Window
  Manager, so it does not get independent-flip / VRR presentation. Frame pacing is
  aligned to the display refresh instead.

## Design limitations

* **Not DLSS.** Without game motion vectors and depth (not exposed by GeForce NOW),
  super resolution uses a single-frame CNN plus optical-flow-based temporal
  accumulation. Quality gains are real but moderate (see the model card).
* **Frame interpolation adds latency** of half an input frame interval (8.3 ms at
  60 fps) plus processing. Auto mode keeps it off when that exceeds the latency
  budget and never enables it with the Low Latency preset.
* **Fullscreen-upscale mode with a windowed GFN** confines the mouse cursor to the
  GFN window while the overlay is active (the cursor is drawn scaled in the output).
  Alt+Tab or the Windows key leave as usual. If you prefer a normal window, choose
  *Display → Output mode → Match GFN window*.
* **System cursor hiding** in fullscreen-upscale mode uses the Magnification API;
  if Windows refuses, the real cursor stays visible in addition to the scaled one.
* **Stream-resolution auto detection** reliably detects ≥ ~1.7× upscaling (e.g.
  1080p on 4K, 720p on 1440p). Milder ratios (1440p on 4K, 1080p on 1440p) are left
  native unless the stream resolution is set manually (Enhancement → Stream resolution).
* **Face protection** is a skin-tone heuristic, not face detection.
* **Desktop Duplication fallback** requires the GFN monitor to be driven by the same
  GPU as the processing device and makes our overlay invisible to screen recorders
  (`WDA_EXCLUDEFROMCAPTURE`, needed to avoid feedback).
* **HDR capture of SDR content** is captured as 8-bit; HDR streams are captured in
  FP16 only while Windows HDR is enabled on that monitor.

## Verification limits of this release

* The CI environment has no hardware GPU and no real GeForce NOW account/session.
  GPU code is verified on the Direct3D 11 WARP rasterizer, GeForce NOW detection
  and the full application lifecycle against a scripted fake GFN window. Real-GPU
  performance and real GeForce NOW streams must be validated on user hardware; the
  in-app Benchmark and Performance page are provided for that. See
  [TEST_RESULTS.md](TEST_RESULTS.md).
* The installer is not code-signed; Windows SmartScreen may warn on first run.
