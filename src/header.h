#pragma once
#include <stddef.h>

namespace autosave {

// Reads the header the game wrote (the XML chunk) and reports what matters for validation
// (GBX magic, exever, best, validable). Only reads the file.
void checkReplayHeader(const wchar_t* path, char* out, size_t cap);

} // namespace autosave
