// Loads dist\dinput8.dll like the game does and calls DirectInput8Create.
// Expected: the proxy forwards to the system dinput8.dll (S_OK) and the log in this
// exe's folder says "UNKNOWN exe ... autosave off" (this is not the game).
#include <windows.h>
#include <stdio.h>

static const GUID kIID_IDirectInput8W = {0xbf798031, 0x483a, 0x4da2, {0xaa, 0x99, 0x5d, 0x64, 0xed, 0x36, 0x97, 0x00}};

int wmain(int argc, wchar_t** argv) {
    const wchar_t* dll = argc > 1 ? argv[1] : L"dinput8.dll";
    HMODULE m = LoadLibraryW(dll);
    if (!m) {
        printf("LoadLibrary failed %lu\n", GetLastError());
        return 1;
    }
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(m, path, MAX_PATH);
    wprintf(L"loaded %s\n", path);
    using Fn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, void*);
    Fn create = reinterpret_cast<Fn>(GetProcAddress(m, "DirectInput8Create"));
    if (!create) {
        printf("no DirectInput8Create\n");
        return 1;
    }
    void* di = nullptr;
    HRESULT hr = create(GetModuleHandleW(nullptr), 0x0800, kIID_IDirectInput8W, &di, nullptr);
    printf("DirectInput8Create hr=%08lX iface=%p\n", (unsigned long)hr, di);
    if (di) {
        IUnknown* u = static_cast<IUnknown*>(di);
        printf("Release -> %lu\n", u->Release());
    }
    const char* names[] = {"DllCanUnloadNow", "DllGetClassObject", "DllRegisterServer", "DllUnregisterServer",
                           "GetdfDIJoystick"};
    for (const char* n : names) printf("%s %s\n", n, GetProcAddress(m, n) ? "ok" : "MISSING");
    using FnJ = const void*(WINAPI*)();
    FnJ j = reinterpret_cast<FnJ>(GetProcAddress(m, "GetdfDIJoystick"));
    printf("GetdfDIJoystick() = %p\n", j ? j() : nullptr);
    printf("LegacyEssentialsLoader %s\n", GetProcAddress(m, "LegacyEssentialsLoader") ? "ok" : "MISSING");
    // chain: a dinput8.chain.dll next to the loader is loaded and gets the DirectInput calls
    HMODULE chain = GetModuleHandleW(L"dinput8.chain.dll");
    printf("dinput8.chain.dll %s\n", chain ? (chain == m ? "is this loader" : "loaded (chained)") : "not loaded");
    printf("eswc_autosave.dll %s\n", GetModuleHandleW(L"eswc_autosave.dll") ? "loaded" : "not loaded");
    return (hr == S_OK && di) ? 0 : 2;
}
