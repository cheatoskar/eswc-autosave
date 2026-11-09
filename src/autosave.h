#pragma once

namespace autosave {

// Called once from the proxied DirectInput8Create (game main thread, no loader lock):
// checks the exe build, reads the ini, installs the hooks.
void initOnce();

} // namespace autosave
