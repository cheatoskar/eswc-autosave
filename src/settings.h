#pragma once
// eswc_autosave.ini: read at start, single keys written back when the menu changes them.
#include <windows.h>

namespace autosave {

struct Hotkey {
    UINT vk = 0; // 0 = off
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
};

struct Settings {
    // [autosave]
    bool autosave = true;     // Enabled
    bool bestOnly = true;     // Mode=best
    UINT testKey = VK_F9;     // TestKey, 0 = off
    bool scanReplays = true;  // ScanReplays
    bool tmxCopy = true;      // TmxCopy (ESWC)
    // [online]
    bool onlineFix = true;    // OnlineFix (ESWC, Original)
    // [controller]
    bool controllerFix = true; // ControllerFix
    // [menu]
    Hotkey menuKey;           // MenuKey, default Ctrl+F2
    Settings() {
        menuKey.vk = VK_F2;
        menuKey.ctrl = true;
    }
};

// "Ctrl+F2", "Shift+Alt+F11", "F2", "Insert", "0x71" (virtual key). "" or "0" or "off" = off (vk 0).
// false: not understood (out unchanged).
bool parseHotkey(const wchar_t* text, Hotkey* out);
// "Ctrl+F2"; "Off" for vk 0.
void formatHotkey(const Hotkey& k, wchar_t* out, size_t cap);
// true when the key event (virtual key + current modifier state) is this hotkey.
bool hotkeyMatches(const Hotkey& k, UINT vk, bool ctrl, bool shift, bool alt);

// <exe folder>\eswc_autosave.ini
void iniPath(wchar_t* out, size_t cap);
// Reads every key (missing keys keep the defaults). existed: the file is there.
void loadSettings(const wchar_t* path, Settings* s, bool* existed);
// Writes one key; false if the file could not be written (read-only game folder).
bool writeSetting(const wchar_t* path, const wchar_t* section, const wchar_t* key, const wchar_t* value);
bool writeSettingBool(const wchar_t* path, const wchar_t* section, const wchar_t* key, bool on);

} // namespace autosave
