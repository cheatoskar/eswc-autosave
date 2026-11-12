// Volume, Music, Sounds, Window mode (gamesettings.h).
#include "gamesettings.h"

#include <float.h>
#include <stdio.h>

#include "../vendor/horizon/game-settings/gs_game.h"
#include "../vendor/horizon/game-settings/gs_master.h"
#include "../vendor/horizon/imgui/imgui.h"
#include "log.h"

namespace autosave {
namespace gamesettings {
namespace {

gs::Game* g_game = nullptr;
gs::MasterVolume* g_master = nullptr;
int g_windowAtStart = -1; // the mode the game starts in (0/1), -1 unknown

void logCb(void*, int level, const char* text) { AS_LOG("%s%s", level == 2 ? "ERROR: " : level == 1 ? "warning: " : "", text); }

// label on the left, control filling the rest of the row
void rowLabel(const char* label, float scale) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(110.0f * scale);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

void unavailable() {
    ImGui::BeginDisabled();
    ImGui::TextUnformatted("Unavailable");
    ImGui::EndDisabled();
}

void savePercent(const wchar_t* ini, const wchar_t* key, int pct) {
    wchar_t v[8];
    swprintf_s(v, L"%d", pct);
    if (!writeSetting(ini, L"sound", key, v)) AS_LOG("menu: could not write %ls=%d to the ini (error %lu)", key, pct, GetLastError());
    else AS_LOG("menu: %ls=%d", key, pct);
}

// A 0..100 % slider: shows the game's value, applies a drag at most every 100 ms and once more when it is let
// go (gs::Throttle), so the game does not redo its sounds on every frame. true when an edit ended: *done = value.
struct Drag {
    bool active = false;
    int value = 0;
    gs::Throttle throttle;
};
Drag g_dragVolume, g_dragMusic, g_dragSounds;

bool slider(const char* id, Drag& d, int current, void (*apply)(int), int* done) {
    int pct = d.active ? d.value : current;
    ImGui::SliderInt(id, &pct, 0, 100, "%d%%");
    const bool active = ImGui::IsItemActive();
    const bool released = ImGui::IsItemDeactivated();
    if (active && !d.active) d.throttle.begin(current);
    if (active || released) {
        d.value = pct;
        if (d.throttle.due(pct, released, GetTickCount())) apply(pct);
    }
    d.active = active;
    if (!ImGui::IsItemDeactivatedAfterEdit()) return false;
    *done = pct;
    return true;
}

void applyMusic(int pct) { g_game->setMusic(gs::fromPercent(pct)); }
void applySounds(int pct) { g_game->setSounds(gs::fromPercent(pct)); }
void applyVolume(int pct) { g_master->set(gs::fromPercent(pct)); }

void volumeRow(const wchar_t* ini, float scale, const char* label, const char* id, bool music) {
    float v = -1.0f;
    const bool ok = g_game && (music ? g_game->readMusic(&v) : g_game->readSounds(&v));
    rowLabel(label, scale);
    if (!ok) {
        unavailable();
        return;
    }
    int pct = 0;
    if (slider(id, music ? g_dragMusic : g_dragSounds, gs::toPercent(v), music ? &applyMusic : &applySounds, &pct))
        savePercent(ini, music ? L"Music" : L"Sounds", pct); // kept, and put into the game at every start
}

} // namespace

void start(uintptr_t exeBase, uint32_t exeTimestamp, const Settings& s, bool apply, bool volume) {
    const gs::GameTable* t = gs::tableForTimestamp(exeTimestamp);
    if (!t) {
        AS_LOG("game settings: unknown exe (timestamp %08X): Music, Sounds and Window mode stay off", exeTimestamp);
    } else {
        g_game = new gs::Game(t, exeBase, &logCb, nullptr);
        g_game->audioOk();
        g_game->displayOk();
        if (apply) {
            gs::Prefs p;
            p.music = s.music;
            p.sounds = s.sounds;
            p.window = s.windowMode;
            if (p.music < 0 && p.sounds < 0 && p.window < 0) AS_LOG("game settings: nothing chosen in the ini, the game's own values stay");
            else g_game->applyStart(p);
        } else {
            AS_LOG("game settings: the Horizon Client's Game settings module applies its values; ours stay unused");
        }
        bool fs = false;
        if (g_game->readFullscreen(&fs)) g_windowAtStart = fs ? 1 : 0;
    }
    if (volume) {
        g_master = new gs::MasterVolume();
        g_master->start(&logCb, nullptr);
    }
}

void drawRows(const wchar_t* ini, float scale) {
    // Volume: the game's Windows audio session
    const float master = g_master ? g_master->get() : -1.0f;
    rowLabel("Volume", scale);
    if (master < 0.0f) {
        unavailable();
    } else {
        int pct = 0;
        if (slider("##le-volume", g_dragVolume, gs::toPercent(master), &applyVolume, &pct))
            AS_LOG("menu: volume %d%% (Windows audio session)", pct);
    }
    volumeRow(ini, scale, "Music", "##le-music", true);
    volumeRow(ini, scale, "Sounds", "##le-sounds", false);

    // Window mode: the game's setting, read when it starts
    bool fs = false;
    const bool fsOk = g_game && g_game->readFullscreen(&fs);
    rowLabel("Window mode", scale);
    if (!fsOk) {
        unavailable();
        return;
    }
    static const char* kModes[] = {"Windowed", "Fullscreen"};
    int mode = fs ? 1 : 0;
    if (ImGui::Combo("##le-window", &mode, kModes, 2) && (mode == 1) != fs && g_game->setFullscreen(mode == 1)) {
        if (!writeSetting(ini, L"display", L"WindowMode", mode ? L"fullscreen" : L"windowed"))
            AS_LOG("menu: could not write WindowMode to the ini (error %lu)", GetLastError());
        else AS_LOG("menu: WindowMode=%s (next start)", mode ? "fullscreen" : "windowed");
    }
    if (g_windowAtStart >= 0 && mode != g_windowAtStart) {
        ImGui::Indent(110.0f * scale);
        ImGui::TextUnformatted("Restart game");
        ImGui::Unindent(110.0f * scale);
    }
}

} // namespace gamesettings
} // namespace autosave
