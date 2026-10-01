#pragma once
// Minimal UI localization: English source strings with a Japanese table.

#include <string>

namespace bgn {

enum class UiLanguage { English, Japanese };

void setUiLanguage(UiLanguage lang);
UiLanguage uiLanguage();
// Resolves "auto" using the Windows UI language.
UiLanguage resolveLanguage(const std::string& setting);

// Returns the translation of an English UI string (or the string itself).
const char* tr(const char* english);

} // namespace bgn
