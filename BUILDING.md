# Building Better GFN Neural

## Requirements

* Windows 10/11 x64
* Visual Studio 2022 (17.8+) or newer with the **Desktop development with C++** workload
  (MSVC v143+, Windows 10/11 SDK 10.0.19041 or newer – the SDK provides `fxc.exe`,
  C++/WinRT headers and `windowsapp.lib`)
* CMake 3.21+ and Ninja (both ship with Visual Studio)
* Optional: NSIS 3.x for the installer (`choco install nsis`)

No external package manager is needed: Dear ImGui, nlohmann/json and stb are
vendored in `third_party/`, the NSR network weights are committed in
`shaders/generated/` and `src/neural/generated/`.

## Build (Developer PowerShell / x64 Native Tools prompt)

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure        # portable unit tests
build\bin\BetterGFNNeural.exe                      # run
```

Visual Studio IDE: *File → Open → Folder…* on the repository (CMake project), or
generate a solution with `cmake -S . -B build-vs -G "Visual Studio 17 2022" -A x64`.

All HLSL shaders are compiled at build time by `fxc` (`cmake/Shaders.cmake`) into
headers that are embedded in the executable – the EXE has no runtime file
dependencies. The C runtime is linked statically (`/MT`).

## Tests

```powershell
# GPU self-test (works on any D3D11 GPU; --warp forces the software rasterizer)
Start-Process build\bin\BetterGFNNeural.exe -ArgumentList '--selftest','--output','selftest.json' -Wait
# Headless benchmark
Start-Process build\bin\BetterGFNNeural.exe -ArgumentList '--benchmark','--output','bench.json' -Wait
# Integration tests (fake GeForce NOW window, crash recovery, corrupt settings, UI screenshots)
./tests/integration/run_integration.ps1 -Exe build\bin\BetterGFNNeural.exe -FakeGfn build\bin\FakeGfn.exe -OutDir test-results
```

The portable core (settings, profiles, GFN classification, Auto Mode, content
resolution analysis, NSR CPU reference) also builds on Linux/macOS:

```bash
cmake -S . -B build && cmake --build build -j && ctest --test-dir build
```

## Packaging

```powershell
./packaging/package.ps1 -BuildDir build -Version 1.0.0
# => dist\BetterGFNNeural.exe, dist\BetterGFNNeuralSetup.exe, dist\BetterGFNNeural-1.0.0-portable.zip
```

The portable ZIP contains `portable.dat`; with that file next to the EXE, settings
and logs are stored in the same folder instead of `%LOCALAPPDATA%\BetterGFNNeural`.

## Re-training the NSR models (optional)

See [models/MODEL_CARD.md](models/MODEL_CARD.md). The trainer is a single C++17
file (`tools/nsr_trainer/nsr_trainer.cpp`) and writes the JSON model, the HLSL
weight table and the C++ reference weights in one go.

## Continuous integration

`.github/workflows/build-test-package` builds with MSVC on `windows-latest`,
runs unit tests, the GPU self-test and benchmark on WARP, the integration tests,
and uploads the EXE, installer, portable ZIP and all test reports as artifacts.
