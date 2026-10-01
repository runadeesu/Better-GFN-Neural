#pragma once
// Portable string helpers (UTF-8 <-> UTF-16, case folding, trimming).

#include <string>
#include <string_view>
#include <vector>

namespace bgn {

std::u16string utf8ToUtf16(std::string_view utf8);
std::string utf16ToUtf8(std::u16string_view utf16);

#ifdef _WIN32
std::wstring widen(std::string_view utf8);
std::string narrow(std::wstring_view wide);
#endif

std::string toLowerAscii(std::string_view s);
std::string trim(std::string_view s);
bool startsWith(std::string_view s, std::string_view prefix);
bool endsWith(std::string_view s, std::string_view suffix);
bool iequalsAscii(std::string_view a, std::string_view b);
bool icontainsAscii(std::string_view haystack, std::string_view needle);
std::string replaceAll(std::string s, std::string_view from, std::string_view to);
std::vector<std::string> split(std::string_view s, char delim);
std::string formatBytes(double bytes);

} // namespace bgn
