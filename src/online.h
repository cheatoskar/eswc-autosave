#pragma once
#include <stdint.h>

namespace autosave {

// Lets the game's own WinINet HTTPS requests reach game.trackmanianations.com again:
// that server only speaks TLS 1.0, which Windows 11 no longer offers to WinINet by default
// (error 12157). Only this process is affected. Details in online.cpp.
void onlineFixInstall(uintptr_t exeBase);

} // namespace autosave
