#include "header.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace autosave {

void checkReplayHeader(const wchar_t* path, char* out, size_t cap) {
    out[0] = 0;
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        _snprintf_s(out, cap, _TRUNCATE, "header: file not readable (%lu)", GetLastError());
        return;
    }
    static char buf[16384];
    DWORD got = 0;
    ReadFile(f, buf, sizeof(buf) - 1, &got, nullptr);
    CloseHandle(f);
    buf[got] = 0;
    for (DWORD i = 0; i < got; i++)
        if (!buf[i]) buf[i] = ' ';
    const bool gbx = got > 3 && memcmp(buf, "GBX", 3) == 0;
    const char* xml = strstr(buf, "<header type=\"replay\"");
    const char* val = strstr(buf, "validable=\"");
    const char* best = strstr(buf, "best=\"");
    const char* exever = strstr(buf, "exever=\"");
    const char v = val ? val[11] : '?';
    const int bestMs = best ? atoi(best + 6) : -1;
    char ver[16] = "?";
    if (exever) {
        size_t k = 0;
        for (const char* p = exever + 8; *p && *p != '"' && k < sizeof(ver) - 1; p++) ver[k++] = *p;
        ver[k] = 0;
    }
    _snprintf_s(out, cap, _TRUNCATE, "header: %s, xml %s, exever=%s, best=%d, validable=%c%s", gbx ? "GBX" : "NOT GBX",
                xml ? "ok" : "MISSING", ver, bestMs, v, (gbx && xml && v == '1') ? "" : "  <-- WARNING");
}

} // namespace autosave
