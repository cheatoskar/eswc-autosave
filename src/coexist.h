#pragma once
// Coexistence with the Horizon Client (horizon\host.dll, started by the Horizon Legacy Loader).
//
// The Horizon Legacy Loader is dinput8.dll; an earlier dinput8.dll (this mod's loader) is renamed to
// dinput8.chain.dll and called first, then horizon\host.dll is started in the same DirectInput8Create call.
// So when this mod starts, the client is not loaded yet: the decision is made twice, at start from the files
// (expected), and on the game thread's first message from what is really loaded.
//
// What runs where, when the client runs:
//   login fix        the client's (same code: vendor\horizon\host\onlinefix.cpp); ours stays off
//   controller fix   whoever claims it first in this process (Local\HorizonControllerHotplug.<pid>); the client's
//                    module claims it in its start, before our first message, so the client's runs
//   menu             the client's menu; ours stays off (no second Direct3D or DirectInput hook)
//   autosave         ours: the client's Autosave module pauses itself when it finds this mod (eswc_autosave.dll
//                    and dinput8.chain.dll), so there is only one saver either way
#include <windows.h>

namespace autosave {

struct HorizonProbe {
    bool loaderIsChain = false;  // our loader runs as dinput8.chain.dll (the Horizon Legacy Loader chains it)
    bool hostFile = false;       // <exe folder>\horizon\host.dll exists
    bool hostLoaded = false;     // that DLL is loaded in this process
};

struct Plan {
    bool horizon = false;     // the client runs in this process (or is about to start)
    bool loginFix = false;    // install our login fix
    bool menu = false;        // our menu and its Direct3D / DirectInput hooks
    bool controller = false;  // try to claim and run our controller fix
    bool autosave = false;    // our autosave hooks
};

enum class Stage { Start, FirstMessage };

// gameHasLoginFix: ESWC and Original. menuKey: a menu key is set.
Plan makePlan(const HorizonProbe& p, Stage stage, bool gameHasLoginFix, bool onlineFix, bool controllerFix, bool autosaveOn,
              bool menuKey);

HorizonProbe probeHorizon();

// One line for the log: what the probe saw.
void describeProbe(const HorizonProbe& p, char* out, size_t cap);

} // namespace autosave
