#pragma once
// Stand-in for horizon-host's src/host/host.h, only what vendor/horizon/host/onlinefix.cpp uses from it:
// its log macro and the declaration of onlineFixInstall. The vendored file itself stays byte for byte the
// client's (tools/sync-horizon.ps1 -Check).
#include <stdint.h>

#include "../../log.h"

#define HLOG(...) ::autosave::logLine(__VA_ARGS__)

namespace horizon {
// Login fix for ESWC and TM Original: TLS 1.0 for WinINet in this process and no revocation check on the
// game's HTTPS requests (iatSlot: link address of the exe's HttpOpenRequestA import slot).
void onlineFixInstall(uintptr_t exeBase, uintptr_t iatSlot);
} // namespace horizon
