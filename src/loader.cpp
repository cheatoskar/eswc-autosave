// dinput8.dll: the Legacy Essentials loader, a small loader, nothing else.
//
// 1. Every DirectInput export is passed on, unchanged, to dinput8.chain.dll if that file lies next to this
//    DLL and is not this DLL (another mod's dinput8.dll, renamed), else to the system dinput8.dll.
// 2. After the game's first DirectInput8Create returned (game main thread, outside the loader lock), it loads
//    eswc_autosave.dll from the folder this DLL is in (full path, no search order) and calls its
//    EswcAutosaveInit. All the mod's logic (autosave, controller fix, login fix, menu, log) lives in that DLL.
//
// Under the Horizon Legacy Loader this DLL is dinput8.chain.dll itself; its own chain lookup then finds itself
// and uses the system DLL. The export LegacyEssentialsLoader tells eswc_autosave.dll which loader it runs under.
// Without eswc_autosave.dll next to it, this DLL only passes DirectInput through.
// It never reads or changes input data: DirectInput8Create returns the chained or system DLL's own object.
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

// <this DLL's folder>\dinput8.chain.dll, if it is there and is not this very DLL.
HMODULE loadChain() {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(g_self, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return nullptr;
    wchar_t* slash = nullptr;
    for (wchar_t* p = path; *p; p++)
        if (*p == L'\\' || *p == L'/') slash = p;
    if (!slash || (slash + 1 - path) + 18 >= MAX_PATH) return nullptr;
    lstrcpyW(slash + 1, L"dinput8.chain.dll");
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) return nullptr;
    HMODULE chain = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!chain) {
        debug(L"dinput8.chain.dll could not be loaded, using the system dinput8.dll", GetLastError());
        return nullptr;
    }
    if (chain == g_self) return nullptr; // we are the chain of another loader (Horizon)
    return chain;
}

bool loadReal() {
    if (g_real) return true;
    HMODULE chain = loadChain();
    wchar_t path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH); // SysWOW64 for this 32-bit process (WOW64 redirection)
    if (n == 0 || n > MAX_PATH - 16) return false;
    lstrcatW(path, L"\\dinput8.dll");
    HMODULE real = LoadLibraryW(path);
    if (!real) {
        debug(L"could not load the system dinput8.dll", GetLastError());
        return false;
    }
    for (int i = 0; i < 6; i++) {
        FARPROC p = chain ? GetProcAddress(chain, kNames[i]) : nullptr;
        g_procs[i] = p ? p : GetProcAddress(real, kNames[i]);
    }
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
    // and outside the loader lock, so loading another DLL here is safe. The chained DLL first (another
    // loader starts its own mod in this call, on the game's original code), then the mod.
    using Fn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, void*);
    Fn fn = reinterpret_cast<Fn>(proc(0));
    const HRESULT hr = fn ? fn(inst, version, riid, out, outer) : E_FAIL;
    startMod();
    return hr;
}

// Marker: eswc_autosave.dll looks for this export to see that this loader runs as dinput8.chain.dll.
unsigned WINAPI Proxy_LegacyEssentialsLoader() { return kLoaderApi; }

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
