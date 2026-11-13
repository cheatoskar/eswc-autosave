#pragma once
#include <stddef.h>
#include <stdint.h>

namespace autosave {

// A 5-byte jump at the start of a game function, with a trampoline that runs the overwritten bytes
// and jumps back. Installed only when the function starts with exactly `prologue` (whole instructions,
// at least 5 bytes, no relative operands); otherwise nothing is written.
struct InlineHook {
    uintptr_t link;               // link address (image base 0x400000)
    unsigned char prologue[16];
    size_t length;
    const char* name;
    void* trampoline;             // set when installed
};

// base: the exe's load address (the link address is rebased with it).
bool installHook(InlineHook& h, void* detour, uintptr_t base);

} // namespace autosave
