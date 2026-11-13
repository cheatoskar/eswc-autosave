// Unit tests: menu key parsing and formatting, ini read and write-back, and the Horizon Client coexistence plan.
// Run from anywhere: the ini goes to %TEMP%.
#include <windows.h>

#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "../src/coexist.h"
#include "../src/settings.h"

using namespace autosave;

static int g_fail = 0, g_pass = 0;
static void check(bool ok, const char* what) {
    if (ok) g_pass++;
    else {
        g_fail++;
        printf("FAIL  %s\n", what);
    }
}

static void hotkeys() {
    Hotkey k;
    check(parseHotkey(L"Ctrl+F2", &k) && k.vk == VK_F2 && k.ctrl && !k.shift && !k.alt, "Ctrl+F2");
    check(parseHotkey(L"shift+alt+f11", &k) && k.vk == VK_F11 && !k.ctrl && k.shift && k.alt, "shift+alt+f11");
    check(parseHotkey(L"F24", &k) && k.vk == VK_F24 && !k.ctrl, "F24");
    check(parseHotkey(L"Insert", &k) && k.vk == VK_INSERT, "Insert");
    check(parseHotkey(L"Ctrl+E", &k) && k.vk == 'E' && k.ctrl, "Ctrl+E");
    check(parseHotkey(L"ctrl+e", &k) && k.vk == 'E' && k.ctrl, "ctrl+e (lower case letter)");
    check(parseHotkey(L"0x71", &k) && k.vk == VK_F2 && !k.ctrl, "0x71");
    check(parseHotkey(L" Ctrl+F2 ", &k) && k.vk == VK_F2 && k.ctrl, "spaces around");
    check(parseHotkey(L"0", &k) && k.vk == 0, "0 = off");
    check(parseHotkey(L"off", &k) && k.vk == 0, "off");
    check(parseHotkey(L"", &k) && k.vk == 0, "empty = off");
    Hotkey keep;
    keep.vk = 'Q';
    check(!parseHotkey(L"Ctrl+Banana", &keep) && keep.vk == 'Q', "unknown key refused, value kept");
    check(!parseHotkey(L"Hyper+F2", &keep) && keep.vk == 'Q', "unknown modifier refused");
    check(!parseHotkey(L"F25", &keep), "F25 refused");
    check(!parseHotkey(L"Ctrl+", &keep), "modifier without key refused");

    wchar_t out[48];
    Hotkey f;
    f.vk = VK_F2;
    f.ctrl = true;
    formatHotkey(f, out, 48);
    check(wcscmp(out, L"Ctrl+F2") == 0, "format Ctrl+F2");
    Hotkey g;
    g.vk = VK_INSERT;
    g.shift = true;
    formatHotkey(g, out, 48);
    check(wcscmp(out, L"Shift+Insert") == 0, "format Shift+Insert");
    formatHotkey(Hotkey{}, out, 48);
    check(wcscmp(out, L"Off") == 0, "format off");
    // round trip of every F key with all modifiers
    bool rt = true;
    for (UINT vk = VK_F1; vk <= VK_F24; vk++)
        for (int m = 0; m < 8; m++) {
            Hotkey a;
            a.vk = vk;
            a.ctrl = m & 1;
            a.shift = (m & 2) != 0;
            a.alt = (m & 4) != 0;
            Hotkey b;
            formatHotkey(a, out, 48);
            if (!parseHotkey(out, &b) || b.vk != a.vk || b.ctrl != a.ctrl || b.shift != a.shift || b.alt != a.alt) rt = false;
        }
    check(rt, "format/parse round trip, F1-F24 x modifiers");

    check(hotkeyMatches(f, VK_F2, true, false, false), "Ctrl+F2 matches");
    check(!hotkeyMatches(f, VK_F2, false, false, false), "F2 alone does not match Ctrl+F2");
    check(!hotkeyMatches(f, VK_F2, true, true, false), "Ctrl+Shift+F2 does not match Ctrl+F2");
    check(!hotkeyMatches(Hotkey{}, 0, false, false, false), "off never matches");
}

static void ini() {
    wchar_t dir[MAX_PATH];
    GetTempPathW(MAX_PATH, dir);
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%sle_essentials_test_%lu.ini", dir, GetCurrentProcessId());
    DeleteFileW(path);
    Settings s;
    bool existed = true;
    loadSettings(path, &s, &existed);
    check(!existed, "missing ini reported");
    check(s.autosave && s.bestOnly && s.testKey == VK_F9 && s.scanReplays && s.tmxCopy && s.onlineFix && s.controllerFix,
          "defaults without an ini");
    check(s.menuKey.vk == VK_F2 && s.menuKey.ctrl, "default menu key Ctrl+F2");
    check(s.music == -1 && s.sounds == -1 && s.windowMode == -1, "default: the game's own volumes and window mode");

    // the 1.2.0 ini (no [controller], no [menu]) keeps its values, new keys get defaults
    const char* old =
        "[autosave]\r\nEnabled=0\r\nMode=all\r\nTestKey=0\r\nScanReplays=0\r\nTmxCopy=0\r\n[online]\r\nOnlineFix=0\r\n";
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD wr = 0;
    WriteFile(h, old, static_cast<DWORD>(strlen(old)), &wr, nullptr);
    CloseHandle(h);
    loadSettings(path, &s, &existed);
    check(existed, "ini found");
    check(!s.autosave && !s.bestOnly && s.testKey == 0 && !s.scanReplays && !s.tmxCopy && !s.onlineFix, "1.2.0 ini values");
    check(s.controllerFix && s.menuKey.vk == VK_F2, "1.2.0 ini: new keys default");
    check(s.music == -1 && s.sounds == -1 && s.windowMode == -1, "1.2.0 ini: game settings untouched");

    // write-back from the menu
    check(writeSettingBool(path, L"autosave", L"Enabled", true), "write Enabled");
    check(writeSetting(path, L"autosave", L"Mode", L"best"), "write Mode");
    check(writeSettingBool(path, L"controller", L"ControllerFix", false), "write ControllerFix");
    check(writeSettingBool(path, L"online", L"OnlineFix", true), "write OnlineFix");
    check(writeSetting(path, L"menu", L"MenuKey", L"Shift+F11"), "write MenuKey");
    loadSettings(path, &s, &existed);
    check(s.autosave && s.bestOnly && !s.controllerFix && s.onlineFix, "values read back");
    check(s.menuKey.vk == VK_F11 && s.menuKey.shift && !s.menuKey.ctrl, "menu key read back");
    check(s.testKey == 0 && !s.tmxCopy, "untouched keys stay");
    // a menu key the parser does not know: default stays
    writeSetting(path, L"menu", L"MenuKey", L"Ctrl+Nope");
    loadSettings(path, &s, &existed);
    check(s.menuKey.vk == VK_F2 && s.menuKey.ctrl, "unknown MenuKey -> default");
    writeSetting(path, L"menu", L"MenuKey", L"0");
    loadSettings(path, &s, &existed);
    check(s.menuKey.vk == 0, "MenuKey=0 -> no menu");
    // Mode as 1.2.0 read it: anything but "best" saves every finish
    writeSetting(path, L"autosave", L"Mode", L"Best");
    loadSettings(path, &s, &existed);
    check(s.bestOnly, "Mode=Best (any case)");
    writeSetting(path, L"autosave", L"Mode", L"sometimes");
    loadSettings(path, &s, &existed);
    check(!s.bestOnly, "Mode=<other> -> all, as 1.2.0");
    // volumes and window mode, as the menu writes them
    check(writeSetting(path, L"sound", L"Music", L"35"), "write Music");
    check(writeSetting(path, L"sound", L"Sounds", L"100"), "write Sounds");
    check(writeSetting(path, L"display", L"WindowMode", L"fullscreen"), "write WindowMode");
    loadSettings(path, &s, &existed);
    check(s.music == 35 && s.sounds == 100 && s.windowMode == 1, "game settings read back");
    writeSetting(path, L"sound", L"Music", L"");
    writeSetting(path, L"display", L"WindowMode", L"Windowed");
    loadSettings(path, &s, &existed);
    check(s.music == -1 && s.windowMode == 0, "empty Music -> game's own; Windowed (any case)");
    DeleteFileW(path);
}

static void values() {
    check(parsePercent(L"0") == 0 && parsePercent(L"100") == 100 && parsePercent(L" 42 ") == 42, "percent: plain numbers");
    check(parsePercent(L"250") == 100, "percent: above 100 -> 100");
    check(parsePercent(L"") == -1 && parsePercent(L"game") == -1 && parsePercent(L"-5") == -1 && parsePercent(L"5x") == -1 &&
              parsePercent(nullptr) == -1,
          "percent: anything else -> the game's own");
    check(parseWindowMode(L"windowed") == 0 && parseWindowMode(L"FULLSCREEN") == 1, "window mode: names");
    check(parseWindowMode(L"") == -1 && parseWindowMode(L"borderless") == -1 && parseWindowMode(nullptr) == -1, "window mode: else -> game's own");
}

static void plans() {
    HorizonProbe none;
    Plan p = makePlan(none, Stage::Start, true, true, true, true, true);
    check(!p.horizon && p.loginFix && p.menu && p.controller && p.autosave, "standalone: everything ours");
    p = makePlan(none, Stage::Start, false, true, true, true, true);
    check(!p.loginFix, "Sunrise: no login fix");
    p = makePlan(none, Stage::Start, true, false, false, false, false);
    check(!p.loginFix && !p.menu && !p.controller && !p.autosave, "standalone, all switched off");
    check(p.gameSettings, "standalone: game settings ours");

    HorizonProbe chained;
    chained.loaderIsChain = true;
    chained.hostFile = true;
    p = makePlan(chained, Stage::Start, true, true, true, true, true);
    check(p.horizon && !p.loginFix && !p.menu, "start, chained + host.dll there: client expected, no login fix/menu yet");
    check(p.controller && p.autosave, "start, client expected: controller tried later, autosave ours");
    p = makePlan(chained, Stage::FirstMessage, true, true, true, true, true);
    check(!p.horizon && p.loginFix, "first message, client did not start: login fix ours");

    HorizonProbe running = chained;
    running.hostLoaded = true;
    p = makePlan(running, Stage::FirstMessage, true, true, true, true, true);
    check(p.horizon && !p.loginFix && !p.menu && p.controller && p.autosave, "first message, client runs");
    check(p.gameSettings, "client without a Game settings module: ours applies the ini at start");
    HorizonProbe withModule = chained;
    withModule.hostGameSettings = true;
    p = makePlan(withModule, Stage::Start, true, true, true, true, true);
    check(p.horizon && !p.gameSettings, "client with its Game settings module: ours steps aside");
    HorizonProbe moduleNoClient;
    moduleNoClient.hostFile = true;
    moduleNoClient.hostGameSettings = true;
    p = makePlan(moduleNoClient, Stage::Start, true, true, true, true, true);
    check(!p.horizon && p.gameSettings, "module files there but the client is not chained in: ours");

    HorizonProbe fileOnly;
    fileOnly.hostFile = true; // host.dll lying there, our loader is dinput8.dll itself: the client is not chained in
    p = makePlan(fileOnly, Stage::Start, true, true, true, true, true);
    check(!p.horizon && p.loginFix && p.menu, "host.dll there but our loader is dinput8.dll: standalone");

    HorizonProbe loadedFirst; // the client's loader chained behind ours: already loaded at our start
    loadedFirst.hostFile = true;
    loadedFirst.hostLoaded = true;
    p = makePlan(loadedFirst, Stage::Start, true, true, true, true, true);
    check(p.horizon && !p.loginFix && !p.menu, "client already loaded at our start");
}

int main() {
    hotkeys();
    ini();
    values();
    plans();
    printf("essentials_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
