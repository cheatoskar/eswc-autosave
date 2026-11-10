// dinput8.dll: a small loader, nothing else.
//
// 1. Every DirectInput export is passed on, unchanged, to the system dinput8.dll.
// 2. The first time the game calls DirectInput8Create, it loads eswc_autosave.dll from the
//    folder this DLL is in (full path, no search order) and calls its EswcAutosaveInit.
//    All the mod's logic (autosave, TMX copy, online fix, log) lives in that DLL.
//
// Without eswc_autosave.dll next to it, this DLL only passes DirectInput through.
// It never reads or changes input data: DirectInput8Create returns the system's own object.
#include <windows.h>

namespace {
// Version of the call into eswc_autosave.dll; it refuses a loader it does not know.
constexpr unsigned kLoaderApi = 1;

HMODULE g_real = nullptr;
FARPROC g_procs[6] = {};
const char* const kNames[6] = {"DirectInput8Create", "DllCanUnloadNow",     "DllGetClassObject",
                               "DllRegisterServer",  "DllUnregisterServer", "GetdfDIJoystick"};
HMODULE g_self = nullptr;
volatile LONG g_modStarted = 0;

void debug(const wchar_t* msg, DWORD err) {
    wchar_t buf[256];
    wsprintfW(buf, L"eswc_autosave loader: %s (error %lu)\n", msg, err);
    OutputDebugStringW(buf);
}

bool loadReal() {
    if (g_real) return true;
    wchar_t path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH); // SysWOW64 for this 32-bit process (WOW64 redirection)
    if (n == 0 || n > MAX_PATH - 16) return false;
    lstrcatW(path, L"\\dinput8.dll");
    HMODULE real = LoadLibraryW(path);
    if (!real) {
        debug(L"could not load the system dinput8.dll", GetLastError());
        return false;
    }
    for (int i = 0; i < 6; i++) g_procs[i] = GetProcAddress(real, kNames[i]);
    g_real = real;
    return true;
}

FARPROC proc(int i) {
    if (!loadReal()) return nullptr;
    return g_procs[i];
}

// Loads <this DLL's folder>\eswc_autosave.dll once and starts it. Never unloaded: it installs hooks.
void startMod() {
    if (InterlockedExchange(&g_modStarted, 1) != 0) return;
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(g_self, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    wchar_t* slash = nullptr;
    for (wchar_t* p = path; *p; p++)
        if (*p == L'\\' || *p == L'/') slash = p;
    if (!slash || (slash + 1 - path) + 18 >= MAX_PATH) return;
    lstrcpyW(slash + 1, L"eswc_autosave.dll");
    HMODULE mod = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!mod) {
        debug(L"eswc_autosave.dll not loaded, DirectInput only", GetLastError());
        return;
    }
    using InitFn = int(__cdecl*)(unsigned);
    InitFn init = reinterpret_cast<InitFn>(GetProcAddress(mod, "EswcAutosaveInit"));
    if (!init) {
        debug(L"eswc_autosave.dll has no EswcAutosaveInit", GetLastError());
        return;
    }
    init(kLoaderApi);
}
} // namespace

extern "C" {

HRESULT WINAPI Proxy_DirectInput8Create(HINSTANCE inst, DWORD version, REFIID riid, LPVOID* out, void* outer) {
    // The game calls this from its main thread while it sets up input, long before a race,
    // and outside the loader lock, so loading another DLL here is safe.
    startMod();
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
        g_self = inst;
        DisableThreadLibraryCalls(inst);
    }
    return TRUE;
}
