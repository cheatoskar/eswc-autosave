// Runs the mod's TMX-copy conversion (src\tmxcopy.cpp) on replay files outside the game.
//
//   tmxcopy_test.exe <outdir> [--login L] [--time ms] [--ref DIR] <replay.Replay.Gbx>...
//
// <outdir> "-" writes next to each input like the mod does: only use it on copies.
// For each input: a summary of the input, the result (converted / not needed / refused / error),
// for converted files a summary of the output and, with --ref, a byte comparison with
// "<DIR>\<input stem> solo.Replay.Gbx" (the reference converter's output naming).
// The outputs are written to <outdir> only; inputs are opened read-only.
// Exit code: number of inputs that ended in Status::Error (failed I/O or verification).
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "../src/tmxcopy.h"

using autosave::tmxcopy::Report;
using autosave::tmxcopy::Status;

namespace {

void utf8(const wchar_t* w, char* out, int cap) {
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, out, cap, nullptr, nullptr)) out[0] = 0;
}

const char* statusName(Status s) {
    switch (s) {
    case Status::Converted: return "CONVERTED";
    case Status::NotNeeded: return "NOT NEEDED";
    case Status::Refused: return "REFUSED";
    default: return "ERROR";
    }
}

bool readAll(const wchar_t* path, unsigned char** data, size_t* n) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size = {};
    bool ok = GetFileSizeEx(h, &size) && size.QuadPart < (256 << 20);
    *n = ok ? static_cast<size_t>(size.QuadPart) : 0;
    *data = ok ? static_cast<unsigned char*>(malloc(*n + 1)) : nullptr;
    DWORD got = 0;
    ok = ok && *data && ReadFile(h, *data, static_cast<DWORD>(*n), &got, nullptr) && got == *n;
    CloseHandle(h);
    return ok;
}

// Compares two files byte for byte. Returns "IDENTICAL", "DIFFERENT ..." or "ref missing".
void compare(const wchar_t* a, const wchar_t* b, char* out, size_t cap) {
    unsigned char *da = nullptr, *db = nullptr;
    size_t na = 0, nb = 0;
    if (!readAll(b, &db, &nb)) {
        _snprintf_s(out, cap, _TRUNCATE, "reference file missing");
    } else if (!readAll(a, &da, &na)) {
        _snprintf_s(out, cap, _TRUNCATE, "output not readable");
    } else if (na == nb && memcmp(da, db, na) == 0) {
        _snprintf_s(out, cap, _TRUNCATE, "IDENTICAL (%zu bytes)", na);
    } else {
        size_t i = 0;
        while (i < na && i < nb && da[i] == db[i]) i++;
        _snprintf_s(out, cap, _TRUNCATE, "DIFFERENT (sizes %zu/%zu, first difference at %zu)", na, nb, i);
    }
    free(da);
    free(db);
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc < 3) {
        printf("usage: tmxcopy_test.exe <outdir|-> [--login L] [--time ms] [--ref DIR] <replay.Replay.Gbx>...\n");
        return 100;
    }
    // "-" = next to each input, exactly as the mod does it (only use that on copies of replays)
    const wchar_t* outDir = wcscmp(argv[1], L"-") == 0 ? nullptr : argv[1];
    if (outDir) CreateDirectoryW(outDir, nullptr);
    char login[128] = "";
    int expectedTime = -1;
    const wchar_t* refDir = nullptr;
    int errors = 0;
    for (int i = 2; i < argc; i++) {
        if (wcscmp(argv[i], L"--login") == 0 && i + 1 < argc) {
            utf8(argv[++i], login, sizeof(login));
            continue;
        }
        if (wcscmp(argv[i], L"--time") == 0 && i + 1 < argc) {
            expectedTime = _wtoi(argv[++i]);
            continue;
        }
        if (wcscmp(argv[i], L"--ref") == 0 && i + 1 < argc) {
            refDir = argv[++i];
            continue;
        }
        const wchar_t* in = argv[i];
        const wchar_t* slash = wcsrchr(in, L'\\');
        const wchar_t* slash2 = wcsrchr(in, L'/');
        if (slash2 > slash) slash = slash2;
        const wchar_t* fileName = slash ? slash + 1 : in;
        char name[1200], desc[600];
        utf8(fileName, name, sizeof(name));
        printf("== %s\n", name);
        autosave::tmxcopy::describe(in, desc, sizeof(desc));
        printf("   input : %s\n", desc);

        Report* r = new Report();
        autosave::tmxcopy::makeCopy(in, outDir, login, expectedTime, r);
        printf("   result: %s%s%s  (%.2f ms)\n", statusName(r->status), r->reason[0] ? ": " : "", r->reason, r->ms);
        if (r->status == Status::Error) errors++;
        if (r->status == Status::Converted) {
            char path[3000];
            utf8(r->outPath, path, sizeof(path));
            printf("   output: %s\n", path);
            printf("           ghosts %u->1 kept #%d time=%d EventsDuration=%u numNodes %u->%u validable=%d\n",
                   r->ghostsBefore, r->keptIndex, r->time, r->eventsDuration, r->nodesBefore, r->nodesAfter,
                   r->validable);
            autosave::tmxcopy::describe(r->outPath, desc, sizeof(desc));
            printf("   reread: %s\n", desc);
            if (refDir) {
                wchar_t stem[600], ref[1200];
                const size_t ext = wcslen(L".Replay.Gbx");
                const size_t sl = wcslen(fileName) > ext ? wcslen(fileName) - ext : 0;
                if (sl < _countof(stem)) {
                    wmemcpy(stem, fileName, sl);
                    stem[sl] = 0;
                    _snwprintf_s(ref, _countof(ref), _TRUNCATE, L"%s\\%s solo.Replay.Gbx", refDir, stem);
                    char cmp[200];
                    compare(r->outPath, ref, cmp, sizeof(cmp));
                    printf("   vs ref: %s\n", cmp);
                }
            }
        }
        delete r;
    }
    return errors;
}
