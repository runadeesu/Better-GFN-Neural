#include "app/CommandLine.h"

#include <cstdlib>

namespace bgn {

CommandLine parseCommandLine(const std::vector<std::string>& args) {
    CommandLine c;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto value = [&](std::string& out) {
            if (i + 1 < args.size()) out = args[++i];
        };
        if (a == "--background" || a == "/background") c.background = true;
        else if (a == "--selftest") c.selfTest = true;
        else if (a == "--benchmark") c.benchmark = true;
        else if (a == "--portable") c.portable = true;
        else if (a == "--reset-settings") c.resetSettings = true;
        else if (a == "--simulate-crash") c.simulateCrash = true;
        else if (a == "--warp") c.forceWarp = true;
        else if (a == "--no-ui") c.noUi = true;
        else if (a == "--automation") value(c.automation);
        else if (a == "--data-dir") value(c.dataDir);
        else if (a == "--log-level") value(c.logLevel);
        else if (a == "--output") value(c.outputJson);
        else if (a == "--screenshot-at") {
            std::string v;
            value(v);
            c.screenshotAt = std::atof(v.c_str());
        } else if (a == "--seconds") {
            std::string v;
            value(v);
            c.automationSeconds = std::atof(v.c_str());
        } else c.unknown.push_back(a);
    }
    return c;
}

} // namespace bgn
