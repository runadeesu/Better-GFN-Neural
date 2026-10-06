#include "telemetry/OsdText.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace bgn {

std::vector<std::string> formatOsd(const OsdData& d) {
    char a[64], b[64], c[64];
    std::snprintf(a, sizeof(a), "FPS %3.0f > %3.0f%s", d.inputFps, d.outputFps, d.frameGen ? " 2x" : "");
    std::snprintf(b, sizeof(b), "GPU %4.1fms  LAT +%.0fms", d.gpuMs, d.addedLatencyMs);
    if (d.streamQuality >= 0) std::snprintf(c, sizeof(c), "Q%.0f T%d %s", d.streamQuality, d.tier, d.upscaler.c_str());
    else std::snprintf(c, sizeof(c), "T%d %s", d.tier, d.upscaler.c_str());
    std::vector<std::string> lines{a, b, c};
    if (d.concealed > 0) {
        char e[48];
        std::snprintf(e, sizeof(e), "SMOOTHED %llu", static_cast<unsigned long long>(d.concealed));
        lines.emplace_back(e);
    }
    for (auto& l : lines) {
        for (char& ch : l)
            if (static_cast<unsigned char>(ch) < 32 || static_cast<unsigned char>(ch) > 126) ch = '?';
        if (int(l.size()) > kOsdMaxCols) l.resize(kOsdMaxCols);
    }
    return lines;
}

void packOsdText(const std::vector<std::string>& lines, int& cols, int& rows, uint32_t* words) {
    cols = 0;
    for (const auto& l : lines) cols = std::max(cols, int(std::min<size_t>(l.size(), kOsdMaxCols)));
    rows = cols > 0 ? std::min(int(lines.size()), kOsdMaxChars / cols) : 0;
    char grid[kOsdMaxChars];
    std::memset(grid, ' ', sizeof(grid));
    for (int r = 0; r < rows; ++r)
        for (int x = 0; x < cols && x < int(lines[size_t(r)].size()); ++x) grid[r * cols + x] = lines[size_t(r)][size_t(x)];
    for (int i = 0; i < kOsdMaxChars / 4; ++i)
        words[i] = uint32_t(uint8_t(grid[i * 4])) | (uint32_t(uint8_t(grid[i * 4 + 1])) << 8) | (uint32_t(uint8_t(grid[i * 4 + 2])) << 16) |
                   (uint32_t(uint8_t(grid[i * 4 + 3])) << 24);
}

} // namespace bgn
