#include "gfn/GfnClassifier.h"

#include <algorithm>

#include "core/StringUtil.h"
#include "profiles/GameTitle.h"

namespace bgn {

const char* toString(GfnState s) {
    switch (s) {
    case GfnState::NotRunning: return "Waiting";
    case GfnState::Connected: return "Connected";
    case GfnState::Streaming: return "Enhancing";
    }
    return "?";
}

static bool contains(const std::vector<std::string>& v, const std::string& s) {
    for (const auto& x : v)
        if (iequalsAscii(x, s)) return true;
    return false;
}

GfnClassification classifyGfn(const std::vector<WindowSnapshot>& windows, const DetectionRules& rules) {
    GfnClassification result;
    struct Candidate {
        int index;
        int score;
        std::string game;
        bool browser;
    };
    std::vector<Candidate> streams;
    bool anyGfnProcess = false;

    for (int i = 0; i < int(windows.size()); ++i) {
        const WindowSnapshot& w = windows[i];
        bool isGfn = contains(rules.processNames, w.processName);
        bool isBrowser = !isGfn && rules.detectBrowser && contains(rules.browserProcessNames, w.processName) && icontainsAscii(w.title, "geforce now");
        if (!isGfn && !isBrowser) continue;
        if (isGfn) {
            anyGfnProcess = true;
            if (result.pid == 0) result.pid = w.pid;
        }
        if (!w.visible || w.cloaked) continue;
        if (isBrowser && result.state == GfnState::NotRunning) result.state = GfnState::Connected;

        const int cw = w.clientWidth > 0 ? w.clientWidth : w.width;
        const int ch = w.clientHeight > 0 ? w.clientHeight : w.height;
        const bool bigEnough = cw >= rules.minStreamWidth && ch >= rules.minStreamHeight;
        const bool launcher = isLauncherTitle(w.title);
        const bool coversMonitor = w.monitorWidth > 0 && w.width >= w.monitorWidth && w.height >= w.monitorHeight;

        if (!launcher && bigEnough) {
            std::string game = cleanGameTitle(w.title);
            if (!game.empty() && !isLauncherTitle(game)) {
                int score = 100 + (w.foreground ? 20 : 0) + (w.minimized ? -50 : 0) + (isBrowser ? -10 : 0);
                streams.push_back({i, score, game, isBrowser});
                continue;
            }
        }
        if (launcher && rules.fullscreenHeuristic && coversMonitor && !w.minimized && isGfn) {
            streams.push_back({i, 60 + (w.foreground ? 20 : 0), std::string(), false});
        }
    }

    if (anyGfnProcess && result.state == GfnState::NotRunning) result.state = GfnState::Connected;
    if (!streams.empty()) {
        auto best = std::max_element(streams.begin(), streams.end(), [](const Candidate& a, const Candidate& b) { return a.score < b.score; });
        result.state = GfnState::Streaming;
        result.streamIndex = best->index;
        result.gameName = best->game;
        result.fromBrowser = best->browser;
        result.pid = windows[best->index].pid;
    }
    return result;
}

} // namespace bgn
