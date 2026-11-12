#pragma once
// Legacy Essentials: everything besides the autosave hooks. Started by autosave::initOnce once the exe is known
// and the ini is read: Horizon Client check, login fix, controller fix, menu.
#include <windows.h>
#include <stdint.h>

#include "games.h"
#include "settings.h"

namespace autosave {

constexpr char kProductVersion[] = "1.3.0";

// autosave.cpp: the autosave hooks and their switches (game thread).
bool autosaveInstall();            // idempotent; false: hooks could not be installed (logged)
bool autosaveInstalled();
void autosaveSetEnabled(bool on);  // finishes are saved / ignored from now on
void autosaveSetBestOnly(bool on);

namespace essentials {

// Game main thread (inside the first DirectInput8Create), no loader lock.
void start(const ExeId& exe, const Settings& settings, uintptr_t exeBase, DWORD mainThread);

// A replay was written (full path). Game thread. Remembered for the menu's folder button, shown as a toast.
void noteSaved(const wchar_t* fullPath);

} // namespace essentials
} // namespace autosave
