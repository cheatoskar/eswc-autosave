#pragma once
// The menu overlay: Dear ImGui (vendor/horizon/imgui, the client's copy) drawn in the game's Direct3D 9 Present.
//
// Hooks, method tables and import slots only (the same ways the Horizon Client uses, src/host/d3dhook.cpp and
// dinput.cpp there): IDirect3D9::CreateDevice in the factory table (reached through our own factory, the exe's
// Direct3DCreate9 import and its GetProcAddress import), then Present and Reset in the game device's table.
// The game window is subclassed for the menu key and, while the menu is open, for mouse and keys.
// While the menu is open the game's DirectInput keyboard and mouse reads come back empty; controllers are never
// touched. With the menu closed nothing is drawn unless a toast is up.
#include <windows.h>

#include "settings.h"

namespace autosave {
namespace overlay {

using DrawMenuFn = void (*)(bool* open); // inside an ImGui frame, game thread

// Game thread, before the game makes its Direct3D device (the first DirectInput8Create).
void install(const Hotkey& menuKey, DrawMenuFn drawMenu);
bool installed();
bool deviceHooked();          // the game's device was seen
void toast(const char* utf8); // any thread; shown for a few seconds while the overlay runs
void setMenuOpen(bool open);  // game thread
bool menuOpen();

float scale();                       // UI scale (1 at 1080p)
void* logoTexture();                 // IDirect3DTexture9* of the logo for ImGui::Image, or null

} // namespace overlay
} // namespace autosave
