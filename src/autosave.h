#pragma once

namespace autosave {

// Called once via EswcAutosaveInit, which the dinput8.dll loader calls from the game's first
// DirectInput8Create (game main thread, no loader lock):
// checks the exe build, reads the ini, installs the hooks.
void initOnce();

} // namespace autosave
