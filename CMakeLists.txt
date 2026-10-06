cmake_minimum_required(VERSION 3.21)

# Version comes from src/core/Version.h (single source of truth)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/core/Version.h" _bgn_version_h)
string(REGEX MATCH "kVersionString = \"([0-9]+\\.[0-9]+\\.[0-9]+)\"" _ "${_bgn_version_h}")
set(BGN_VERSION "${CMAKE_MATCH_1}")

project(BetterGFNNeural VERSION ${BGN_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(BGN_BUILD_TESTS "Build unit / GPU / integration tests" ON)

if(MSVC)
    # Static CRT: the portable EXE runs without the VC++ redistributable
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/utf-8 /W4 /permissive- /Zc:__cplusplus /EHsc /MP /wd4100 /wd4324 /wd4505)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00 _CRT_SECURE_NO_WARNINGS)
    add_link_options(/DEBUG:NONE)
endif()

# ---------------------------------------------------------------------------
# Portable core (also builds on Linux/macOS for fast unit testing)
# ---------------------------------------------------------------------------
add_library(bgn_portable STATIC
    src/core/Log.cpp
    src/core/StringUtil.cpp
    src/settings/Settings.cpp
    src/settings/SettingsStore.cpp
    src/settings/Presets.cpp
    src/profiles/GameTitle.cpp
    src/profiles/ProfileManager.cpp
    src/gfn/GfnClassifier.cpp
    src/automode/AutoModeController.cpp
    src/filters/ContentResolution.cpp
    src/telemetry/StreamQuality.cpp
    src/neural/NsrReference.cpp
    src/app/CommandLine.cpp
    src/ui/I18n.cpp)
target_include_directories(bgn_portable PUBLIC src third_party third_party/stb)

if(WIN32)
    include(cmake/Shaders.cmake)

    add_library(imgui STATIC
        third_party/imgui/imgui.cpp
        third_party/imgui/imgui_draw.cpp
        third_party/imgui/imgui_tables.cpp
        third_party/imgui/imgui_widgets.cpp
        third_party/imgui/backends/imgui_impl_dx11.cpp
        third_party/imgui/backends/imgui_impl_win32.cpp)
    target_include_directories(imgui PUBLIC third_party/imgui)
    if(MSVC)
        target_compile_options(imgui PRIVATE /W0)
    endif()

    add_library(bgn_win STATIC
        src/core/StbImpl.cpp
        src/platform/Win32.cpp
        src/platform/Paths.cpp
        src/platform/Display.cpp
        src/platform/Startup.cpp
        src/platform/Controllers.cpp
        src/platform/SystemMonitor.cpp
        src/platform/CrashHandler.cpp
        src/platform/CursorControl.cpp
        src/platform/Tray.cpp
        src/renderer/GpuDevice.cpp
        src/renderer/GpuTimer.cpp
        src/renderer/ShaderLibrary.cpp
        src/renderer/GpuResources.cpp
        src/renderer/OverlayPresenter.cpp
        src/capture/WgcCapture.cpp
        src/capture/DuplicationCapture.cpp
        src/framegen/OpticalFlow.cpp
        src/framegen/FrameInterpolator.cpp
        src/neural/NsrUpscaler.cpp
        src/filters/Pipeline.cpp
        src/engine/Engine.cpp
        src/gfn/GfnDetector.cpp
        src/benchmark/GpuTestUtil.cpp
        src/benchmark/Benchmark.cpp
        src/benchmark/SelfTest.cpp
        src/ui/Theme.cpp
        src/ui/Widgets.cpp
        src/ui/UiApp.cpp
        src/ui/pages/HomePage.cpp
        src/ui/pages/EnhancementPage.cpp
        src/ui/pages/DisplayPage.cpp
        src/ui/pages/GamesPage.cpp
        src/ui/pages/PerformancePage.cpp
        src/ui/pages/BenchmarkPage.cpp
        src/ui/pages/SettingsPage.cpp
        src/app/Application.cpp)
    add_dependencies(bgn_win bgn_shaders)
    target_include_directories(bgn_win PUBLIC shaders "${BGN_SHADER_OUT_DIR}")

    # Windows SDK feature probe: GraphicsCaptureSession.MinUpdateInterval (Windows 11 24H2 SDK)
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_FLAGS "/std:c++20 /EHsc")
    set(CMAKE_REQUIRED_LIBRARIES windowsapp)
    check_cxx_source_compiles("
        #include <winrt/Windows.Graphics.Capture.h>
        int main() { winrt::Windows::Graphics::Capture::GraphicsCaptureSession s{nullptr}; if (s) s.MinUpdateInterval(winrt::Windows::Foundation::TimeSpan{1}); return 0; }"
        BGN_HAS_WGC_MIN_UPDATE_INTERVAL)
    unset(CMAKE_REQUIRED_FLAGS)
    unset(CMAKE_REQUIRED_LIBRARIES)
    if(BGN_HAS_WGC_MIN_UPDATE_INTERVAL)
        target_compile_definitions(bgn_win PRIVATE BGN_HAS_WGC_MIN_UPDATE_INTERVAL=1)
    endif()

    target_link_libraries(bgn_win PUBLIC bgn_portable imgui
        d3d11 dxgi dxguid dcomp dwmapi shcore pdh dbghelp avrt windowsapp user32 gdi32 shell32 ole32 advapi32)

    add_executable(BetterGFNNeural WIN32 src/app/main.cpp resources/app.rc resources/app.manifest)
    target_link_libraries(BetterGFNNeural PRIVATE bgn_win)
    set_target_properties(BetterGFNNeural PROPERTIES OUTPUT_NAME "BetterGFNNeural")
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(BGN_BUILD_TESTS)
    enable_testing()
    add_executable(bgn_unit_tests tests/unit/test_portable.cpp)
    target_link_libraries(bgn_unit_tests PRIVATE bgn_portable)
    add_test(NAME unit COMMAND bgn_unit_tests)
    if(WIN32)
        add_executable(FakeGfn WIN32 tests/integration/FakeGfn.cpp)
        target_link_libraries(FakeGfn PRIVATE user32 gdi32 shell32)
    endif()
endif()

# ---------------------------------------------------------------------------
# Install layout (used by the packaging scripts)
# ---------------------------------------------------------------------------
if(WIN32)
    install(TARGETS BetterGFNNeural RUNTIME DESTINATION .)
    install(FILES README.md LICENSE THIRD_PARTY_NOTICES.md CHANGELOG.md DESTINATION .)
    install(DIRECTORY shaders/ DESTINATION shaders FILES_MATCHING PATTERN "*.hlsl" PATTERN "*.hlsli" PATTERN "*.h")
    install(DIRECTORY models/ DESTINATION models)
endif()
