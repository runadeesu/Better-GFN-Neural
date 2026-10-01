#pragma once
// Game name extraction from GeForce NOW window titles. Portable.

#include <string>
#include <string_view>

namespace bgn {

// "Cyberpunk 2077® on GeForce NOW" -> "Cyberpunk 2077"
// Removes trademark symbols, GFN/browser suffixes and redundant whitespace.
std::string cleanGameTitle(std::string_view windowTitle);

// "Cyberpunk 2077" -> "cyberpunk2077" (lower-case ASCII alphanumerics; other
// UTF-8 characters are kept as-is so Japanese titles still produce unique keys).
std::string gameKey(std::string_view displayName);

// True if a title is just the GeForce NOW launcher (no game stream).
bool isLauncherTitle(std::string_view windowTitle);

} // namespace bgn
