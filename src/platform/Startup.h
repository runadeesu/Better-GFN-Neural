#pragma once
// "Start with Windows" via the per-user Run registry key (no admin rights).

#include <filesystem>

namespace bgn {

bool isStartWithWindowsEnabled();
bool setStartWithWindows(bool enable, const std::filesystem::path& exePath);

} // namespace bgn
