// eswc_autosave.dll: the mod itself. Loaded and started by the dinput8.dll loader (loader.cpp)
// from the game's first DirectInput8Create call, on the game's main thread.
#include <windows.h>

#include "autosave.h"
#include "log.h"

namespace {
constexpr unsigned kLoaderApi = 1; // must match loader.cpp
}

// Returns 1 if the mod started (it may still stay inactive, e.g. on an unknown exe; see the log), 0 if not.
extern "C" int __cdecl EswcAutosaveInit(unsigned loaderApi) {
    if (loaderApi != kLoaderApi) {
        AS_LOG("loader api %u, expected %u: dinput8.dll and eswc_autosave.dll are from different versions, "
               "mod inactive",
               loaderApi, kLoaderApi);
        return 0;
    }
    autosave::initOnce();
    return 1;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        autosave::logOpen();
    }
    return TRUE;
}
