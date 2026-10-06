#include "engine/Engine.h"

#include <avrt.h>
#include <dwmapi.h>

#include <winrt/base.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <memory>

#include "automode/AutoModeController.h"
#include "benchmark/GpuTestUtil.h"
#include "media/ImageOps.h"
#include "capture/DuplicationCapture.h"
#include "capture/WgcCapture.h"
#include "core/Log.h"
#include "core/StringUtil.h"
#include "filters/ContentResolution.h"
#include "filters/Pipeline.h"
#include "framegen/FramePacing.h"
#include "platform/CursorControl.h"
#include "platform/Display.h"
#include "renderer/OverlayPresenter.h"
#include "telemetry/RollingStats.h"
#include "telemetry/StreamQuality.h"

namespace bgn {

namespace {

bool isFocused(HWND target, DWORD pid) {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    if (fg == target) return true;
    DWORD fpid = 0;
    GetWindowThreadProcessId(fg, &fpid);
    return fpid != 0 && fpid == pid;
}

RECT clientScreenRect(HWND hwnd) {
    RECT c{};
    GetClientRect(hwnd, &c);
    POINT o{0, 0};
    ClientToScreen(hwnd, &o);
    return RECT{o.x, o.y, o.x + c.right, o.y + c.bottom};
}

int rw(const RECT& r) { return r.right - r.left; }
int rh(const RECT& r) { return r.bottom - r.top; }

struct OutputPlan {
    RECT overlay{};   // screen rect of the overlay window
    RECT dst{};       // destination rect inside the overlay (letterbox)
    int outW = 0, outH = 0;
    bool fullscreen = false;
};

// Fits w x h into maxW x maxH preserving aspect ratio.
void fitInside(int w, int h, int maxW, int maxH, int& ow, int& oh) {
    double s = std::min(double(maxW) / double(w), double(maxH) / double(h));
    ow = std::max(2, int(std::lround(w * s)) & ~1);
    oh = std::max(2, int(std::lround(h * s)) & ~1);
}

OutputPlan planOutput(const EngineConfig& cfg, const RECT& client, const MonitorInfo& mon) {
    OutputPlan p;
    const int cw = std::max(1, rw(client)), ch = std::max(1, rh(client));
    bool fullscreen = false;
    switch (cfg.outputMode) {
    case OutputMode::MatchWindow: fullscreen = false; break;
    case OutputMode::Fullscreen: fullscreen = true; break;
    case OutputMode::Auto: fullscreen = cw < mon.width * 0.97 || ch < mon.height * 0.97; break;
    }
    p.fullscreen = fullscreen;
    if (!fullscreen) {
        p.overlay = client;
        p.dst = RECT{0, 0, cw, ch};
        p.outW = cw;
        p.outH = ch;
        return p;
    }
    p.overlay = mon.rect;
    int fitW, fitH;
    fitInside(cw, ch, mon.width, mon.height, fitW, fitH);
    int ox = (mon.width - fitW) / 2, oy = (mon.height - fitH) / 2;
    p.dst = RECT{ox, oy, ox + fitW, oy + fitH};
    int maxW = fitW, maxH = fitH;
    switch (cfg.outputResolution) {
    case OutputResolution::Auto: break;
    case OutputResolution::Source: maxW = cw; maxH = ch; break;
    case OutputResolution::R1080p: maxW = std::min(maxW, 1920); maxH = std::min(maxH, 1080); break;
    case OutputResolution::R1440p: maxW = std::min(maxW, 2560); maxH = std::min(maxH, 1440); break;
    case OutputResolution::R2160p: maxW = std::min(maxW, 3840); maxH = std::min(maxH, 2160); break;
    }
    fitInside(cw, ch, maxW, maxH, p.outW, p.outH);
    return p;
}

int streamHeightSetting(StreamResolution s) {
    switch (s) {
    case StreamResolution::R720p: return 720;
    case StreamResolution::R1080p: return 1080;
    case StreamResolution::R1440p: return 1440;
    default: return 0;
    }
}

} // namespace

struct Engine::Session {
    GpuDevice device;
    ShaderLibrary shaders;
    Pipeline pipeline;
    GpuTimer timer;
    std::unique_ptr<CaptureSource> capture;
    OverlayPresenter presenter;
    CursorControl cursor;
    AutoModeController automode;
    ContentResTracker contentTracker{3};

    bool deviceReady = false;
    double deviceRetryAt = 0;
    EngineTarget target;
    bool sessionReady = false;
    double sessionRetryAt = 0;
    int captureFailures = 0;
    bool usingDuplication = false;
    bool wantHdrCapture = false;
    bool cursorCaptured = false;

    EngineConfig cfg;
    uint64_t cfgRevision = ~0ull;
    PresetPolicy policy;

    std::vector<MonitorInfo> monitors;
    MonitorInfo mon;
    HMONITOR monHandle = nullptr;

    RateCounter inRate{1.0}, outRate{1.0}, capRate{1.0};
    RollingStats gpuMs{90}, latency{90}, frameInterval{120}, fgMs{60};
    std::array<RollingStats, kGpuStageCount> stageMs;
    double lastPresent = 0, lastAuto = 0, lastPublish = 0, lastTopmost = 0, lastHistory = 0;
    uint64_t captured = 0, presented = 0, interpolated = 0, droppedBase = 0;
    bool wasVisible = false;
    std::string autoReason = "Starting";
    std::deque<float> histGpu, histIn, histOut;
    bool hdrInput = false;
    OutputPlan plan;
    PipelineGeometry geo;
    int lastDropped = 0;
    uint64_t lastDroppedTotal = 0;
    AutoDecision lastDecision;
    StreamQualityTracker quality;
    double lastFrameArrival = 0;
    std::deque<float> histQuality;
    int batteryCap = kMaxTier;

    Session() {
        for (auto& r : stageMs) r = RollingStats(60);
    }
};

Engine::Engine() : wake_(CreateEventW(nullptr, FALSE, FALSE, nullptr)) {}

Engine::~Engine() { stop(); }

void Engine::start() {
    if (running_) return;
    quit_ = false;
    running_ = true;
    thread_ = std::thread([this] { threadMain(); });
}

void Engine::stop() {
    if (!running_ && !thread_.joinable()) return;
    quit_ = true;
    SetEvent(wake_.get());
    if (thread_.joinable()) thread_.join();
    if (shotThread_.joinable()) shotThread_.join();
    running_ = false;
}

void Engine::requestScreenshot(const ScreenshotRequest& r) {
    std::lock_guard lock(mutex_);
    pendingShot_ = r;
}

void Engine::setTarget(const EngineTarget& t) {
    {
        std::lock_guard lock(mutex_);
        if (target_ == t) return;
        target_ = t;
    }
    SetEvent(wake_.get());
}

void Engine::setConfig(const EngineConfig& c) {
    {
        std::lock_guard lock(mutex_);
        config_ = c;
    }
    SetEvent(wake_.get());
}

void Engine::setSystemSample(const SystemSample& s) {
    std::lock_guard lock(mutex_);
    system_ = s;
}

void Engine::notifyDisplayChange() {
    {
        std::lock_guard lock(mutex_);
        displayChanged_ = true;
    }
    SetEvent(wake_.get());
}

void Engine::setSuspended(bool s) {
    {
        std::lock_guard lock(mutex_);
        suspended_ = s;
    }
    SetEvent(wake_.get());
}

EngineStats Engine::stats() const {
    std::lock_guard lock(mutex_);
    return stats_;
}

void Engine::takePendingScreenshot(Session& s, bool hdrOutput, bool hdrInput, float sdrWhiteNits) {
    std::optional<ScreenshotRequest> req;
    {
        std::lock_guard lock(mutex_);
        req.swap(pendingShot_);
    }
    if (!req) return;
    // GPU readback (blocking for a few ms; only when the user asked for a screenshot)
    auto fin = std::make_shared<std::vector<float>>();
    auto orig = std::make_shared<std::vector<float>>();
    int fw = 0, fh = 0, ow = 0, oh = 0;
    bool ok = readbackTexture(s.device.device(), s.device.context(), s.pipeline.finalTexture(), *fin, fw, fh);
    if (ok && req->comparison) ok = readbackTexture(s.device.device(), s.device.context(), s.pipeline.inputTexture(), *orig, ow, oh);
    if (!ok) {
        std::lock_guard lock(mutex_);
        stats_.screenshotError = "Screenshot failed (GPU readback)";
        return;
    }
    if (shotThread_.joinable()) shotThread_.join();
    shotThread_ = std::thread([this, r = *req, fin, orig, fw, fh, ow, oh, hdrOutput, hdrInput, sdrWhiteNits] {
        std::error_code ec;
        std::filesystem::create_directories(r.folder, ec);
        const auto base = r.folder / std::filesystem::path(widen(r.baseName));
        Image8 enhanced = encodeToSdr8(*fin, fw, fh, hdrOutput ? PixelEncoding::ScRgbLinear : PixelEncoding::SdrGamma, sdrWhiteNits);
        bool ok = writePng(std::filesystem::path(base.wstring() + L".png"), enhanced);
        if (ok && r.comparison && !orig->empty()) {
            Image8 original = encodeToSdr8(*orig, ow, oh, hdrInput ? PixelEncoding::PqWorking : PixelEncoding::SdrGamma, sdrWhiteNits);
            ok &= writePng(std::filesystem::path(base.wstring() + L"_original.png"), original);
            ok &= writePng(std::filesystem::path(base.wstring() + L"_compare.png"), sideBySide(original, enhanced));
        }
        std::lock_guard lock(mutex_);
        if (ok) {
            ++stats_.screenshotsSaved;
            stats_.lastScreenshot = narrow(base.wstring() + L".png");
            stats_.screenshotError.clear();
            BGN_LOG_INFO("Engine", "screenshot saved ({}x{}{})", fw, fh, r.comparison ? ", with comparison" : "");
        } else {
            stats_.screenshotError = "Screenshot could not be saved";
            BGN_LOG_WARN("Engine", "screenshot could not be written to the selected folder");
        }
    });
}

void Engine::publish(const EngineStats& st) {
    std::lock_guard lock(mutex_);
    stats_ = st;
}

static void endSession(Engine::Session& s) {
    s.cursor.deactivate();
    s.presenter.setVisible(false);
    s.presenter.destroy();
    if (s.capture) {
        s.capture->releaseFrame();
        s.capture->stop();
        s.capture.reset();
    }
    if (s.deviceReady) s.pipeline.resetHistory();
    s.sessionReady = false;
    s.wasVisible = false;
    s.cursorCaptured = false;
}

static void destroyDevice(Engine::Session& s) {
    endSession(s);
    s.pipeline.release();
    s.timer.release();
    s.shaders.release();
    s.device.destroy();
    s.deviceReady = false;
}

void Engine::threadMain() {
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (...) {
        BGN_LOG_WARN("Engine", "COM apartment already initialized");
    }
    DWORD taskIndex = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Games", &taskIndex);
    if (!mmcss) SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    BGN_LOG_INFO("Engine", "engine thread started (MMCSS {})", mmcss ? "Games" : "unavailable");

    auto session = std::make_unique<Session>();
    while (!quit_) {
        try {
            loopOnce(*session);
        } catch (const winrt::hresult_error& e) {
            BGN_LOG_ERROR("Engine", "WinRT error: {}", hrToString(e.code()));
            endSession(*session);
            session->sessionRetryAt = qpcSeconds() + 2.0;
        } catch (const std::exception& e) {
            BGN_LOG_ERROR("Engine", "error: {}", e.what());
            endSession(*session);
            session->sessionRetryAt = qpcSeconds() + 2.0;
        }
    }
    destroyDevice(*session);
    session.reset();
    if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
    BGN_LOG_INFO("Engine", "engine thread stopped");
}

void Engine::loopOnce(Session& s) {
    const double now = qpcSeconds();
    EngineTarget target;
    EngineConfig cfg;
    SystemSample sys;
    bool displayChanged, suspended;
    {
        std::lock_guard lock(mutex_);
        target = target_;
        cfg = config_;
        sys = system_;
        displayChanged = displayChanged_;
        displayChanged_ = false;
        suspended = suspended_;
    }
    if (s.presenter.hwnd()) s.presenter.pumpMessages();

    EngineStats st;
    auto idle = [&](const char* state, DWORD waitMs) {
        if (s.sessionReady || s.presenter.hwnd()) endSession(s);
        st.state = state;
        st.gpuName = s.device.info().name;
        st.gpuFamily = s.device.info().family;
        if (now - s.lastPublish > 0.25) {
            s.lastPublish = now;
            EngineStats prev = stats();
            prev.state = state;
            prev.active = false;
            prev.overlayVisible = false;
            prev.inputFps = prev.outputFps = prev.captureFps = 0;
            publish(prev);
        }
        WaitForSingleObject(wake_.get(), waitMs);
    };

    if (suspended) return idle("Paused (system sleep)", 200);
    if (!cfg.enabled) return idle("Paused", 200);
    if (!target.hwnd || !IsWindow(target.hwnd)) return idle("Waiting for a GeForce NOW game", 200);

    // ---- Device -------------------------------------------------------------
    if (!s.deviceReady) {
        if (now < s.deviceRetryAt) return idle("Waiting for GPU", 100);
        GpuDeviceOptions opt;
        opt.forceWarp = cfg.forceWarp;
        if (!s.device.create(opt) || !s.shaders.init(s.device.device()) || !s.pipeline.init(s.device, s.shaders)) {
            destroyDevice(s);
            s.deviceRetryAt = now + 2.0;
            std::lock_guard lock(mutex_);
            stats_.lastError = "GPU initialization failed";
            return;
        }
        s.timer.init(s.device.device());
        s.deviceReady = true;
        s.cfgRevision = ~0ull;
    }

    // ---- Config / Auto Mode policy -----------------------------------------
    const int batteryCap = batteryTierCap(sys.onBattery, cfg.power.batterySaver, cfg.power.batteryMaxTier);
    if (batteryCap != s.batteryCap) {
        BGN_LOG_INFO("Engine", "power: {} (max tier {})", sys.onBattery ? "on battery" : "on AC", batteryCap);
        s.batteryCap = batteryCap;
        s.cfgRevision = ~0ull; // re-apply the policy with the new cap
    }
    if (cfg.revision != s.cfgRevision) {
        s.cfgRevision = cfg.revision;
        s.cfg = cfg;
        s.policy = policyFor(cfg.preset, cfg.lowLatency, cfg.priority);
        if (cfg.safeMode) s.policy.maxTier = std::min(s.policy.maxTier, 3);
        if (s.batteryCap < kMaxTier) {
            s.policy.maxTier = std::min(s.policy.maxTier, s.batteryCap);
            s.policy.minTier = std::min(s.policy.minTier, s.policy.maxTier);
            s.policy.startTier = std::min(s.policy.startTier, s.policy.maxTier);
        }
        int initial = cfg.initialTier;
        if (cfg.preset != Preset::Auto) initial = s.policy.startTier;
        if (initial < 0) initial = estimateTierForGpu(s.device.info().vendorId, s.device.info().name, s.device.info().dedicatedVramMB / 1024.0);
        initial = std::clamp(initial, s.policy.minTier, s.policy.maxTier);
        FrameGenMode fg = (cfg.safeMode || s.batteryCap < kMaxTier) ? FrameGenMode::Off : cfg.enhancement.frameGen;
        s.automode.configure(s.policy, fg, cfg.autoMode, initial);
        if (!cfg.autoMode) s.automode.setFixedTier(initial);
        s.autoReason = cfg.autoMode ? "Auto Mode active" : "Fixed quality (Auto Mode off)";
        BGN_LOG_INFO("Engine", "config: preset {}, auto {}, start tier {}, frame interpolation {}", toString(cfg.preset), cfg.autoMode, initial, toString(fg));
    }

    // ---- Monitor -------------------------------------------------------------
    HMONITOR hmon = MonitorFromWindow(target.hwnd, MONITOR_DEFAULTTONEAREST);
    if (displayChanged || hmon != s.monHandle || s.monitors.empty()) {
        s.monitors = enumerateMonitors();
        const MonitorInfo* m = nullptr;
        if (!cfg.preferredMonitor.empty()) m = findMonitorByName(s.monitors, cfg.preferredMonitor);
        if (!m || m->handle != hmon) m = findMonitor(s.monitors, hmon);
        if (m) s.mon = *m;
        s.monHandle = hmon;
        if (displayChanged) {
            BGN_LOG_INFO("Engine", "display configuration changed: {}", describeMonitor(s.mon));
            s.pipeline.resetHistory();
        }
    }

    // ---- Capture session -----------------------------------------------------
    const bool wantHdr = s.mon.hdrEnabled;
    const bool sameWindow = target.hwnd == s.target.hwnd && target.pid == s.target.pid;
    if (sameWindow) s.target.gameName = target.gameName; // a title change alone must not restart capture
    if (s.sessionReady && (!sameWindow || s.capture->failed() || wantHdr != s.wantHdrCapture)) {
        if (s.capture && s.capture->failed()) {
            BGN_LOG_WARN("Engine", "capture ended: {}", s.capture->error().empty() ? "window closed" : s.capture->error());
            ++s.captureFailures;
        }
        endSession(s);
        s.sessionRetryAt = now + 0.5;
    }
    if (!s.sessionReady) {
        if (now < s.sessionRetryAt) return idle("Reconnecting capture", 100);
        s.target = target;
        s.wantHdrCapture = wantHdr;
        bool useDup = cfg.captureBackend == CaptureBackend::DesktopDuplication ||
                      (cfg.captureBackend == CaptureBackend::Auto && (!WgcCapture::isSupported() || s.captureFailures >= 3));
        CaptureOptions copt;
        copt.hdr = wantHdr;
        copt.cursor = false;
        if (useDup) s.capture = std::make_unique<DuplicationCapture>();
        else s.capture = std::make_unique<WgcCapture>();
        if (!s.capture->start(s.device, target.hwnd, copt)) {
            std::string err = s.capture->error();
            BGN_LOG_ERROR("Engine", "capture start failed: {}", err);
            s.capture.reset();
            ++s.captureFailures;
            s.sessionRetryAt = now + std::min(10.0, 1.0 + s.captureFailures);
            std::lock_guard lock(mutex_);
            stats_.lastError = err;
            stats_.state = "Capture failed - retrying";
            return;
        }
        s.usingDuplication = useDup;
        if (!s.presenter.create(s.device, useDup)) {
            s.capture->stop();
            s.capture.reset();
            s.sessionRetryAt = now + 2.0;
            return;
        }
        s.sessionReady = true;
        s.pipeline.resetHistory();
        s.contentTracker.reset();
        s.quality.reset();
        s.lastFrameArrival = 0;
        s.inRate.reset();
        s.outRate.reset();
        BGN_LOG_INFO("Engine", "session started for '{}' via {}", target.gameName.empty() ? "GeForce NOW" : target.gameName, s.capture->name());
    }

    // ---- Wait for a frame ----------------------------------------------------
    HANDLE ev = s.capture->frameEvent();
    if (ev) {
        HANDLE handles[2] = {ev, wake_.get()};
        MsgWaitForMultipleObjectsEx(2, handles, 50, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
    CapturedFrame frame;
    const bool got = s.capture->acquire(frame, ev ? 0 : 8);
    const double tNow = qpcSeconds();
    const bool focused = isFocused(target.hwnd, target.pid);
    const bool minimized = IsIconic(target.hwnd) != FALSE;
    const RECT client = clientScreenRect(target.hwnd);
    const bool wantVisible = focused && !minimized && rw(client) > 16 && rh(client) > 16;

    if (got) {
        ++s.captured;
        s.inRate.tick(tNow);
        // Frame arrival regularity (only while the stream is actually running)
        if (s.lastFrameArrival > 0 && wantVisible && s.inRate.rate(tNow) >= 20.0) s.quality.addFrameInterval((tNow - s.lastFrameArrival) * 1000.0);
        s.lastFrameArrival = tNow;
        s.hdrInput = frame.hdr;
        const int cropW = rw(frame.crop), cropH = rh(frame.crop);
        if (cropW < 16 || cropH < 16 || !wantVisible) {
            s.capture->releaseFrame();
            if (!wantVisible && s.wasVisible) {
                s.presenter.setVisible(false);
                s.cursor.deactivate();
                s.wasVisible = false;
            }
        } else {
            // Geometry / output plan
            s.plan = planOutput(s.cfg, client, s.mon);
            EffectiveConfig ec = resolveConfig(s.cfg.enhancement, s.automode.tier(), s.cfg.autoMode);
            {
                const StreamQuality q = s.quality.current();
                applyAdaptiveCleanup(ec, s.cfg.enhancement, q.valid ? q.blockiness : -1.0);
            }
            int streamH = 0;
            if (!s.plan.fullscreen && ec.upscaler != UpscalerKind::None) {
                if (s.cfg.streamResolution == StreamResolution::Auto) streamH = s.contentTracker.current();
                else streamH = streamHeightSetting(s.cfg.streamResolution);
                if (streamH >= int(cropH * 0.9)) streamH = 0;
            }
            PipelineGeometry geo;
            geo.cropW = cropW;
            geo.cropH = cropH;
            geo.streamH = streamH;
            geo.streamW = streamH ? (int(std::lround(double(streamH) * cropW / cropH)) & ~1) : 0;
            geo.outW = s.plan.outW;
            geo.outH = s.plan.outH;
            if (!(geo == s.pipeline.geometry())) {
                if (!s.pipeline.configure(geo)) {
                    s.capture->releaseFrame();
                    std::lock_guard lock(mutex_);
                    stats_.lastError = "Out of GPU memory - lowering quality";
                    s.automode.setFixedTier(std::max(0, s.automode.tier() - 2));
                    return;
                }
            }
            s.geo = geo;
            const bool hdrOut = s.mon.hdrEnabled && (s.cfg.enhancement.hdr.mode != TriState::Off || frame.hdr);
            PipelineFrameParams params;
            params.cfg = ec;
            params.hdrOutput = hdrOut;
            params.hdrPlus = hdrOut && !frame.hdr && s.cfg.enhancement.hdr.mode != TriState::Off;
            params.paperWhiteNits = s.cfg.enhancement.hdr.paperWhiteOverride > 0 ? s.cfg.enhancement.hdr.paperWhiteOverride : std::max(80.0f, s.mon.sdrWhiteNits);
            params.peakNits = s.cfg.enhancement.hdr.peakNitsOverride > 0 ? s.cfg.enhancement.hdr.peakNitsOverride
                                                                          : (s.mon.maxLuminance > 100 ? s.mon.maxLuminance : 1000.0f);
            params.compareSplit = s.cfg.compareSplit;
            params.splitPosition = 0.5f;
            params.colorVision = int(s.cfg.accessibility.colorVision);
            params.colorVisionStrength = s.cfg.accessibility.colorVisionStrength;
            params.nightLight = s.cfg.accessibility.nightLight;
            if (!s.wasVisible) s.pipeline.resetHistory();

            CaptureInput ci;
            ci.texture = frame.texture;
            ci.crop = frame.crop;
            ci.hdr = frame.hdr;
            const bool processed = s.pipeline.process(ci, params, &s.timer);
            s.capture->releaseFrame();
            if (processed) takePendingScreenshot(s, hdrOut, frame.hdr, params.paperWhiteNits);

            if (processed) {
                if (!s.presenter.configure(s.plan.overlay, hdrOut, s.cfg.lowLatency)) {
                    BGN_LOG_ERROR("Engine", "presenter configuration failed");
                    s.timer.endFrame(s.device.context());
                    endSession(s);
                    s.sessionRetryAt = tNow + 1.0;
                    return;
                }
                s.presenter.setVisible(true);
                s.wasVisible = true;

                // Cursor: confine + scale only when the output is larger than the source window
                const bool scaledOutput = s.plan.fullscreen && (rw(client) < s.mon.width || rh(client) < s.mon.height);
                if (scaledOutput) {
                    s.cursor.activate(client);
                    s.cursor.tick();
                    if (!s.cursorCaptured) {
                        s.capture->setCursorCapture(true);
                        s.cursorCaptured = true;
                    }
                } else if (s.cursor.active() || s.cursorCaptured) {
                    s.cursor.deactivate();
                    s.capture->setCursorCapture(false);
                    s.cursorCaptured = false;
                }

                const PacingPlan pacing = computePacing(s.inRate.rate(tNow), s.mon.refreshHz, s.automode.frameGen());
                HRESULT hr = S_OK;
                const int bbW = s.presenter.width(), bbH = s.presenter.height();
                if (pacing.useInterpolation && s.pipeline.interpolate(&s.timer)) {
                    s.presenter.waitForFrameSlot(20);
                    s.pipeline.present(s.presenter.backBufferRtv(), bbW, bbH, s.plan.dst, true, s.presented * 2 + 1, false);
                    hr = s.presenter.present(UINT(pacing.syncIntervalMid));
                    ++s.interpolated;
                    s.outRate.tick(qpcSeconds());
                    if (SUCCEEDED(hr)) {
                        s.presenter.waitForFrameSlot(40);
                        s.pipeline.present(s.presenter.backBufferRtv(), bbW, bbH, s.plan.dst, false, s.presented * 2 + 2, false);
                        s.timer.mark(s.device.context(), GpuStage::Present);
                        hr = s.presenter.present(UINT(pacing.syncIntervalReal));
                    }
                } else {
                    s.presenter.waitForFrameSlot(s.cfg.lowLatency ? 4 : 20);
                    s.pipeline.present(s.presenter.backBufferRtv(), bbW, bbH, s.plan.dst, false, s.presented * 2, false);
                    s.timer.mark(s.device.context(), GpuStage::Present);
                    hr = s.presenter.present(s.cfg.lowLatency ? 0 : 1);
                }
                s.timer.endFrame(s.device.context());
                const double tPresent = qpcSeconds();
                ++s.presented;
                s.outRate.tick(tPresent);
                if (s.lastPresent > 0) s.frameInterval.add((tPresent - s.lastPresent) * 1000.0);
                s.lastPresent = tPresent;
                if (frame.timestamp > 0 && tPresent > frame.timestamp && tPresent - frame.timestamp < 1.0) s.latency.add((tPresent - frame.timestamp) * 1000.0);
                if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || s.device.isRemoved()) {
                    BGN_LOG_ERROR("Engine", "GPU device lost ({}), recovering", hrToString(s.device.removedReason()));
                    destroyDevice(s);
                    s.deviceRetryAt = tPresent + 1.0;
                    return;
                }
                if (tPresent - s.lastTopmost > 1.0) {
                    s.presenter.reassertTopmost();
                    s.lastTopmost = tPresent;
                }
            } else {
                s.timer.endFrame(s.device.context());
            }
        }
    } else if (!wantVisible && s.wasVisible) {
        s.presenter.setVisible(false);
        s.cursor.deactivate();
        s.wasVisible = false;
    }

    // ---- Timings, analysis, Auto Mode ------------------------------------------
    if (s.deviceReady) {
        GpuFrameTiming t;
        while (s.timer.collect(s.device.context(), t)) {
            s.gpuMs.add(t.totalMs);
            for (int i = 0; i < kGpuStageCount; ++i) s.stageMs[i].add(t.stageMs[i]);
            if (t.stageMs[int(GpuStage::Interpolate)] > 0) s.fgMs.add(t.stageMs[int(GpuStage::Interpolate)]);
        }
        s.pipeline.pollReadbacks();
        double qb = 0, qi = 0;
        if (s.pipeline.takeQualityMeasurement(qb, qi)) s.quality.addBlockinessSample(qb, qi);
        ContentResMeasurement cm;
        if (s.pipeline.takeContentMeasurement(cm)) {
            double factor = estimateUpscaleFactor(cm);
            int h = streamHeightForFactor(s.geo.cropH, factor);
            int before = s.contentTracker.current();
            int after = s.contentTracker.push(h);
            if (after != before) BGN_LOG_INFO("Engine", "detected stream resolution: {} (factor {:.2f})", after ? std::to_string(after) + "p" : "native", factor);
        }
    }
    const double tEnd = qpcSeconds();
    if (tEnd - s.lastAuto >= 0.5 && s.wasVisible) {
        s.lastAuto = tEnd;
        AutoInputs in;
        in.nowSeconds = tEnd;
        in.inputFps = s.inRate.rate(tEnd);
        in.refreshHz = s.mon.refreshHz;
        in.gpuMsAvg = s.gpuMs.mean();
        in.gpuMsP95 = s.gpuMs.percentile(0.95);
        in.frameGenActive = s.automode.frameGen();
        in.frameGenMs = s.fgMs.mean();
        in.gpuUtilization = sys.gpuUsage;
        in.cpuUtilization = sys.cpuUsage;
        double usage = 0, budget = 0;
        if (s.device.queryVideoMemory(usage, budget)) {
            in.vramUsageMB = usage;
            in.vramBudgetMB = budget;
        }
        in.addedLatencyMs = s.latency.mean();
        uint64_t dropped = s.capture ? s.capture->droppedFrames() : 0;
        in.droppedFramesDelta = int(dropped - s.lastDroppedTotal);
        s.lastDroppedTotal = dropped;
        AutoDecision d = s.automode.update(in);
        s.lastDecision = d;
        if (d.changed && !d.reason.empty()) {
            s.autoReason = d.reason;
            BGN_LOG_INFO("AutoMode", "{}", d.reason);
            s.gpuMs.clear(); // measure the new configuration fresh
        }
    }

    // ---- Publish stats ---------------------------------------------------------
    if (tEnd - s.lastPublish >= 0.25) {
        s.lastPublish = tEnd;
        if (tEnd - s.lastHistory >= 0.25) {
            s.lastHistory = tEnd;
            auto push = [](std::deque<float>& q, float v) {
                q.push_back(v);
                while (q.size() > 240) q.pop_front();
            };
            push(s.histGpu, float(s.gpuMs.mean()));
            push(s.histIn, float(s.inRate.rate(tEnd)));
            push(s.histOut, float(s.outRate.rate(tEnd)));
            const StreamQuality q = s.quality.current();
            if (q.valid && s.wasVisible) push(s.histQuality, float(q.score));
        }
        const GpuInfo& gi = s.device.info();
        st.state = s.wasVisible ? "Enhancing" : (s.sessionReady ? "Ready (GeForce NOW not in focus)" : "Starting");
        st.active = s.sessionReady;
        st.overlayVisible = s.wasVisible;
        st.captureBackend = s.capture ? s.capture->name() : "";
        st.presentPath = s.presenter.usesComposition() ? "DirectComposition flip model" : "Flip model swap chain";
        st.gpuName = gi.name;
        st.gpuFamily = gi.family;
        st.driverVersion = gi.driverVersion;
        st.vramTotalMB = gi.dedicatedVramMB;
        st.halfPrecision = gi.halfPrecision;
        st.softwareGpu = gi.software;
        st.inputFps = s.inRate.rate(tEnd);
        st.captureFps = st.inputFps;
        st.outputFps = s.outRate.rate(tEnd);
        const PipelineStatus& ps = s.pipeline.status();
        st.captureW = s.geo.cropW;
        st.captureH = s.geo.cropH;
        st.procW = ps.inW;
        st.procH = ps.inH;
        st.outW = ps.outW;
        st.outH = ps.outH;
        st.displayW = rw(s.plan.overlay);
        st.displayH = rh(s.plan.overlay);
        st.gpuMsAvg = s.gpuMs.mean();
        st.gpuMsMax = s.gpuMs.max();
        st.gpuMsP95 = s.gpuMs.percentile(0.95);
        st.frameTimeMs = s.frameInterval.mean();
        st.addedLatencyMs = s.latency.mean();
        for (int i = 0; i < kGpuStageCount; ++i) st.stageMs[i] = s.stageMs[i].mean();
        st.capturedFrames = s.captured;
        st.droppedFrames = s.capture ? s.capture->droppedFrames() : 0;
        st.presentedFrames = s.presented;
        st.interpolatedFrames = s.interpolated;
        st.tier = s.automode.tier();
        st.tierName = tierName(st.tier);
        EffectiveConfig ec = resolveConfig(s.cfg.enhancement, st.tier, s.cfg.autoMode);
        st.upscaler = ps.upscalerUsed;
        st.upscalerReduced = ec.upscalerReduced;
        st.frameGenActive = s.automode.frameGen() && s.interpolated > 0;
        st.frameGenBeneficial = s.lastDecision.frameGenBeneficial;
        st.frameGenMode = s.cfg.safeMode ? FrameGenMode::Off : s.cfg.enhancement.frameGen;
        st.autoReason = s.autoReason;
        st.budgetMs = s.lastDecision.budgetMs;
        double usage = 0, budget = 0;
        if (s.device.queryVideoMemory(usage, budget)) {
            st.vramUsageMB = usage;
            st.vramBudgetMB = budget;
        }
        st.pipelineVramMB = double(ps.vramBytes) / (1024.0 * 1024.0);
        st.gpuUtil = sys.gpuUsage;
        st.cpuUtil = sys.cpuUsage;
        st.hdrOutput = s.presenter.hdr();
        st.hdrInput = s.hdrInput;
        st.hdrPlus = st.hdrOutput && !s.hdrInput && s.cfg.enhancement.hdr.mode != TriState::Off;
        st.refreshHz = s.mon.refreshHz;
        st.monitorName = s.mon.friendlyName.empty() ? s.mon.deviceName : s.mon.friendlyName;
        st.outputModeUsed = s.plan.fullscreen ? "Fullscreen (upscaled to monitor)" : "Match GFN window";
        st.detectedStreamHeight = s.geo.streamH;
        st.contentFactor = ps.contentFactor;
        st.cursorConfined = s.cursor.active();
        st.gpuMsHistory.assign(s.histGpu.begin(), s.histGpu.end());
        st.inputFpsHistory.assign(s.histIn.begin(), s.histIn.end());
        st.outputFpsHistory.assign(s.histOut.begin(), s.histOut.end());
        st.qualityHistory.assign(s.histQuality.begin(), s.histQuality.end());
        {
            const StreamQuality q = s.quality.current();
            st.streamQualityValid = q.valid;
            st.streamQuality = q.score;
            st.blockiness = q.blockiness;
            st.stutter = q.stutter;
            EffectiveConfig adj = ec;
            applyAdaptiveCleanup(adj, s.cfg.enhancement, q.valid ? q.blockiness : -1.0);
            st.adaptiveCleanupActive = adj.deblock > ec.deblock + 1e-4f || adj.denoise > ec.denoise + 1e-4f;
        }
        st.onBattery = sys.onBattery;
        st.batterySaverActive = s.batteryCap < kMaxTier;
        {
            std::lock_guard lock(mutex_);
            st.lastError = stats_.lastError;
            st.screenshotsSaved = stats_.screenshotsSaved;
            st.lastScreenshot = stats_.lastScreenshot;
            st.screenshotError = stats_.screenshotError;
            stats_ = st;
        }
    }
}

} // namespace bgn
