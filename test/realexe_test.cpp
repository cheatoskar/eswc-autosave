// Checks the vendored Horizon code against the real game exes on disk (read only, nothing is run):
//   - the controller fix's game table: PE timestamp, every code signature and table entry (GameBackend::verify) and
//     the late-pad bindings' signatures (Binder::verify)
//   - the login fix's import slots: ESWC 0x00aca340 and Original 0x00a162d4 are the exe's HttpOpenRequestA slot
//   - the menu's way in: the exe imports Direct3DCreate9 or GetProcAddress
// The exe image is laid out at its link address 0x400000 in this process (the exes have no relocations).
//
//   realexe_test.exe <exe> eswc|sunrise|original [...]
#include <windows.h>

#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "../vendor/horizon/controller-hotplug/hp_game.h"

static int g_fail = 0, g_pass = 0;
static void check(bool ok, const char* what) {
    if (ok) g_pass++;
    else g_fail++;
    printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
}

static std::string g_lastLog;
static void logCb(void*, int level, const char* text) {
    g_lastLog = text;
    printf("      [%s] %s\n", level == 2 ? "error" : level == 1 ? "warn" : "info", text);
}

// RVA of the IAT slot of dll!fn, 0 if not imported
static DWORD iatRva(const BYTE* img, const char* dll, const char* fn) {
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(img + reinterpret_cast<const IMAGE_DOS_HEADER*>(img)->e_lfanew);
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return 0;
    for (auto* d = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(img + dir.VirtualAddress); d->Name; d++) {
        if (_stricmp(reinterpret_cast<const char*>(img + d->Name), dll) != 0) continue;
        const DWORD names = d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk; // on disk FirstThunk holds the names too
        auto* n = reinterpret_cast<const IMAGE_THUNK_DATA32*>(img + names);
        for (DWORD i = 0; n[i].u1.AddressOfData; i++) {
            if (IMAGE_SNAP_BY_ORDINAL32(n[i].u1.Ordinal)) continue;
            auto* byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(img + n[i].u1.AddressOfData);
            if (strcmp(reinterpret_cast<const char*>(byName->Name), fn) == 0) return d->FirstThunk + i * 4;
        }
    }
    return 0;
}

static bool runOne(const wchar_t* path, const char* game) {
    printf("===== %ls (%s)\n", path, game);
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        check(false, "exe readable");
        return false;
    }
    std::vector<BYTE> file(GetFileSize(f, nullptr));
    DWORD got = 0;
    ReadFile(f, file.data(), static_cast<DWORD>(file.size()), &got, nullptr);
    CloseHandle(f);
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(file.data() + reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data())->e_lfanew);
    const uintptr_t base = nt->OptionalHeader.ImageBase;
    check(base == 0x400000, "link address 0x400000");
    // at the link address if that range is free here, else anywhere (then the few pointers the table checks are
    // moved by the same distance, as a loader would relocate them; the comparison stays "dword == link value")
    BYTE* img = static_cast<BYTE*>(VirtualAlloc(reinterpret_cast<void*>(base), nt->OptionalHeader.SizeOfImage, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!img) img = static_cast<BYTE*>(VirtualAlloc(nullptr, nt->OptionalHeader.SizeOfImage, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!img) {
        check(false, "memory for the image");
        return false;
    }
    memcpy(img, file.data(), nt->OptionalHeader.SizeOfHeaders);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++)
        if (sec[i].SizeOfRawData) memcpy(img + sec[i].VirtualAddress, file.data() + sec[i].PointerToRawData, sec[i].SizeOfRawData);
    const uintptr_t at = reinterpret_cast<uintptr_t>(img);
    printf("      image at %08X\n", static_cast<unsigned>(at));

    const hp::GameTable* t = hp::tableForTimestamp(nt->FileHeader.TimeDateStamp);
    check(t != nullptr, "controller fix: table for this PE timestamp");
    if (t) {
        if (at != base)
            for (int i = 0; i < t->ptrCount; i++) *reinterpret_cast<uint32_t*>(img + (t->ptrs[i].addr - base)) += static_cast<uint32_t>(at - base);
        hp::GameBackend backend(t, at, &logCb, nullptr);
        const bool v = backend.verify();
        char what[160];
        _snprintf_s(what, _TRUNCATE, "controller fix: %d code signatures + %d table entries match (%s)", t->codeCount, t->ptrCount, t->name);
        check(v, what);
        const hp::Binder* b = backend.binder();
        check(b && g_lastLog.find("bindings code checked") == 0, "controller fix: late-pad bindings code signatures match");
    }
    if (strcmp(game, "eswc") == 0 || strcmp(game, "original") == 0) {
        const DWORD want = strcmp(game, "eswc") == 0 ? 0x00aca340 - 0x400000 : 0x00a162d4 - 0x400000;
        check(iatRva(img, "WININET.dll", "HttpOpenRequestA") == want, "login fix: HttpOpenRequestA import slot as in essentials.cpp");
    }
    const bool d3d = iatRva(img, "d3d9.dll", "Direct3DCreate9") != 0;
    const bool gpa = iatRva(img, "KERNEL32.dll", "GetProcAddress") != 0;
    printf("      imports: Direct3DCreate9 %s, GetProcAddress %s\n", d3d ? "yes" : "no", gpa ? "yes" : "no");
    check(d3d || gpa, "menu: the exe reaches Direct3D through an import the overlay patches");
    VirtualFree(img, 0, MEM_RELEASE);
    return true;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) {
        printf("realexe_test.exe <exe> eswc|sunrise|original [...]\n");
        return 2;
    }
    for (int i = 1; i + 1 < argc; i += 2) {
        char game[16];
        WideCharToMultiByte(CP_UTF8, 0, argv[i + 1], -1, game, sizeof(game), nullptr, nullptr);
        runOne(argv[i], game);
    }
    printf("realexe_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
