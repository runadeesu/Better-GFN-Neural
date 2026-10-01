// Better GFN Neural - entry point.
#include "platform/Win32.h"

#include <objbase.h>
#include <shellapi.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "app/Application.h"
#include "app/CommandLine.h"
#include "benchmark/Benchmark.h"
#include "benchmark/SelfTest.h"
#include "core/Log.h"
#include "core/StringUtil.h"
#include "platform/CrashHandler.h"
#include "platform/Paths.h"

using namespace bgn;

static void attachConsole() {
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
    }
}

static void writeOutput(const CommandLine& cmd, const std::string& text) {
    std::printf("%s\n", text.c_str());
    std::fflush(stdout);
    if (!cmd.outputJson.empty()) {
        std::ofstream f(std::filesystem::path(widen(cmd.outputJson)), std::ios::binary);
        f << text;
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.push_back(narrow(argv[i]));
    LocalFree(argv);
    CommandLine cmd = parseCommandLine(args);

    if (cmd.selfTest || cmd.benchmark) {
        attachConsole();
        AppPaths paths = resolveAppPaths(cmd);
        registerPrivacyRedactions();
        Log::init(paths.logsDir, LogLevel::Info);
        installCrashHandler(paths.logsDir);
        int rc = 0;
        if (cmd.selfTest) {
            SelfTestOptions o;
            o.forceWarp = cmd.forceWarp;
            std::string json;
            bool ok = runSelfTest(o, json);
            writeOutput(cmd, json);
            rc = ok ? 0 : 1;
        } else {
            BenchmarkOptions o;
            o.forceWarp = cmd.forceWarp;
            if (cmd.forceWarp) {
                o.inW = 480;
                o.inH = 270;
                o.outW = 960;
                o.outH = 540;
                o.framesPerTier = 8;
                o.warmupFrames = 2;
            }
            std::string err;
            BenchmarkResult r = runBenchmark(o, err);
            writeOutput(cmd, r.valid ? benchmarkResultToJson(r) : std::string("{\"error\": \"") + err + "\"}");
            rc = r.valid ? 0 : 1;
        }
        Log::shutdown();
        CoUninitialize();
        return rc;
    }

    int rc = 0;
    {
        Application app(cmd);
        rc = app.run();
    }
    CoUninitialize();
    return rc;
}
