#include "core/StringUtil.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>

namespace bgn {

std::u16string utf8ToUtf16(std::string_view in) {
    std::u16string out;
    out.reserve(in.size());
    size_t i = 0;
    while (i < in.size()) {
        uint32_t c = static_cast<unsigned char>(in[i]);
        uint32_t cp = 0xFFFD;
        size_t len = 1;
        if (c < 0x80) {
            cp = c;
        } else if ((c >> 5) == 0x6 && i + 1 < in.size()) {
            cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(in[i + 1]) & 0x3F);
            len = 2;
        } else if ((c >> 4) == 0xE && i + 2 < in.size()) {
            cp = ((c & 0x0F) << 12) | ((static_cast<unsigned char>(in[i + 1]) & 0x3F) << 6) | (static_cast<unsigned char>(in[i + 2]) & 0x3F);
            len = 3;
        } else if ((c >> 3) == 0x1E && i + 3 < in.size()) {
            cp = ((c & 0x07) << 18) | ((static_cast<unsigned char>(in[i + 1]) & 0x3F) << 12) |
                 ((static_cast<unsigned char>(in[i + 2]) & 0x3F) << 6) | (static_cast<unsigned char>(in[i + 3]) & 0x3F);
            len = 4;
        }
        i += len;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(cp));
        }
    }
    return out;
}

std::string utf16ToUtf8(std::u16string_view in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        uint32_t cp = in[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < in.size() && in[i + 1] >= 0xDC00 && in[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (in[i + 1] - 0xDC00);
            ++i;
        } else if (cp >= 0xD800 && cp <= 0xDFFF) {
            cp = 0xFFFD;
        }
        if (cp < 0x80) {
            out.push_back(char(cp));
        } else if (cp < 0x800) {
            out.push_back(char(0xC0 | (cp >> 6)));
            out.push_back(char(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(char(0xE0 | (cp >> 12)));
            out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(char(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(char(0xF0 | (cp >> 18)));
            out.push_back(char(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(char(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

#ifdef _WIN32
std::wstring widen(std::string_view utf8) {
    std::u16string s = utf8ToUtf16(utf8);
    return std::wstring(s.begin(), s.end());
}
std::string narrow(std::wstring_view wide) {
    std::u16string s(wide.begin(), wide.end());
    return utf16ToUtf8(s);
}
#endif

std::string toLowerAscii(std::string_view s) {
    std::string r(s);
    std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return r;
}

std::string trim(std::string_view s) {
    size_t b = 0, e = s.size();
    while (b < e && (std::isspace(static_cast<unsigned char>(s[b])))) ++b;
    while (e > b && (std::isspace(static_cast<unsigned char>(s[e - 1])))) --e;
    return std::string(s.substr(b, e - b));
}

bool startsWith(std::string_view s, std::string_view p) { return s.size() >= p.size() && s.substr(0, p.size()) == p; }
bool endsWith(std::string_view s, std::string_view p) { return s.size() >= p.size() && s.substr(s.size() - p.size()) == p; }

bool iequalsAscii(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

bool icontainsAscii(std::string_view h, std::string_view n) {
    if (n.empty()) return true;
    return toLowerAscii(h).find(toLowerAscii(n)) != std::string::npos;
}

std::string replaceAll(std::string s, std::string_view from, std::string_view to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::vector<std::string> split(std::string_view s, char delim) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == delim) {
            out.emplace_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

std::string formatBytes(double bytes) {
    char buf[64];
    if (bytes >= 1024.0 * 1024.0 * 1024.0) std::snprintf(buf, sizeof(buf), "%.1f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    else if (bytes >= 1024.0 * 1024.0) std::snprintf(buf, sizeof(buf), "%.0f MB", bytes / (1024.0 * 1024.0));
    else std::snprintf(buf, sizeof(buf), "%.0f KB", bytes / 1024.0);
    return buf;
}

} // namespace bgn
