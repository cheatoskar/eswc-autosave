#include "coexist.h"

#include <stdio.h>
#include <wchar.h>

namespace autosave {

Plan makePlan(const HorizonProbe& p, Stage stage, bool gameHasLoginFix, bool onlineFix, bool controllerFix, bool autosaveOn,
              bool menuKey) {
    Plan r;
    // At start the client is not loaded yet; its files and our place in the chain say it will be.
    // On the first message it has started (or not: Ctrl+Shift held at start, or it refused the exe).
    r.horizon = stage == Stage::Start ? (p.hostLoaded || (p.loaderIsChain && p.hostFile)) : p.hostLoaded;
    r.loginFix = !r.horizon && gameHasLoginFix && onlineFix;
    r.menu = !r.horizon && menuKey;
    r.controller = controllerFix; // the claim decides (see coexist.h)
    r.autosave = autosaveOn;      // the client's module steps aside for ours
    // the client's Game settings module (same code) applies its own values; an older client without it: ours
    r.gameSettings = !r.horizon || !p.hostGameSettings;
    return r;
}

HorizonProbe probeHorizon() {
    HorizonProbe p;
    wchar_t path[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* slash = (n && n < MAX_PATH) ? wcsrchr(path, L'\\') : nullptr;
    if (!slash || (slash + 1 - path) + 60 >= MAX_PATH) return p;
    wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"horizon\\host.dll");
    p.hostFile = GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
    p.hostLoaded = GetModuleHandleW(path) != nullptr;
    wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"horizon\\plugins\\game-settings\\game-settings.dll");
    p.hostGameSettings = GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
    // our loader as the chain: a module named dinput8.chain.dll that exports our loader's marker
    HMODULE chain = GetModuleHandleW(L"dinput8.chain.dll");
    p.loaderIsChain = chain && GetProcAddress(chain, "LegacyEssentialsLoader") != nullptr;
    return p;
}

void describeProbe(const HorizonProbe& p, char* out, size_t cap) {
    _snprintf_s(out, cap, _TRUNCATE, "loader %s, horizon\\host.dll %s%s%s", p.loaderIsChain ? "chained (dinput8.chain.dll)" : "is dinput8.dll",
                p.hostFile ? "there" : "not there", p.hostFile ? (p.hostLoaded ? ", loaded" : ", not loaded") : "",
                p.hostGameSettings ? ", its Game settings module there" : "");
}

} // namespace autosave
