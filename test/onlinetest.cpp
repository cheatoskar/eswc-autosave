// Runs the real login fix code (vendor/horizon/host/onlinefix.cpp, the Horizon Client's) inside a test exe: its own import slot of
// HttpOpenRequestA stands in for the game's slot 0x00aca340. Then a read-only GET of the public
// server list over HTTPS, opened the way the game opens it. Expect "HTTP 200" with the fix and 12157
// without it (argument 0). Log: eswc_autosave.log next to this exe.
#include <windows.h>
#include <wininet.h>
#include <stdio.h>
#include <stdlib.h>

#include "../src/log.h"
#include "../src/shim/host/host.h"

static void** findIatSlot(const char* dll, const char* func) {
    BYTE* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (auto* d = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); d->Name; d++) {
        if (_stricmp(reinterpret_cast<char*>(base + d->Name), dll) != 0) continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + d->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + d->FirstThunk);
        for (; names->u1.AddressOfData; names++, slots++) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            auto* n = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (strcmp(reinterpret_cast<char*>(n->Name), func) == 0) return reinterpret_cast<void**>(&slots->u1.Function);
        }
    }
    return nullptr;
}

int main(int argc, char** argv) {
    const bool fix = argc < 2 || atoi(argv[1]) != 0;
    autosave::logOpen();
    if (fix) {
        void** slot = findIatSlot("WININET.dll", "HttpOpenRequestA");
        if (!slot) {
            printf("no import slot\n");
            return 2;
        }
        horizon::onlineFixInstall(reinterpret_cast<uintptr_t>(slot) - 0x00aca340 + 0x00400000, 0x00aca340);
    }
    HINTERNET s = InternetOpenA("GameBox", 0, nullptr, nullptr, 0);
    HINTERNET c = InternetConnectA(s, "game.trackmanianations.com", 443, nullptr, nullptr, INTERNET_SERVICE_HTTP, 0, 0);
    HINTERNET r = HttpOpenRequestA(c, "GET", "/online_game/getservers.php?ver=0.1.7.9&lang=en", "HTTP/1.1", nullptr,
                                   nullptr, 0x84000100 | 0xc0f000, 0);
    BOOL sent = HttpSendRequestA(r, nullptr, 0, nullptr, 0);
    if (!sent) {
        printf("fix=%d: HttpSendRequest failed, error %lu\n", fix, GetLastError());
        return 1;
    }
    DWORD status = 0, len = sizeof(status);
    HttpQueryInfoA(r, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &len, nullptr);
    char buf[256] = {};
    DWORD got = 0;
    InternetReadFile(r, buf, sizeof(buf) - 1, &got);
    printf("fix=%d: HTTP %lu: %s\n", fix, status, buf);
    return status == 200 ? 0 : 1;
}
