#pragma once
// UI localization: English source strings with a Japanese table.
//
// Languages: English, Japanese, and Bilingual (Japanese UI with the English
// original shown next to headings and setting names).

#include <format>
#include <string>
#include <string_view>
#include <unordered_map>

namespace bgn {

enum class UiLanguage { English, Japanese, Bilingual };

void setUiLanguage(UiLanguage lang);
UiLanguage uiLanguage();
// Setting values: "auto", "en", "ja", "ja+en". "auto" uses the Windows UI
// language: Japanese Windows => Bilingual, anything else => English.
UiLanguage resolveLanguage(const std::string& setting);

// Returns the translation of an English UI string (or the string itself).
const char* tr(const char* english);

// Translates text composed at runtime by non-UI code (engine state, Auto Mode
// decisions, capture errors, benchmark progress). Unknown text is returned as is.
std::string trText(std::string_view english);

// Bilingual mode only: the English original of a string returned by tr(),
// or nullptr when there is nothing to add.
const char* englishFor(const char* shown);

// Translated std::format. Falls back to the English format string if a
// translation's placeholders do not match.
template <class... Args>
std::string trf(const char* englishFmt, Args&&... args) {
    try {
        return std::vformat(tr(englishFmt), std::make_format_args(args...));
    } catch (const std::format_error&) {
        return std::vformat(englishFmt, std::make_format_args(args...));
    }
}

// The Japanese table (for tests).
const std::unordered_map<std::string_view, const char*>& japaneseTable();

} // namespace bgn
