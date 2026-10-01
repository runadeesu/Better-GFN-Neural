#include "profiles/GameTitle.h"

#include <array>
#include <cctype>

#include "core/StringUtil.h"

namespace bgn {

namespace {

// UTF-8 sequences removed from titles.
constexpr std::array<std::string_view, 8> kStripSequences = {
    "\xC2\xAE",     // ® REGISTERED SIGN
    "\xE2\x84\xA2", // ™ TRADE MARK SIGN
    "\xC2\xA9",     // © COPYRIGHT SIGN
    "\xE2\x84\xA0", // ℠ SERVICE MARK
    "\xE2\x80\x8B", // zero width space
    "\xE2\x80\x8E", // LRM
    "\xE2\x80\x8F", // RLM
    "\xEF\xBB\xBF", // BOM
};

// Suffixes appended by GFN or by browsers (checked case-insensitively, repeatedly).
constexpr std::array<std::string_view, 16> kSuffixes = {
    " - google chrome", " - microsoft edge", " - mozilla firefox", " - opera", " - brave", " \xE2\x80\x94 mozilla firefox",
    " on geforce now",  " - geforce now",    " | geforce now",     " \xE2\x80\x93 geforce now", " \xE2\x80\x94 geforce now",
    " (geforce now)",   " | nvidia geforce now", " - nvidia geforce now", " on nvidia geforce now", " geforce now",
};

std::string collapseSpaces(std::string_view s) {
    std::string out;
    bool space = false;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        // NBSP (C2 A0) => space
        if (c == 0xC2 && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xA0) {
            space = true;
            ++i;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            space = true;
            continue;
        }
        if (space && !out.empty()) out.push_back(' ');
        space = false;
        out.push_back(static_cast<char>(c));
    }
    return out;
}

} // namespace

bool isLauncherTitle(std::string_view title) {
    std::string t = toLowerAscii(collapseSpaces(title));
    return t.empty() || t == "geforce now" || t == "nvidia geforce now" || t == "geforcenow" || t == "geforce now - google chrome" ||
           t == "geforce now - microsoft edge" || t == "geforce now \xE2\x80\x94 mozilla firefox" || t == "geforce now - mozilla firefox";
}

std::string cleanGameTitle(std::string_view windowTitle) {
    std::string s(windowTitle);
    for (auto seq : kStripSequences) s = replaceAll(s, seq, "");
    s = collapseSpaces(s);
    bool changed = true;
    while (changed) {
        changed = false;
        std::string lower = toLowerAscii(s);
        for (auto suf : kSuffixes) {
            if (lower.size() > suf.size() && endsWith(lower, suf)) {
                s.resize(s.size() - suf.size());
                s = trim(s);
                changed = true;
                break;
            }
        }
    }
    // Leading "GeForce NOW - " prefix (seen in some builds/browser tabs)
    for (std::string_view pre : {std::string_view("geforce now - "), std::string_view("geforce now: "), std::string_view("geforce now | ")}) {
        if (toLowerAscii(s).rfind(pre, 0) == 0 && s.size() > pre.size()) s = trim(s.substr(pre.size()));
    }
    s = trim(s);
    // Strip surrounding punctuation leftovers
    while (!s.empty() && (s.back() == '-' || s.back() == '|' || s.back() == ':')) s = trim(s.substr(0, s.size() - 1));
    return s;
}

std::string gameKey(std::string_view displayName) {
    std::string key;
    for (size_t i = 0; i < displayName.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(displayName[i]);
        if (c < 0x80) {
            if (std::isalnum(c)) key.push_back(static_cast<char>(std::tolower(c)));
        } else {
            key.push_back(static_cast<char>(c)); // keep multi-byte UTF-8 as-is
        }
    }
    return key;
}

} // namespace bgn
