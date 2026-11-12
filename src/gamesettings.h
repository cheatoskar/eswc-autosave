#pragma once
// Volume, Music, Sounds and Window mode in this mod: the glue around the Horizon Client's game-settings code
// (vendor/horizon/game-settings, the same files the client's Game settings module uses; how it works: gs_game.h).
// The chosen values are kept in the ini ([sound] Music, Sounds; [display] WindowMode) and written into the
// game's own settings at every start. Volume is the game's Windows audio session (Windows remembers it).
#include <windows.h>
#include <stdint.h>

#include "settings.h"

namespace autosave {
namespace gamesettings {

// Game main thread, inside the first DirectInput8Create (before the game makes its Direct3D device and audio).
// apply: write the ini's values into the game's settings now (false: the Horizon Client's module does it).
// volume: start the worker for the Volume row (the menu is shown).
void start(uintptr_t exeBase, uint32_t exeTimestamp, const Settings& s, bool apply, bool volume);

// The menu's rows (game thread, inside the menu window). ini: where changes are written.
void drawRows(const wchar_t* ini, float scale);

} // namespace gamesettings
} // namespace autosave
