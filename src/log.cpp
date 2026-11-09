#include "log.h"

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>

namespace autosave {
namespace {
CRITICAL_SECTION g_lock;
HANDLE g_file = INVALID_HANDLE_VALUE;
bool g_init = false;
} // namespace

void logOpen() {
    if (g_init) return;
    InitializeCriticalSection(&g_lock);
    g_init = true;
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash) return;
    wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"eswc_autosave.log");
    g_file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
}

void logLine(const char* fmt, ...) {
    if (!g_init || g_file == INVALID_HANDLE_VALUE) return;
    char buf[1024];
    SYSTEMTIME t;
    GetLocalTime(&t);
    int len = _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%04u-%02u-%02u %02u:%02u:%02u.%03u  ", t.wYear, t.wMonth,
                          t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    if (len < 0) len = 0;
    va_list ap;
    va_start(ap, fmt);
    int m = _vsnprintf_s(buf + len, sizeof(buf) - len - 2, _TRUNCATE, fmt, ap);
    va_end(ap);
    len = (m < 0) ? (int)strlen(buf) : len + m;
    buf[len++] = '\r';
    buf[len++] = '\n';
    EnterCriticalSection(&g_lock);
    DWORD written = 0;
    WriteFile(g_file, buf, (DWORD)len, &written, nullptr);
    LeaveCriticalSection(&g_lock);
}

} // namespace autosave
