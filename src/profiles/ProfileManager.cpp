#include "profiles/ProfileManager.h"

#include "core/StringUtil.h"
#include "profiles/GameTitle.h"

namespace bgn {

namespace {

struct Template {
    const char* key;
    const char* name;
    void (*apply)(GameProfile&);
};

void competitiveShooter(GameProfile& p) {
    p.preset = Preset::LowLatency;
    p.priority = PerformancePriority::Latency;
    auto& e = p.enhancement;
    e.frameGen = FrameGenMode::Off;          // never add hold-back latency in competitive games
    e.temporal = {true, false, 0.3f};        // light temporal => minimal ghosting on fast flicks
    e.sharpen = {true, false, 0.6f};         // target / HUD clarity
    e.deblur = {true, false, 0.5f};
    e.textBoost = true;
    e.color.vibrance = 0.2f;
    e.color.shadowDetail = 0.35f;            // see into dark corners without crushing
}

void cinematic(GameProfile& p) {
    p.preset = Preset::Quality;
    p.priority = PerformancePriority::Quality;
    auto& e = p.enhancement;
    e.frameGen = FrameGenMode::Auto;
    e.temporal = {true, false, 0.6f};
    e.sharpen = {true, false, 0.45f};
    e.color.localContrast = 0.3f;
    e.color.highlightRecovery = 0.4f;
}

void racing(GameProfile& p) {
    p.preset = Preset::Quality;
    p.priority = PerformancePriority::Balanced;
    auto& e = p.enhancement;
    e.frameGen = FrameGenMode::Auto;          // smooth camera motion benefits most
    e.deblur = {true, false, 0.6f};
    e.motionDeblur = true;
    e.sharpen = {true, false, 0.5f};
    e.color.vibrance = 0.25f;
}

void blocky(GameProfile& p) {
    p.preset = Preset::Balanced;
    auto& e = p.enhancement;
    e.frameGen = FrameGenMode::Auto;
    e.sharpen = {true, false, 0.3f};          // pixel-art textures: avoid halos
    e.temporal = {true, false, 0.55f};        // stabilises foliage shimmer
    e.denoise = {true, false, 0.25f};
    e.deblock = {true, false, 0.35f};
}

const Template kTemplates[] = {
    {"cyberpunk2077", "Cyberpunk 2077", cinematic},
    {"fortnite", "Fortnite", competitiveShooter},
    {"forzahorizon", "Forza Horizon", racing},
    {"callofduty", "Call of Duty", competitiveShooter},
    {"minecraft", "Minecraft", blocky},
    {"apexlegends", "Apex Legends", competitiveShooter},
    {"counterstrike", "Counter-Strike", competitiveShooter},
    {"baldursgate3", "Baldur's Gate 3", cinematic},
    {"thewitcher3", "The Witcher 3", cinematic},
    {"rocketleague", "Rocket League", racing},
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
    t->apply(p);
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
