#pragma once
#include <windows.h>

namespace autosave {

// SHA-256 of a file as 64 lowercase hex chars (+ NUL). Returns false on any error.
bool sha256File(const wchar_t* path, char out[65]);

} // namespace autosave
