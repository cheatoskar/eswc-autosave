#include "hook.h"

#include <windows.h>
#include <string.h>

#include "log.h"

namespace autosave {

bool installHook(InlineHook& h, void* detour, uintptr_t base) {
    if (h.length < 5 || h.length > sizeof(h.prologue)) return false;
    BYTE* target = reinterpret_cast<BYTE*>(base - 0x00400000 + h.link);
    if (memcmp(target, h.prologue, h.length) != 0) {
        AS_LOG("hook: %s at %08X does not match, not hooked", h.name, (unsigned)h.link);
        return false;
    }
    BYTE* tramp = static_cast<BYTE*>(VirtualAlloc(nullptr, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!tramp) return false;
    memcpy(tramp, target, h.length);
    tramp[h.length] = 0xE9;
    *reinterpret_cast<int32_t*>(tramp + h.length + 1) = static_cast<int32_t>((target + h.length) - (tramp + h.length + 5));
    FlushInstructionCache(GetCurrentProcess(), tramp, 32);
    DWORD old = 0;
    if (!VirtualProtect(target, h.length, PAGE_EXECUTE_READWRITE, &old)) return false;
    target[0] = 0xE9;
    *reinterpret_cast<int32_t*>(target + 1) = static_cast<int32_t>(static_cast<BYTE*>(detour) - (target + 5));
    for (size_t i = 5; i < h.length; i++) target[i] = 0x90;
    VirtualProtect(target, h.length, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, h.length);
    h.trampoline = tramp;
    AS_LOG("hook: %s at %08X installed", h.name, (unsigned)h.link);
    return true;
}

} // namespace autosave
