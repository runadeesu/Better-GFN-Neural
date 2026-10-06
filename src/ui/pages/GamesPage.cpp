#include <algorithm>
#include <format>
#include <vector>

#include "profiles/ProfileManager.h"
#include "settings/Presets.h"
#include "ui/I18n.h"
#include "ui/Pages.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace bgn::ui {

void drawGamesPage(PageContext& c) {
    Settings& s = c.settings;
    const float sc = ImGui::GetStyle().FontScaleDpi;
    const float W = ImGui::GetContentRegionAvail().x;
    static std::string selected;
    if (!c.model.profileKey.empty() && selected.empty()) selected = c.model.profileKey;
    if (!selected.empty() && !s.profiles.count(selected)) selected.clear();
    if (selected.empty() && !s.profiles.empty()) selected = s.profiles.begin()->first;

    const bool omakase = omakaseBanner(c);
    const float listW = 300 * sc, gap = 12 * sc;
    beginCard("list", ImVec2(listW, 0));
    sectionTitle(tr("Game profiles"), tr("Created automatically from the GeForce NOW window title. Built-in tuning for popular games."));
    std::vector<const GameProfile*> sorted;
    for (const auto& [k, p] : s.profiles) sorted.push_back(&p);
    std::sort(sorted.begin(), sorted.end(), [](const GameProfile* a, const GameProfile* b) { return a->lastPlayedUnix > b->lastPlayedUnix; });
    if (sorted.empty()) textWrappedDim(tr("No game profiles yet. Profiles are created automatically when a game is detected."));
    for (const GameProfile* p : sorted) {
        ImGui::PushID(p->key.c_str());
        bool isSel = p->key == selected;
        bool isCurrent = p->key == c.model.profileKey;
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x, h = 52 * sc;
        if (ImGui::InvisibleButton("row", ImVec2(w, h))) selected = p->key;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        bool hov = ImGui::IsItemHovered();
        dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), isSel ? withAlpha(col::Accent, 0.12f) : (hov ? col::CardHover : col::Card), 10 * sc);
        if (isSel) dl->AddRect(pos, ImVec2(pos.x + w, pos.y + h), withAlpha(col::Accent, 0.6f), 10 * sc);
        ImGui::PushFont(fonts().semibold, kFontBody);
        dl->AddText(ImVec2(pos.x + 12 * sc, pos.y + 8 * sc), col::Text, p->displayName.c_str());
        ImGui::PopFont();
        std::string sub = p->builtin ? std::format("{} \xC2\xB7 {} {}", tr("Built-in"), p->sessions, tr("sessions")) : std::format("{} {}", p->sessions, tr("sessions"));
        if (p->useGlobal) sub += std::string("  \xC2\xB7  ") + tr("Global");
        if (isCurrent) sub = std::string("\xE2\x96\xB6 ") + sub;
        ImGui::PushFont(fonts().regular, kFontSmall);
        dl->AddText(ImVec2(pos.x + 12 * sc, pos.y + 29 * sc), isCurrent ? col::Accent : col::TextDim, sub.c_str());
        ImGui::PopFont();
        ImGui::PopID();
    }
    endCard();
    ImGui::SameLine(0, gap);

    ImGui::BeginGroup();
    auto it = s.profiles.find(selected);
    if (it != s.profiles.end()) {
        GameProfile& p = it->second;
        beginCard("profileHeader", ImVec2(W - listW - gap, 0));
        sectionTitle(p.displayName.c_str(), p.builtin && !omakase ? tr("Built-in profile tuned for this game. You can change everything.") : nullptr);
        if (omakase) {
            const GameKind kind = classifyGame(p.key);
            std::string line = trf("Omakase: {}", tr(toString(kind)));
            if (p.learnedTier >= 0) line += "  \xC2\xB7  " + trf("Learned \xC2\xB7 starts at {}", trText(tierName(p.learnedTier)));
            textColored(col::Accent, line.c_str(), kFontBody, true);
            textWrappedDim(tr("Tuned automatically for this type of game. Choose \"Adjust manually instead\" above to edit this profile."));
            endCard();
            ImGui::EndGroup();
            return;
        }
        bool changed = false;
        changed |= toggleRow(tr("Use global settings"), &p.useGlobal);
        ImGui::BeginDisabled(p.useGlobal);
        label(tr("Preset"));
        const char* presets[] = {presetName(Preset::Auto), presetName(Preset::Ultra), presetName(Preset::Quality), presetName(Preset::Balanced),
                                 presetName(Preset::Performance), presetName(Preset::LowLatency)};
        int pr = int(p.preset);
        if (segmented("pp", &pr, presets, 6)) {
            p.preset = Preset(pr);
            changed = true;
        }
        label(tr("Performance priority"));
        const char* prio[] = {tr("Balanced"), tr(toString(PerformancePriority::Quality)), tr(toString(PerformancePriority::Latency))};
        int pv = int(p.priority);
        if (segmented("prio", &pv, prio, 3)) {
            p.priority = PerformancePriority(pv);
            changed = true;
        }
        ImGui::EndDisabled();
        if (secondaryButton(tr("Delete profile")) && c.actions.deleteProfile) {
            std::string key = p.key;
            selected.clear();
            c.actions.deleteProfile(key);
            endCard();
            ImGui::EndGroup();
            return;
        }
        endCard();
        if (!p.useGlobal) changed |= drawEnhancementEditor(p.enhancement, p.key.c_str());
        if (changed) c.actions.settingsChanged();
    } else {
        beginCard("empty", ImVec2(W - listW - gap, 0));
        textWrappedDim(tr("Supported out of the box: Cyberpunk 2077, Fortnite, Forza Horizon, Call of Duty, Minecraft, Apex Legends, Counter-Strike, Baldur's Gate 3, The Witcher 3, Rocket League. Any other game gets a profile automatically."));
        endCard();
    }
    ImGui::EndGroup();
}

} // namespace bgn::ui
