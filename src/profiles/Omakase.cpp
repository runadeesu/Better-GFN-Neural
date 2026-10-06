#include "profiles/Omakase.h"

#include <algorithm>

#include "core/StringUtil.h"
#include "settings/Presets.h"

namespace bgn {

namespace {

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

struct KindKeyword {
    const char* word;
    GameKind kind;
    bool prefixOnly; // short words: only at the start of the key
};

// Checked in order: racing before cinematic ("forzahorizon" vs "horizon").
const KindKeyword kKeywords[] = {
    // Racing / fast camera
    {"forza", GameKind::Racing, false}, {"f1", GameKind::Racing, true}, {"granturismo", GameKind::Racing, false},
    {"assettocorsa", GameKind::Racing, false}, {"needforspeed", GameKind::Racing, false}, {"thecrew", GameKind::Racing, false},
    {"dirtrally", GameKind::Racing, false}, {"wrc", GameKind::Racing, true}, {"trackmania", GameKind::Racing, false},
    {"rocketleague", GameKind::Racing, false}, {"motogp", GameKind::Racing, false}, {"projectcars", GameKind::Racing, false},
    // Competitive: shooters, fighting, MOBA, battle royale
    {"fortnite", GameKind::Competitive, false}, {"apexlegends", GameKind::Competitive, false}, {"callofduty", GameKind::Competitive, false},
    {"warzone", GameKind::Competitive, false}, {"counterstrike", GameKind::Competitive, false}, {"cs2", GameKind::Competitive, true},
    {"valorant", GameKind::Competitive, false}, {"overwatch", GameKind::Competitive, false}, {"rainbowsix", GameKind::Competitive, false},
    {"pubg", GameKind::Competitive, false}, {"playerunknown", GameKind::Competitive, false}, {"thefinals", GameKind::Competitive, false},
    {"marvelrivals", GameKind::Competitive, false}, {"destiny", GameKind::Competitive, false}, {"battlefield", GameKind::Competitive, false},
    {"halo", GameKind::Competitive, true}, {"splitgate", GameKind::Competitive, false}, {"deltaforce", GameKind::Competitive, false},
    {"xdefiant", GameKind::Competitive, false}, {"paladins", GameKind::Competitive, false}, {"teamfortress", GameKind::Competitive, false},
    {"leagueoflegends", GameKind::Competitive, false}, {"dota", GameKind::Competitive, true}, {"smite", GameKind::Competitive, true},
    {"streetfighter", GameKind::Competitive, false}, {"tekken", GameKind::Competitive, false}, {"mortalkombat", GameKind::Competitive, false},
    {"guiltygear", GameKind::Competitive, false}, {"escapefromtarkov", GameKind::Competitive, false}, {"huntshowdown", GameKind::Competitive, false},
    {"warthunder", GameKind::Competitive, false}, {"worldoftanks", GameKind::Competitive, false}, {"rust", GameKind::Competitive, true},
    {"deadbydaylight", GameKind::Competitive, false}, {"brawlhalla", GameKind::Competitive, false}, {"naraka", GameKind::Competitive, false},
    // Cinematic: story, open world, RPG
    {"cyberpunk", GameKind::Cinematic, false}, {"witcher", GameKind::Cinematic, false}, {"baldursgate", GameKind::Cinematic, false},
    {"eldenring", GameKind::Cinematic, false}, {"reddeadredemption", GameKind::Cinematic, false}, {"assassinscreed", GameKind::Cinematic, false},
    {"starfield", GameKind::Cinematic, false}, {"hogwartslegacy", GameKind::Cinematic, false}, {"godofwar", GameKind::Cinematic, false},
    {"horizon", GameKind::Cinematic, true}, {"ghostoftsushima", GameKind::Cinematic, false}, {"alanwake", GameKind::Cinematic, false},
    {"hellblade", GameKind::Cinematic, false}, {"stalker", GameKind::Cinematic, false}, {"metroexodus", GameKind::Cinematic, false},
    {"deathstranding", GameKind::Cinematic, false}, {"finalfantasy", GameKind::Cinematic, false}, {"blackmythwukong", GameKind::Cinematic, false},
    {"diablo", GameKind::Cinematic, false}, {"pathofexile", GameKind::Cinematic, false}, {"thelastofus", GameKind::Cinematic, false},
    {"spiderman", GameKind::Cinematic, false}, {"residentevil", GameKind::Cinematic, false}, {"fallout", GameKind::Cinematic, false},
    {"theelderscrolls", GameKind::Cinematic, false}, {"skyrim", GameKind::Cinematic, false}, {"masseffect", GameKind::Cinematic, false},
    {"dragonage", GameKind::Cinematic, false}, {"monsterhunter", GameKind::Cinematic, false}, {"indianajones", GameKind::Cinematic, false},
    {"stellarblade", GameKind::Cinematic, false}, {"liesofp", GameKind::Cinematic, false}, {"darksouls", GameKind::Cinematic, false},
    {"sekiro", GameKind::Cinematic, false}, {"genshinimpact", GameKind::Cinematic, false}, {"honkaistarrail", GameKind::Cinematic, false},
    {"wutheringwaves", GameKind::Cinematic, false}, {"zenlesszonezero", GameKind::Cinematic, false}, {"microsoftflightsimulator", GameKind::Cinematic, false},
    // Pixel art / voxel
    {"minecraft", GameKind::Blocky, false}, {"terraria", GameKind::Blocky, false}, {"stardewvalley", GameKind::Blocky, false},
    {"roblox", GameKind::Blocky, false}, {"teardown", GameKind::Blocky, false}, {"deeprockgalactic", GameKind::Blocky, false},
    {"valheim", GameKind::Blocky, false}, {"enshrouded", GameKind::Blocky, false},
};

} // namespace

const char* toString(GameKind k) {
    switch (k) {
    case GameKind::General: return "Other game";
    case GameKind::Competitive: return "Competitive game";
    case GameKind::Cinematic: return "Story / open world";
    case GameKind::Racing: return "Racing / fast motion";
    case GameKind::Blocky: return "Pixel art / voxel";
    }
    return "Other game";
}

GameKind classifyGame(const std::string& key) {
    if (key.empty()) return GameKind::General;
    for (const auto& k : kKeywords) {
        if (k.prefixOnly ? startsWith(key, k.word) : key.find(k.word) != std::string::npos) return k.kind;
    }
    return GameKind::General;
}

void applyGameKind(GameKind kind, GameProfile& p) {
    switch (kind) {
    case GameKind::General: break;
    case GameKind::Competitive: competitiveShooter(p); break;
    case GameKind::Cinematic: cinematic(p); break;
    case GameKind::Racing: racing(p); break;
    case GameKind::Blocky: blocky(p); break;
    }
}

OmakasePlan planOmakase(const Settings& s, const std::string& profileKey, const std::string& gpuName) {
    OmakasePlan plan;
    plan.kind = classifyGame(profileKey);
    GameProfile tuned; // defaults: Auto preset, every strength automatic
    applyGameKind(plan.kind, tuned);
    plan.preset = tuned.preset;
    plan.priority = tuned.priority;
    plan.enhancement = tuned.enhancement;
    // Natural look, scene-adaptive color and HDR, adaptive cleanup: the picture
    // should look like the game, only cleaner and sharper.
    plan.enhancement.style = VisualStyle::Natural;
    plan.enhancement.adaptiveCleanup = true;
    plan.enhancement.upscale = UpscaleMode::Auto;
    plan.enhancement.color.automatic = true;
    plan.enhancement.hdr = HdrSettings{};

    if (!profileKey.empty()) {
        auto it = s.profiles.find(profileKey);
        if (it != s.profiles.end() && it->second.learnedTier >= 0 && !gpuName.empty() && it->second.learnedGpu == gpuName) {
            const PresetPolicy pol = policyFor(plan.preset, true, plan.priority);
            plan.initialTier = std::clamp(it->second.learnedTier, pol.minTier, pol.maxTier);
            plan.learned = true;
        }
    }
    if (!plan.learned && s.benchmark.valid && !gpuName.empty() && s.benchmark.gpuName == gpuName) plan.initialTier = s.benchmark.recommendedTier;
    return plan;
}

int dominantTier(const std::map<int, double>& tierSeconds) {
    int best = -1;
    double bestT = 0;
    for (const auto& [tier, t] : tierSeconds)
        if (t > bestT) {
            best = tier;
            bestT = t;
        }
    return best;
}

bool learnTier(GameProfile& p, int tier, const std::string& gpuName) {
    if (tier < 0 || tier > kMaxTier || gpuName.empty()) return false;
    if (p.learnedTier == tier && p.learnedGpu == gpuName) return false;
    p.learnedTier = tier;
    p.learnedGpu = gpuName;
    return true;
}

} // namespace bgn
