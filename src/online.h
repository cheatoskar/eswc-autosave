#pragma once
#include <stdint.h>

namespace autosave {

// Lets the game's own WinINet HTTPS requests reach game.trackmanianations.com again:
// that server only speaks TLS 1.0, which Windows 11 no longer offers to WinINet by default
// (error 12157). Only this process is affected. Details in online.cpp.
// iatSlot: link address of the exe's HttpOpenRequestA import slot (ESWC 0x00aca340, Original 0x00a162d4).
// TM Original (Online Patch v2) gets the same two transport changes; its readme asks for them system-wide.
void onlineFixInstall(uintptr_t exeBase, uintptr_t iatSlot);

} // namespace autosave
