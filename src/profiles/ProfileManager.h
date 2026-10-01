#pragma once
// Per-game profiles: built-in templates for popular titles plus user profiles
// stored in Settings::profiles. Portable.

#include <optional>
#include <string>
#include <vector>

#include "settings/Settings.h"

namespace bgn {

struct ResolvedProfile {
    std::string key;
    std::string displayName;
    bool fromProfile = false; // false: global settings
    Preset preset = Preset::Auto;
    PerformancePriority priority = PerformancePriority::Balanced;
    EnhancementSettings enhancement;
};

class ProfileManager {
public:
    // Built-in template for a game key, if any ("cyberpunk2077", "fortnite", ...).
    static std::optional<GameProfile> builtinTemplate(const std::string& key, const std::string& displayName);
    static std::vector<std::string> builtinNames();

    // Finds the profile key used for |gameKey| (exact match or alias prefix match
    // against existing profiles, e.g. "callofdutyblackops6" -> "callofduty").
    static std::string matchKey(const Settings& s, const std::string& gameKey);

    // Ensures a profile exists for the detected game, seeding it from a built-in
    // template when available. Returns the key of the profile. Updates play stats.
    static std::string onGameDetected(Settings& s, const std::string& displayName, int64_t nowUnix);

    // Effective settings for a game (or global settings if |gameKey| is empty).
    static ResolvedProfile resolve(const Settings& s, const std::string& gameKey);
};

} // namespace bgn
