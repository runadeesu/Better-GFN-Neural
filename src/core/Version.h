#pragma once
// Single source of truth for the application version (CMake parses this file).

namespace bgn {
inline constexpr const char* kVersionString = "1.1.0";
inline constexpr int kVersionMajor = 1;
inline constexpr int kVersionMinor = 0;
inline constexpr int kVersionPatch = 0;
inline constexpr const char* kAppName = "Better GFN Neural";
} // namespace bgn
