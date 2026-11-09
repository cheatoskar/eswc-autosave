// dinput8.dll proxy: every export is passed on to the system dinput8.dll.
#include <windows.h>

#include "autosave.h"
#include "log.h"

namespace {
HMODULE g_real = nullptr;
FARPROC g_procs[6] = {};
const char* const kNames[6] = {"DirectInput8Create", "DllCanUnloadNow",     "DllGetClassObject",
                               "DllRegisterServer",  "DllUnregisterServer", "GetdfDIJoystick"};

bool loadReal() {
    if (g_real) return true;
    wchar_t path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH); // SysWOW64 for this 32-bit process (WOW64 redirection)
    if (n == 0 || n > MAX_PATH - 16) return false;
    wcscat_s(path, L"\\dinput8.dll");
    g_real = LoadLibraryW(path);
    if (!g_real) {
        AS_LOG("proxy: could not load the system dinput8.dll (error %lu)", GetLastError());
        return false;
    }
    for (int i = 0; i < 6; i++) g_procs[i] = GetProcAddress(g_real, kNames[i]);
    return true;
}

FARPROC proc(int i) {
    if (!loadReal()) return nullptr;
    return g_procs[i];
}
} // namespace

extern "C" {

HRESULT WINAPI Proxy_DirectInput8Create(HINSTANCE inst, DWORD version, REFIID riid, LPVOID* out, void* outer) {
    // The game calls this from its main thread while it sets up input, long before a race.
    autosave::initOnce();
    using Fn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, void*);
    Fn fn = reinterpret_cast<Fn>(proc(0));
    if (!fn) return E_FAIL;
    return fn(inst, version, riid, out, outer);
}

HRESULT WINAPI Proxy_DllCanUnloadNow() {
    using Fn = HRESULT(WINAPI*)();
    Fn fn = reinterpret_cast<Fn>(proc(1));
    return fn ? fn() : S_FALSE;
}

HRESULT WINAPI Proxy_DllGetClassObject(REFCLSID clsid, REFIID riid, LPVOID* out) {
    using Fn = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);
    Fn fn = reinterpret_cast<Fn>(proc(2));
    return fn ? fn(clsid, riid, out) : CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI Proxy_DllRegisterServer() {
    using Fn = HRESULT(WINAPI*)();
    Fn fn = reinterpret_cast<Fn>(proc(3));
    return fn ? fn() : E_FAIL;
}

HRESULT WINAPI Proxy_DllUnregisterServer() {
    using Fn = HRESULT(WINAPI*)();
    Fn fn = reinterpret_cast<Fn>(proc(4));
    return fn ? fn() : E_FAIL;
}

LPCVOID WINAPI Proxy_GetdfDIJoystick() {
    using Fn = LPCVOID(WINAPI*)();
    Fn fn = reinterpret_cast<Fn>(proc(5));
    return fn ? fn() : nullptr;
}

} // extern "C"

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        autosave::logOpen();
    }
    return TRUE;
}
