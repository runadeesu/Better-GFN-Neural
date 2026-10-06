#include "profiles/ProfileManager.h"

#include "core/StringUtil.h"
#include "profiles/GameTitle.h"
#include "profiles/Omakase.h"

namespace bgn {

namespace {

struct Template {
    const char* key;
    const char* name;
    GameKind kind;
};

const Template kTemplates[] = {
    {"cyberpunk2077", "Cyberpunk 2077", GameKind::Cinematic},
    {"fortnite", "Fortnite", GameKind::Competitive},
    {"forzahorizon", "Forza Horizon", GameKind::Racing},
    {"callofduty", "Call of Duty", GameKind::Competitive},
    {"minecraft", "Minecraft", GameKind::Blocky},
    {"apexlegends", "Apex Legends", GameKind::Competitive},
    {"counterstrike", "Counter-Strike", GameKind::Competitive},
    {"baldursgate3", "Baldur's Gate 3", GameKind::Cinematic},
    {"thewitcher3", "The Witcher 3", GameKind::Cinematic},
    {"rocketleague", "Rocket League", GameKind::Racing},
};

const Template* findTemplate(const std::string& key) {
    for (const auto& t : kTemplates)
        if (key == t.key || startsWith(key, t.key)) return &t;
    return nullptr;
}

} // namespace

std::optional<GameProfile> ProfileManager::builtinTemplate(const std::string& key, const std::string& displayName) {
    const Template* t = findTemplate(key);
    if (!t) return std::nullopt;
    GameProfile p;
    p.key = t->key;
    p.displayName = displayName.empty() ? t->name : displayName;
    p.builtin = true;
    applyGameKind(t->kind, p);
    return p;
}

std::vector<std::string> ProfileManager::builtinNames() {
    std::vector<std::string> v;
    for (const auto& t : kTemplates) v.emplace_back(t.name);
    return v;
}

std::string ProfileManager::matchKey(const Settings& s, const std::string& key) {
    if (key.empty()) return {};
    if (s.profiles.count(key)) return key;
    // Longest existing profile key that prefixes the detected key
    std::string best;
    for (const auto& [k, p] : s.profiles)
        if (k.size() >= 4 && startsWith(key, k) && k.size() > best.size()) best = k;
    if (!best.empty()) return best;
    if (const Template* t = findTemplate(key)) return t->key;
    return key;
}

std::string ProfileManager::onGameDetected(Settings& s, const std::string& displayName, int64_t nowUnix) {
    std::string key = matchKey(s, gameKey(displayName));
    if (key.empty()) return {};
    auto it = s.profiles.find(key);
    if (it == s.profiles.end()) {
        GameProfile p;
        if (auto tpl = builtinTemplate(key, displayName)) {
            p = *tpl;
        } else {
            p.key = key;
            p.displayName = displayName;
            p.useGlobal = true; // unknown game: follow global settings until the user customizes it
            p.enhancement = s.enhancement;
            p.preset = s.preset;
        }
        it = s.profiles.emplace(key, p).first;
    }
    it->second.lastPlayedUnix = nowUnix;
    it->second.sessions += 1;
    return key;
}

ResolvedProfile ProfileManager::resolve(const Settings& s, const std::string& key) {
    ResolvedProfile r;
    r.preset = s.preset;
    r.enhancement = s.enhancement;
    if (key.empty()) return r;
    auto it = s.profiles.find(key);
    if (it == s.profiles.end()) return r;
    r.key = key;
    r.displayName = it->second.displayName;
    if (it->second.useGlobal) return r;
    r.fromProfile = true;
    r.preset = it->second.preset;
    r.priority = it->second.priority;
    r.enhancement = it->second.enhancement;
    return r;
}

} // namespace bgn
