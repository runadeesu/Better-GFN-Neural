#pragma once
// CPU image helpers for screenshots (portable).

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

namespace bgn {

struct Image8 {
    int w = 0, h = 0;
    std::vector<uint8_t> rgb; // w * h * 3
    bool empty() const { return w <= 0 || h <= 0 || rgb.empty(); }
};

// How the float pixels read back from the GPU are encoded.
enum class PixelEncoding {
    SdrGamma,    // sRGB gamma values 0..1 (SDR output, SDR working space)
    ScRgbLinear, // linear scRGB, 1.0 = 80 nits (HDR output)
    PqWorking,   // SMPTE ST 2084 (HDR working space)
};

// Converts float RGBA (4 floats per pixel) to 8-bit sRGB. HDR content is tone
// mapped so that |sdrWhiteNits| maps to white.
Image8 encodeToSdr8(const std::vector<float>& rgba, int w, int h, PixelEncoding enc, float sdrWhiteNits = 200.0f);

Image8 resizeBilinear(const Image8& src, int w, int h);

// Two images next to each other (left is scaled to the height of right),
// separated by a |gap| pixel wide divider.
Image8 sideBySide(const Image8& left, const Image8& right, int gap = 8);

bool writePng(const std::filesystem::path& path, const Image8& img);

// "Cyberpunk 2077_2026-10-06_21-15-03" (characters not allowed in Windows
// file names are replaced, length limited).
std::string screenshotBaseName(const std::string& game, const std::tm& localTime);

} // namespace bgn
