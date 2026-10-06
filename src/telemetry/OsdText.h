#pragma once
// On-screen display text (portable): what the in-game OSD shows and how the
// text is packed into the constant buffer read by shaders/present.hlsl.

#include <cstdint>
#include <string>
#include <vector>

namespace bgn {

struct OsdData {
    double inputFps = 0, outputFps = 0;
    double gpuMs = 0, addedLatencyMs = 0;
    double streamQuality = -1; // -1 = unknown
    int tier = 0;
    std::string upscaler;      // short name, e.g. "NSR-S"
    bool frameGen = false;
    uint64_t concealed = 0;    // late frames filled by stutter smoothing
};

// ASCII lines, at most kOsdMaxCols characters each.
std::vector<std::string> formatOsd(const OsdData& d);

constexpr int kOsdMaxCols = 32;
constexpr int kOsdMaxChars = 192; // = BGN_OSD_MAX_CHARS

// Packs the lines into a rows x cols grid (padded with spaces), 4 chars per
// uint32 little endian. |words| receives kOsdMaxChars / 4 values.
void packOsdText(const std::vector<std::string>& lines, int& cols, int& rows, uint32_t* words);

} // namespace bgn
