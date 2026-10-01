#include "automode/AutoModeController.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace bgn {

namespace {
// Approximate relative GPU cost of each tier (measured on reference hardware
// with the benchmark; only the ratios matter). Used to predict whether the
// next tier fits the budget before trying it.
constexpr double kRelativeCost[kTierCount] = {1.0, 1.35, 2.1, 3.3, 4.1, 7.2, 8.6};
constexpr double kUpdateMinGap = 1.0;
} // namespace

AutoModeController::AutoModeController() {
    for (double& t : lastDowngradeAt_) t = -1e9;
}

double AutoModeController::gpuBudgetMs(double inputFps, double fraction) {
    double fps = inputFps > 1.0 ? inputFps : 60.0;
    return 1000.0 / fps * fraction;
}

bool AutoModeController::frameGenBeneficial(double inputFps, double refreshHz) {
    return inputFps >= 20.0 && refreshHz >= inputFps * 1.8;
}

double AutoModeController::frameGenHoldbackMs(double inputFps) {
    double fps = inputFps > 1.0 ? inputFps : 60.0;
    return 500.0 / fps;
}

void AutoModeController::configure(const PresetPolicy& policy, FrameGenMode fgMode, bool autoMode, int initialTier) {
    policy_ = policy;
    fgMode_ = fgMode;
    autoMode_ = autoMode;
    tier_ = std::clamp(initialTier, policy_.minTier, policy_.maxTier);
    frameGen_ = false;
    overSince_ = underSince_ = -1;
    pendingFgOn_ = 0;
    lastChange_ = -1e9;
    lastFgChange_ = -1e9;
    for (double& t : lastDowngradeAt_) t = -1e9;
}

void AutoModeController::setFixedTier(int tier) {
    autoMode_ = false;
    policy_.startTier = std::clamp(tier, 0, kMaxTier);
    policy_.maxTier = policy_.startTier;
    tier_ = policy_.startTier;
}

void AutoModeController::step(int delta, double now, const char* why, AutoDecision& d) {
    int ceiling = policy_.maxTier;
    int nt = std::clamp(tier_ + delta, policy_.minTier, ceiling);
    if (nt == tier_) return;
    if (nt < tier_) lastDowngradeAt_[tier_] = now;
    d.reason = std::format("{} ({} -> {})", why, tierName(tier_), tierName(nt));
    tier_ = nt;
    lastChange_ = now;
    overSince_ = underSince_ = -1;
    d.changed = true;
}

AutoDecision AutoModeController::update(const AutoInputs& in) {
    AutoDecision d;
    const double now = in.nowSeconds;
    const double budget = gpuBudgetMs(in.inputFps, policy_.gpuBudgetFraction);
    d.budgetMs = budget;
    const double cost = std::max(in.gpuMsP95, in.gpuMsAvg);
    const bool vramKnown = in.vramBudgetMB > 0;
    const double vramRatio = vramKnown ? in.vramUsageMB / in.vramBudgetMB : 0.0;
    const bool gpuSaturated = in.gpuUtilization >= 0.97;

    // ---- 1. Safety governor (always active, also with Auto Mode off) -------
    bool handled = false;
    if (vramKnown && vramRatio > 0.92 && tier_ > policy_.minTier && now - lastChange_ >= 0.5) {
        if (frameGen_) {
            frameGen_ = false;
            lastFgChange_ = now;
            d.reason = "VRAM pressure: frame interpolation paused";
            d.changed = true;
        } else {
            step(-1, now, "VRAM pressure", d);
        }
        handled = true;
    } else if (cost > budget * 1.6 && now - lastChange_ >= 0.5) {
        if (frameGen_ && fgMode_ == FrameGenMode::Auto) {
            frameGen_ = false;
            lastFgChange_ = now;
            d.reason = "GPU overloaded: frame interpolation paused";
            d.changed = true;
        } else {
            step(-2, now, "GPU overloaded", d);
        }
        handled = true;
    } else if (cost > budget || (in.droppedFramesDelta > 2 && gpuSaturated)) {
        if (overSince_ < 0) overSince_ = now;
        if (now - overSince_ >= 1.0 && now - lastChange_ >= kUpdateMinGap) {
            if (frameGen_ && fgMode_ == FrameGenMode::Auto) {
                frameGen_ = false;
                lastFgChange_ = now;
                overSince_ = -1;
                d.reason = "Over GPU budget: frame interpolation paused";
                d.changed = true;
            } else {
                step(-1, now, "Over GPU budget", d);
            }
        }
        handled = true;
    } else {
        overSince_ = -1;
    }

    // ---- 2. Upgrade when there is sustained headroom -----------------------
    const int ceiling = policy_.maxTier;
    if (!handled && tier_ < ceiling) {
        const double predicted = cost * kRelativeCost[tier_ + 1] / kRelativeCost[tier_];
        const bool headroom = predicted < budget * 0.85 && (in.gpuUtilization < 0 || in.gpuUtilization < 0.9) && (!vramKnown || vramRatio < 0.8) &&
                              in.droppedFramesDelta <= 1;
        if (headroom) {
            if (underSince_ < 0) underSince_ = now;
            // Avoid oscillation: a tier we recently had to leave needs a longer proof period
            const double required = (now - lastDowngradeAt_[tier_ + 1] < 30.0) ? 12.0 : 4.0;
            if (now - underSince_ >= required && now - lastChange_ >= 3.0) step(+1, now, "GPU headroom", d);
        } else {
            underSince_ = -1;
        }
    }

    // ---- 3. Frame interpolation ---------------------------------------------
    const bool beneficial = frameGenBeneficial(in.inputFps, in.refreshHz);
    d.frameGenBeneficial = beneficial;
    const double holdback = frameGenHoldbackMs(in.inputFps);
    const double fgCost = in.frameGenActive && in.frameGenMs > 0 ? in.frameGenMs : std::max(0.3, cost * 0.25);
    const double baseCost = in.frameGenActive ? std::max(0.0, cost - in.frameGenMs) : cost;
    bool want = false;
    switch (fgMode_) {
    case FrameGenMode::Off: want = false; break;
    case FrameGenMode::X2:
        // Forced on whenever the display can show the extra frames; dropped only
        // if latency grows far beyond the budget.
        want = beneficial && (in.addedLatencyMs <= policy_.latencyBudgetMs * 2.0 || !in.frameGenActive);
        break;
    case FrameGenMode::Auto:
        want = policy_.allowFrameGen && beneficial && in.inputFps <= 125.0 && holdback + baseCost + fgCost <= policy_.latencyBudgetMs &&
               baseCost + fgCost <= budget * 0.9 && (!vramKnown || vramRatio < 0.85);
        if (in.frameGenActive && in.addedLatencyMs > policy_.latencyBudgetMs * 1.15) want = false;
        break;
    }
    if (want && !frameGen_) {
        if (++pendingFgOn_ >= 3 && now - lastFgChange_ >= 3.0) {
            frameGen_ = true;
            lastFgChange_ = now;
            pendingFgOn_ = 0;
            if (d.reason.empty()) d.reason = "Frame interpolation enabled";
            d.changed = true;
        }
    } else if (!want && frameGen_) {
        frameGen_ = false;
        lastFgChange_ = now;
        pendingFgOn_ = 0;
        if (d.reason.empty()) d.reason = beneficial ? "Frame interpolation paused (latency/GPU budget)" : "Frame interpolation not beneficial at this refresh rate";
        d.changed = true;
    } else if (!want) {
        pendingFgOn_ = 0;
    }

    d.tier = tier_;
    d.frameGen = frameGen_;
    return d;
}

} // namespace bgn
