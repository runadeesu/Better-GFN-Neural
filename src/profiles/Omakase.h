#pragma once
// "Omakase" (fully automatic) mode. Portable and unit tested.
//
// The user does not choose anything: the game type is recognized from the
// title, the tuning for that type is applied, Auto Mode picks the quality tier
// for this PC in real time, and the tier a game settles at is remembered so
// the next session starts there immediately (no ramp-up, no early stutter).

#include <map>
#include <string>

#include "settings/Settings.h"

namespace bgn {

enum class GameKind {
    General,     // unknown game: balanced, everything automatic
    Competitive, // shooters, fighting, MOBA: latency first, no frame interpolation
    Cinematic,   // story / open-world: quality first, interpolation when it fits
    Racing,      // fast camera motion: motion deblur + interpolation when it fits
    Blocky,      // pixel art / voxel: gentle sharpening, strong stabilisation
};
const char* toString(GameKind k);

// Game type from the normalized key ("apexlegends", "eldenring", ...).
GameKind classifyGame(const std::string& gameKey);

// Applies the tuning for a game type to a profile (preset, priority and
// enhancement settings). General leaves the defaults (everything automatic).
void applyGameKind(GameKind kind, GameProfile& p);

struct OmakasePlan {
    GameKind kind = GameKind::General;
    Preset preset = Preset::Auto;
    PerformancePriority priority = PerformancePriority::Balanced;
    EnhancementSettings enhancement; // fully automatic strengths
    int initialTier = -1;            // -1 = let the engine estimate from the GPU
    bool learned = false;            // initialTier comes from an earlier session of this game
};

// Plan for the current game (|profileKey| may be empty when no game is known).
// |gpuName| is the adapter the engine runs on; learned tiers from another GPU
// and benchmark results from another GPU are ignored.
OmakasePlan planOmakase(const Settings& s, const std::string& profileKey, const std::string& gpuName);

// The tier used most in a session (time-weighted); -1 if no time recorded.
int dominantTier(const std::map<int, double>& tierSeconds);

// Remembers the tier a game settled at. Returns true if the profile changed.
bool learnTier(GameProfile& p, int tier, const std::string& gpuName);

} // namespace bgn
