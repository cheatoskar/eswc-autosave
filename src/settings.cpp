#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

namespace autosave {
namespace {

struct KeyName {
    UINT vk;
    const wchar_t* name;
};
const KeyName kNames[] = {
    {VK_INSERT, L"Insert"}, {VK_DELETE, L"Delete"}, {VK_HOME, L"Home"},     {VK_END, L"End"},
    {VK_PRIOR, L"PageUp"},  {VK_NEXT, L"PageDown"}, {VK_PAUSE, L"Pause"},   {VK_SCROLL, L"ScrollLock"},
    {VK_OEM_3, L"`"},       {VK_OEM_5, L"\\"},      {VK_OEM_PLUS, L"+"},    {VK_OEM_MINUS, L"-"},
};

// one part of "Ctrl+F2" without the modifiers; 0 = unknown
UINT keyOf(const wchar_t* s) {
    if (!s[0]) return 0;
    if ((s[0] == L'F' || s[0] == L'f') && s[1] >= L'1' && s[1] <= L'9') {
        wchar_t* end = nullptr;
        const unsigned long n = wcstoul(s + 1, &end, 10);
        if (end && *end == 0 && n >= 1 && n <= 24) return VK_F1 + static_cast<UINT>(n) - 1;
    }
    if (s[1] == 0) {
        const wchar_t c = s[0];
        if (c >= L'a' && c <= L'z') return static_cast<UINT>(c - L'a' + 'A');
        if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9')) return static_cast<UINT>(c);
    }
    if (s[0] == L'0' && (s[1] == L'x' || s[1] == L'X')) {
        wchar_t* end = nullptr;
        const unsigned long v = wcstoul(s, &end, 16);
        if (end && *end == 0 && v > 0 && v < 0xff) return static_cast<UINT>(v);
    }
    for (const auto& k : kNames)
        if (_wcsicmp(s, k.name) == 0) return k.vk;
    return 0;
}

} // namespace

bool parseHotkey(const wchar_t* text, Hotkey* out) {
    if (!text || !out) return false;
    while (*text == L' ') text++;
    if (!text[0] || wcscmp(text, L"0") == 0 || _wcsicmp(text, L"off") == 0) {
        *out = Hotkey{};
        return true;
    }
    Hotkey k;
    wchar_t buf[64];
    wcsncpy_s(buf, text, _TRUNCATE);
    // trailing spaces
    for (size_t n = wcslen(buf); n && buf[n - 1] == L' '; n--) buf[n - 1] = 0;
    wchar_t* part = buf;
    for (;;) {
        // "+" as the key itself: "Ctrl++"
        wchar_t* plus = (part[0] == L'+' && part[1] == 0) ? nullptr : wcschr(part, L'+');
        if (!plus) {
            k.vk = keyOf(part);
            if (!k.vk) return false;
            *out = k;
            return true;
        }
        *plus = 0;
        if (_wcsicmp(part, L"Ctrl") == 0 || _wcsicmp(part, L"Control") == 0) k.ctrl = true;
        else if (_wcsicmp(part, L"Shift") == 0) k.shift = true;
        else if (_wcsicmp(part, L"Alt") == 0) k.alt = true;
        else return false;
        part = plus + 1;
    }
}

void formatHotkey(const Hotkey& k, wchar_t* out, size_t cap) {
    if (!out || !cap) return;
    out[0] = 0;
    if (!k.vk) {
        wcsncpy_s(out, cap, L"Off", _TRUNCATE);
        return;
    }
    wchar_t key[24] = L"";
    if (k.vk >= VK_F1 && k.vk <= VK_F24) swprintf_s(key, L"F%u", k.vk - VK_F1 + 1);
    else if ((k.vk >= 'A' && k.vk <= 'Z') || (k.vk >= '0' && k.vk <= '9')) swprintf_s(key, L"%c", static_cast<wchar_t>(k.vk));
    else {
        for (const auto& n : kNames)
            if (n.vk == k.vk) wcsncpy_s(key, n.name, _TRUNCATE);
        if (!key[0]) swprintf_s(key, L"0x%02X", k.vk);
    }
    swprintf_s(out, cap, L"%s%s%s%s", k.ctrl ? L"Ctrl+" : L"", k.shift ? L"Shift+" : L"", k.alt ? L"Alt+" : L"", key);
}

bool hotkeyMatches(const Hotkey& k, UINT vk, bool ctrl, bool shift, bool alt) {
    return k.vk && vk == k.vk && ctrl == k.ctrl && shift == k.shift && alt == k.alt;
}

int parsePercent(const wchar_t* text) {
    if (!text) return -1;
    while (*text == L' ') text++;
    if (*text < L'0' || *text > L'9') return -1;
    wchar_t* end = nullptr;
    const unsigned long v = wcstoul(text, &end, 10);
    while (end && *end == L' ') end++;
    if (!end || *end) return -1;
    return v > 100 ? 100 : static_cast<int>(v);
}

int parseWindowMode(const wchar_t* text) {
    if (!text) return -1;
    while (*text == L' ') text++;
    if (_wcsnicmp(text, L"windowed", 8) == 0) return 0;
    if (_wcsnicmp(text, L"fullscreen", 10) == 0) return 1;
    return -1;
}

void iniPath(wchar_t* out, size_t cap) {
    out[0] = 0;
    wchar_t path[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* slash = (n && n < MAX_PATH) ? wcsrchr(path, L'\\') : nullptr;
    if (!slash) return;
    wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"eswc_autosave.ini");
    wcsncpy_s(out, cap, path, _TRUNCATE);
}

void loadSettings(const wchar_t* path, Settings* s, bool* existed) {
    *s = Settings{};
    if (existed) *existed = path[0] && GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
    if (!path[0]) return;
    s->autosave = GetPrivateProfileIntW(L"autosave", L"Enabled", 1, path) != 0;
    wchar_t mode[32] = L"";
    GetPrivateProfileStringW(L"autosave", L"Mode", L"best", mode, 32, path);
    s->bestOnly = _wcsicmp(mode, L"best") == 0; // as 1.2.0: anything but "best" saves every finish
    wchar_t key[64] = L"";
    GetPrivateProfileStringW(L"autosave", L"TestKey", L"0x78", key, 64, path);
    s->testKey = static_cast<UINT>(wcstoul(key, nullptr, 0)) & 0xff;
    s->scanReplays = GetPrivateProfileIntW(L"autosave", L"ScanReplays", 1, path) != 0;
    s->tmxCopy = GetPrivateProfileIntW(L"autosave", L"TmxCopy", 1, path) != 0;
    s->onlineFix = GetPrivateProfileIntW(L"online", L"OnlineFix", 1, path) != 0;
    s->controllerFix = GetPrivateProfileIntW(L"controller", L"ControllerFix", 1, path) != 0;
    GetPrivateProfileStringW(L"menu", L"MenuKey", L"Ctrl+F2", key, 64, path);
    Hotkey k;
    if (parseHotkey(key, &k)) s->menuKey = k; // not understood: the default stays
    wchar_t v[32] = L"";
    GetPrivateProfileStringW(L"sound", L"Music", L"", v, 32, path);
    s->music = parsePercent(v);
    GetPrivateProfileStringW(L"sound", L"Sounds", L"", v, 32, path);
    s->sounds = parsePercent(v);
    GetPrivateProfileStringW(L"display", L"WindowMode", L"", v, 32, path);
    s->windowMode = parseWindowMode(v);
}

bool writeSetting(const wchar_t* path, const wchar_t* section, const wchar_t* key, const wchar_t* value) {
    if (!path || !path[0]) return false;
    return WritePrivateProfileStringW(section, key, value, path) != FALSE;
}

bool writeSettingBool(const wchar_t* path, const wchar_t* section, const wchar_t* key, bool on) {
    return writeSetting(path, section, key, on ? L"1" : L"0");
}

} // namespace autosave
