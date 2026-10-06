# Test strategy

| Level | What | Where it runs |
|---|---|---|
| Unit (portable) | settings JSON round trip / tolerance / sanitizing, crash-safe store with backup recovery, session guard (crash counting), presets & tier resolution, GPU family/tier estimation, game title parsing, GFN window classification (launcher, stream, fullscreen heuristic, browser, hidden windows), game profiles & aliases, Auto Mode (downgrade, upgrade without oscillation, minimum tier still enhances, interpolation rules, interpolation paused before tier drop, VRAM pressure, fixed-tier safety governor), frame pacing, content-resolution detection, NSR reference models, log redaction, command line | Linux + Windows CI (`bgn_unit_tests`) |
| GPU self-test | device + all shaders; NSR-S/L shader output == CPU reference of the trained network (FP32 and FP16 variants); every quality tier × several geometries (upscale 2×, 2.5×, 1.6×, native, stream-resolution reconstruction) + HDR output, checking size, finiteness, range and non-black output; optical-flow interpolation error vs. frame repeat and naive blend; temporal anti-flicker reduction | Windows CI on WARP; on users' PCs with `--selftest` |
| Benchmark | full pipeline per tier with GPU timestamps, recommendation | Windows CI on WARP (small frames); in-app on real GPUs |
| Integration | real `BetterGFNNeural.exe` in automation mode vs. `FakeGfn.exe` renamed to `GeForceNOW.exe`: Waiting → Connected (launcher) → game detected (with ® in the title) → resize → fullscreen → windowed → minimize/restore → game switch → GFN exit → Waiting → GFN restart directly into a game; profiles created; settings and logs written; no user name in logs; two simulated crashes → minidump + safe mode → clean exit → normal mode; corrupt settings recovery; UI screenshots of all pages | Windows CI |

Run locally:

```powershell
ctest --test-dir build --output-on-failure
Start-Process build\bin\BetterGFNNeural.exe -ArgumentList '--selftest','--output','selftest.json' -Wait
./tests/integration/run_integration.ps1 -Exe build\bin\BetterGFNNeural.exe -FakeGfn build\bin\FakeGfn.exe -OutDir test-results
```

Manual checks that need real hardware and a GeForce NOW account are listed in
[TEST_RESULTS.md](TEST_RESULTS.md#manual-verification-checklist).
