// Tests for the Sunrise / Original solo autosave (src/legacy.cpp), without the game:
//   1. exe table: the three known exes are recognised only with hash + timestamp + size; with the
//      real exes on disk (optional, read only) their hashes and the hook prologues are checked too.
//   2. a fake game per exe: an image at the exe's link addresses with the game functions the save calls
//      as stubs, and OnFinishLine / the solo race end with the real prologues, hooked by legacy::start.
//      Driving finishes through the hooked functions checks: save only after a local finish that is a
//      new best, the game's name "<map>(mm'ss''cc).Replay.Gbx" in My Replays, no overwrite, the save
//      arguments (10, 7, 1, 1), the replay object left as the game made it, the file left as the
//      game's save wrote it (validable untouched).
//   legacy_test.exe [<exe path> sunrise|original|eswc ...]   (real exes are only read; none given: skipped)
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "../src/games.h"
#include "../src/legacy.h"
#include "../src/log.h"
#include "../src/sha256.h"

using namespace autosave;

namespace {

int g_fail = 0, g_pass = 0;
void check(bool ok, const char* what) {
    printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) g_pass++;
    else g_fail++;
}

// ---------------------------------------------------------------- unit tests
void testTable() {
    int n = 0;
    const ExeId* k = knownExes(&n);
    check(n == 3, "three known exes");
    for (int i = 0; i < n; i++) {
        check(identifyExe(k[i].sha256, k[i].timestamp, k[i].size) == &k[i], k[i].name);
        check(identifyExe(k[i].sha256, k[i].timestamp + 1, k[i].size) == nullptr, "  other PE timestamp refused");
        check(identifyExe(k[i].sha256, k[i].timestamp, k[i].size + 1) == nullptr, "  other file size refused");
    }
    check(identifyExe("0000000000000000000000000000000000000000000000000000000000000000", 0x5cc8f0cf, 7904768) == nullptr,
          "unknown hash refused");
    check(identifyExe("e59dfc7eaa5314e85b2b0df6f40ef7ba01a23bd5a9525f84407f5dff301df4f", 0x5cc8f0cf, 7904768) == nullptr,
          "hash prefix refused");
    check(legacyGame(Game::Sunrise) && legacyGame(Game::Original) && !legacyGame(Game::Eswc), "legacy tables for Sunrise and Original only");
    wchar_t s[40];
    formatRaceTime(61410, s, 40);
    check(wcscmp(s, L"(01'01''41)") == 0, "61410 ms -> (01'01''41)");
    formatRaceTime(46259, s, 40);
    check(wcscmp(s, L"(00'46''25)") == 0, "46259 ms -> (00'46''25) (cut, not rounded, like the game)");
    formatRaceTime(0, s, 40);
    check(wcscmp(s, L"(00'00''00)") == 0, "0 ms");
    formatRaceTime(-1000, s, 40);
    check(wcscmp(s, L"(00'01''00)") == 0, "negative time by its absolute value (as the game)");
}

// Real exe on disk (optional): hash/size/timestamp and the bytes at every address the code uses.
struct PeFile {
    std::vector<unsigned char> d;
    uint32_t base = 0, stamp = 0;
    struct Sec { char name[9]; uint32_t va, vsize, raw, rsize; };
    std::vector<Sec> secs;
    bool load(const wchar_t* path) {
        HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (f == INVALID_HANDLE_VALUE) return false;
        DWORD size = GetFileSize(f, nullptr), got = 0;
        d.resize(size);
        ReadFile(f, d.data(), size, &got, nullptr);
        CloseHandle(f);
        if (got != size || size < 0x400) return false;
        const uint32_t pe = *reinterpret_cast<uint32_t*>(&d[0x3c]);
        stamp = *reinterpret_cast<uint32_t*>(&d[pe + 8]);
        const uint16_t nsec = *reinterpret_cast<uint16_t*>(&d[pe + 6]);
        const uint16_t opt = *reinterpret_cast<uint16_t*>(&d[pe + 20]);
        base = *reinterpret_cast<uint32_t*>(&d[pe + 24 + 28]);
        for (int i = 0; i < nsec; i++) {
            const unsigned char* s = &d[pe + 24 + opt + i * 40];
            Sec x = {};
            memcpy(x.name, s, 8);
            x.vsize = *reinterpret_cast<const uint32_t*>(s + 8);
            x.va = *reinterpret_cast<const uint32_t*>(s + 12);
            x.rsize = *reinterpret_cast<const uint32_t*>(s + 16);
            x.raw = *reinterpret_cast<const uint32_t*>(s + 20);
            secs.push_back(x);
        }
        return true;
    }
    const Sec* secOf(uint32_t va) const {
        for (const auto& s : secs)
            if (va >= base + s.va && va < base + s.va + (s.vsize > s.rsize ? s.vsize : s.rsize)) return &s;
        return nullptr;
    }
    const unsigned char* at(uint32_t va) const {
        const Sec* s = secOf(va);
        if (!s || va - base - s->va >= s->rsize) return nullptr;
        return &d[s->raw + va - base - s->va];
    }
};

void testRealExe(const wchar_t* path, Game expect) {
    PeFile pe;
    char label[400];
    char narrow[300];
    WideCharToMultiByte(CP_UTF8, 0, path, -1, narrow, sizeof(narrow), nullptr, nullptr);
    if (!pe.load(path)) {
        printf("SKIP  %s not found\n", narrow);
        return;
    }
    char hash[65];
    check(sha256File(path, hash), "sha256 of the real exe");
    const ExeId* id = identifyExe(hash, pe.stamp, pe.d.size());
    _snprintf_s(label, _TRUNCATE, "real exe %s recognised as the right game", narrow);
    check(id && id->game == expect, label);
    const LegacyGame* g = legacyGame(expect);
    if (!g) return;
    const unsigned char* f = pe.at(static_cast<uint32_t>(g->onFinishLine));
    check(f && memcmp(f, g->finishPrologue, sizeof(g->finishPrologue)) == 0, "  OnFinishLine prologue matches the exe");
    const unsigned char* e = pe.at(static_cast<uint32_t>(g->soloRaceEnd));
    check(e && memcmp(e, g->soloEndPrologue, sizeof(g->soloEndPrologue)) == 0, "  solo race end prologue matches the exe");
    const uintptr_t fns[] = {g->localPlayerInfo, g->strSet, g->strEmpty, g->filterFileName, g->fileExists, g->bestGhostTime,
                             g->bestStuntScore, g->archiveCtor, g->archiveSave, g->archiveDtor, g->unbindFid, g->scanReplays};
    bool allText = true;
    for (uintptr_t a : fns) {
        const PeFile::Sec* s = pe.secOf(static_cast<uint32_t>(a));
        const unsigned char* p = pe.at(static_cast<uint32_t>(a));
        // in .text and a function start (the byte before is padding or a ret)
        if (!s || strcmp(s->name, ".text") != 0 || !p || !(p[-1] == 0x90 || p[-1] == 0xcc || p[-1] == 0xc3 || p[-3] == 0xc2))
            allText = false;
    }
    check(allText, "  every called function starts in .text after padding/ret");
    const PeFile::Sec* s1 = pe.secOf(static_cast<uint32_t>(g->emptyWideString));
    const PeFile::Sec* s2 = pe.secOf(static_cast<uint32_t>(g->mainEngine));
    check(s1 && s2 && strcmp(s1->name, ".data") == 0 && strcmp(s2->name, ".data") == 0, "  globals are in .data");
}

// ---------------------------------------------------------------- fake game
namespace fake {

constexpr size_t kImage = 0x900000; // covers link addresses 0x400000 .. 0xd00000
unsigned char* g_img = nullptr;
const LegacyGame* g_game = nullptr;
uintptr_t at(uintptr_t link) { return reinterpret_cast<uintptr_t>(g_img) - 0x400000 + link; }

struct FastStringInt { uint32_t len; wchar_t* ptr; };
struct StringParamInt { const wchar_t* ptr; uint32_t len; uint32_t pad; };

wchar_t kEmpty[1] = {0};
std::wstring g_dir; // the fake "My Replays" folder (trailing backslash)

// objects
alignas(16) unsigned char app[0x400], race[0x100], player[0x80], info[0x300], otherInfo[0x300], replay[0x200], challenge[0x200];
alignas(16) unsigned char engineMain[0x40], engineSub[0x40], systemEngine[0x40], folder[0x20];
void* folderVt[64];
wchar_t mapName[64] = L"$o$f00Test Map";

// what the fake game does / records
unsigned g_runTime = 0;
int g_stuntScore = 0;
bool g_clearRecording = true;
int g_archSaves = 0, g_ctor = 0, g_dtor = 0, g_scan = 0, g_unbind = 0, g_live = 0;
void* g_unbindNod = nullptr;
int g_saveArgs[4] = {};
void* g_saveNod = nullptr;
void* g_saveFolder = nullptr;
std::wstring g_lastWritten;
std::string g_lastContent;

void freeStr(FastStringInt* s) {
    if (s->ptr && s->ptr != kEmpty) {
        free(s->ptr);
        g_live--;
    }
    s->ptr = kEmpty;
    s->len = 0;
}
void* __fastcall localInfo(void* r, void*) { return r == race ? info : nullptr; }
void __fastcall strSet(FastStringInt* self, void*, const StringParamInt* p) {
    wchar_t* n = static_cast<wchar_t*>(malloc((p->len + 1) * sizeof(wchar_t)));
    g_live++;
    memcpy(n, p->ptr, p->len * sizeof(wchar_t));
    n[p->len] = 0;
    freeStr(self);
    self->ptr = n;
    self->len = p->len;
}
void __fastcall strEmpty(FastStringInt* self, void*) { freeStr(self); }
void __cdecl filter(FastStringInt* dst, const FastStringInt* src, int, int) {
    std::wstring s(src->ptr, src->len);
    for (auto& c : s)
        if (wcschr(L"\\/:*?\"<>|", c)) c = L'_';
    StringParamInt p = {s.c_str(), static_cast<uint32_t>(s.size()), 0};
    strSet(dst, nullptr, &p);
}
int __cdecl exists(const FastStringInt* name, void* f) {
    if (f != folder) return 1;
    std::wstring path = g_dir + std::wstring(name->ptr, name->len);
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}
void __fastcall bestTime(void* r, void*, int* t, int* o) {
    *t = r == replay ? static_cast<int>(*reinterpret_cast<unsigned*>(info + legacy_off::kInfoBest)) : -1;
    *o = 0;
}
int __fastcall bestScore(void*, void*) { return g_stuntScore; }
void __fastcall archCtor(void*, void*) { g_ctor++; }
void __fastcall archDtor(void*, void*) { g_dtor++; }
int __fastcall archSave(void*, void*, FastStringInt* name, void* nod, void* f, int a, int b, int c, int d) {
    g_archSaves++;
    g_saveArgs[0] = a, g_saveArgs[1] = b, g_saveArgs[2] = c, g_saveArgs[3] = d;
    g_saveNod = nod;
    g_saveFolder = f;
    g_lastWritten = g_dir + std::wstring(name->ptr, name->len);
    char xml[300];
    _snprintf_s(xml, _TRUNCATE, "<header type=\"replay\" version=\"TMr.4\" exever=\"0.1.5.0\"><times best=\"%u\" respawns=\"0\" validable=\"1\"/></header>",
                *reinterpret_cast<unsigned*>(info + legacy_off::kInfoBest));
    g_lastContent.assign("GBX\x06\x00", 5);
    g_lastContent += xml;
    HANDLE h = CreateFileW(g_lastWritten.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD w = 0;
    WriteFile(h, g_lastContent.data(), static_cast<DWORD>(g_lastContent.size()), &w, nullptr);
    CloseHandle(h);
    return 1;
}
void __fastcall unbind(void* sys, void*, void* nod) {
    if (sys == systemEngine) g_unbind++;
    g_unbindNod = nod;
}
void __fastcall scan(void* a, void*) {
    if (a == app) g_scan++;
}
void __fastcall getFullPath(void*, void*, FastStringInt* out) {
    StringParamInt p = {g_dir.c_str(), static_cast<uint32_t>(g_dir.size()), 0};
    strSet(out, nullptr, &p);
}
// the game's own OnFinishLine / solo race end (what they do with the state the autosave reads)
void __fastcall gameFinish(void* r, void*, void* pl) {
    unsigned char* in = *reinterpret_cast<unsigned char**>(static_cast<unsigned char*>(pl) + legacy_off::kPlayerInfo);
    if (*reinterpret_cast<int*>(in + legacy_off::kInfoRaceState) != 1) return;
    *reinterpret_cast<unsigned*>(in + legacy_off::kInfoTime) = g_runTime;
    unsigned& best = *reinterpret_cast<unsigned*>(in + legacy_off::kInfoBest);
    if (g_runTime < best) best = g_runTime;
    *reinterpret_cast<int*>(in + legacy_off::kInfoRaceState) = 2;
    (void)r;
}
void __fastcall gameSoloEnd(void* r, void*) {
    if (g_clearRecording) *reinterpret_cast<int*>(replay + legacy_off::kReplayRecording) = 0;
    (void)r;
}

void jmpTo(uintptr_t from, const void* to) {
    unsigned char* p = reinterpret_cast<unsigned char*>(from);
    p[0] = 0xE9;
    *reinterpret_cast<int32_t*>(p + 1) = static_cast<int32_t>(reinterpret_cast<uintptr_t>(to) - (from + 5));
}

// A fresh image with the game's functions at their link addresses.
void build(const LegacyGame& g) {
    if (g_img) VirtualFree(g_img, 0, MEM_RELEASE);
    g_img = static_cast<unsigned char*>(VirtualAlloc(nullptr, kImage, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    memset(g_img, 0xCC, kImage);
    g_game = &g;
    jmpTo(at(g.localPlayerInfo), reinterpret_cast<void*>(&localInfo));
    jmpTo(at(g.strSet), reinterpret_cast<void*>(&strSet));
    jmpTo(at(g.strEmpty), reinterpret_cast<void*>(&strEmpty));
    jmpTo(at(g.filterFileName), reinterpret_cast<void*>(&filter));
    jmpTo(at(g.fileExists), reinterpret_cast<void*>(&exists));
    jmpTo(at(g.bestGhostTime), reinterpret_cast<void*>(&bestTime));
    jmpTo(at(g.bestStuntScore), reinterpret_cast<void*>(&bestScore));
    jmpTo(at(g.archiveCtor), reinterpret_cast<void*>(&archCtor));
    jmpTo(at(g.archiveSave), reinterpret_cast<void*>(&archSave));
    jmpTo(at(g.archiveDtor), reinterpret_cast<void*>(&archDtor));
    jmpTo(at(g.unbindFid), reinterpret_cast<void*>(&unbind));
    jmpTo(at(g.scanReplays), reinterpret_cast<void*>(&scan));
    // OnFinishLine: the real prologue, then undo its pushes and continue in gameFinish
    unsigned char* f = reinterpret_cast<unsigned char*>(at(g.onFinishLine));
    memcpy(f, g.finishPrologue, sizeof(g.finishPrologue));
    unsigned char popF[] = {0x5e, 0x5d, 0x5b}; // pop esi, pop ebp, pop ebx
    memcpy(f + 11, popF, sizeof(popF));
    jmpTo(reinterpret_cast<uintptr_t>(f + 14), reinterpret_cast<void*>(&gameFinish));
    unsigned char* e = reinterpret_cast<unsigned char*>(at(g.soloRaceEnd));
    memcpy(e, g.soloEndPrologue, sizeof(g.soloEndPrologue));
    unsigned char popE[] = {0x5f, 0x5e, 0x5d, 0x5b, 0x83, 0xc4, 0x08}; // pop edi/esi/ebp/ebx, add esp 8
    memcpy(e + 7, popE, sizeof(popE));
    jmpTo(reinterpret_cast<uintptr_t>(e + 14), reinterpret_cast<void*>(&gameSoloEnd));
    // globals
    static wchar_t* emptyPtr = kEmpty;
    *reinterpret_cast<wchar_t**>(at(g.emptyWideString)) = emptyPtr;
    *reinterpret_cast<void**>(at(g.mainEngine)) = engineMain;
    *reinterpret_cast<void**>(engineMain + 0x28) = engineSub;
    *reinterpret_cast<void**>(engineSub + 0x2c) = systemEngine;
    // objects
    memset(app, 0, sizeof(app));
    memset(race, 0, sizeof(race));
    memset(info, 0, sizeof(info));
    memset(otherInfo, 0, sizeof(otherInfo));
    memset(replay, 0, sizeof(replay));
    memset(challenge, 0, sizeof(challenge));
    for (auto& s : folderVt) s = nullptr;
    folderVt[legacy_off::kFidsGetFullPath / 4] = reinterpret_cast<void*>(&getFullPath);
    *reinterpret_cast<void**>(folder) = folderVt;
    *reinterpret_cast<void**>(race + legacy_off::kRaceApp) = app;
    *reinterpret_cast<void**>(player + legacy_off::kPlayerInfo) = info;
    *reinterpret_cast<void**>(app + legacy_off::kAppReplay) = replay;
    *reinterpret_cast<void**>(app + legacy_off::kAppMyReplays) = folder;
    *reinterpret_cast<void**>(replay + legacy_off::kReplayChallenge) = challenge;
    *reinterpret_cast<uint32_t*>(challenge + legacy_off::kChallengeName) = static_cast<uint32_t>(wcslen(mapName));
    *reinterpret_cast<wchar_t**>(challenge + legacy_off::kChallengeName + 4) = mapName;
    *reinterpret_cast<unsigned*>(info + legacy_off::kInfoBest) = 0xffffffffu;
    g_archSaves = g_ctor = g_dtor = g_scan = g_unbind = 0;
}

using FnFinish = void(__fastcall*)(void*, void*, void*);
using FnEnd = void(__fastcall*)(void*, void*);

// One run of the local player: start (state 1, recording), finish with `time`, then the race end.
void run(unsigned time, bool finish = true, bool raceEnd = true, void* who = player) {
    *reinterpret_cast<int*>(info + legacy_off::kInfoRaceState) = 1;
    *reinterpret_cast<int*>(otherInfo + legacy_off::kInfoRaceState) = 1;
    *reinterpret_cast<int*>(replay + legacy_off::kReplayRecording) = 1;
    g_runTime = time;
    if (finish) reinterpret_cast<FnFinish>(at(g_game->onFinishLine))(race, nullptr, who);
    if (raceEnd) reinterpret_cast<FnEnd>(at(g_game->soloRaceEnd))(race, nullptr);
}

std::string readFile(const std::wstring& p) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return "";
    char buf[1024];
    DWORD got = 0;
    ReadFile(h, buf, sizeof(buf), &got, nullptr);
    CloseHandle(h);
    return std::string(buf, got);
}

} // namespace fake

void clearDir(const std::wstring& dir) {
    CreateDirectoryW(dir.c_str(), nullptr);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) DeleteFileW((dir + fd.cFileName).c_str());
    while (FindNextFileW(h, &fd));
    FindClose(h);
}

bool fileThere(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

void testFakeGame(Game which, const std::wstring& root) {
    const LegacyGame& g = *legacyGame(which);
    printf("===== fake %s\n", g.name);
    fake::g_dir = root + (which == Game::Sunrise ? L"sunrise-My Replays\\" : L"original-My Replays\\");
    clearDir(fake::g_dir);
    fake::build(g);
    legacy::Config cfg; // Mode=best, ScanReplays=1
    cfg.testKey = 0;
    // A wrong base (link addresses shifted by 0x10): the prologues do not match there.
    check(legacy::start(g, reinterpret_cast<uintptr_t>(fake::g_img) + 0x10, cfg, 0) == false,
          "wrong base (other bytes at the addresses): nothing hooked");
    check(memcmp(reinterpret_cast<void*>(fake::at(g.onFinishLine)), g.finishPrologue, 11) == 0 &&
              memcmp(reinterpret_cast<void*>(fake::at(g.soloRaceEnd)), g.soloEndPrologue, 7) == 0,
          "  the fake image is unchanged after the refused start");
    fake::build(g);
    const uintptr_t base = reinterpret_cast<uintptr_t>(fake::g_img); // link 0x400000 is at the image start
    check(legacy::start(g, base, cfg, 0), "hooks installed on the fake image (prologues match)");
    check(*reinterpret_cast<unsigned char*>(fake::at(g.onFinishLine)) == 0xE9 &&
              *reinterpret_cast<unsigned char*>(fake::at(g.soloRaceEnd)) == 0xE9,
          "  OnFinishLine and the race end start with the hook jump");

    // 1. first finish = new best -> saved
    unsigned char replayBefore[sizeof(fake::replay)];
    fake::run(46259, true, false);
    memcpy(replayBefore, fake::replay, sizeof(replayBefore));
    replayBefore[legacy_off::kReplayRecording] = 0; // the race end stops the recording (the game, not us)
    reinterpret_cast<fake::FnEnd>(fake::at(g.soloRaceEnd))(fake::race, nullptr);
    const std::wstring first = fake::g_dir + L"$o$f00Test Map(00'46''25).Replay.Gbx";
    check(legacy::stats().saved == 1 && fileThere(first), "first finish (new best): saved as <map>(00'46''25).Replay.Gbx in My Replays");
    check(fake::g_saveArgs[0] == 10 && fake::g_saveArgs[1] == 7 && fake::g_saveArgs[2] == 1 && fake::g_saveArgs[3] == 1 &&
              fake::g_saveNod == fake::replay && fake::g_saveFolder == fake::folder,
          "  CSystemArchiveNod::Save(name, replay, My Replays, 10, 7, 1, 1) as the game calls it");
    check(fake::g_ctor == 1 && fake::g_dtor == 1, "  archive constructed and destroyed once");
    check(fake::g_unbind == 1 && fake::g_unbindNod == fake::replay, "  replay unbound from the new file (as the game does)");
    check(fake::g_scan == 1, "  replay list rescanned once");
    check(memcmp(replayBefore, fake::replay, sizeof(replayBefore)) == 0, "  the replay object is not changed by the autosave");
    check(fake::readFile(first) == fake::g_lastContent && fake::g_lastContent.find("validable=\"1\"") != std::string::npos,
          "  the file is exactly what the game's save wrote (validable untouched)");
    check(fake::g_live == 0, "  every game string the autosave made is freed again");
    check(wcscmp(legacy::stats().lastPath, first.c_str()) == 0, "  full path from the folder's GetFullPath");

    // 2. slower run: no new best -> not saved
    fake::run(50000);
    check(legacy::stats().saved == 1 && fake::g_archSaves == 1, "slower finish (no new best, Mode=best): not saved");
    // 3. same time as the best: not a new best
    fake::run(46259);
    check(fake::g_archSaves == 1, "equal time: not a new best, not saved");
    // 4. race end without a finish (restart, give up)
    fake::run(0, false, true);
    check(fake::g_archSaves == 1, "race end without a finish: nothing saved");
    // 5. finish of another player (not the local one)
    *reinterpret_cast<void**>(fake::player + legacy_off::kPlayerInfo) = fake::otherInfo;
    fake::run(30000);
    *reinterpret_cast<void**>(fake::player + legacy_off::kPlayerInfo) = fake::info;
    check(fake::g_archSaves == 1, "finish of a player who is not the local one: nothing saved");
    // 6. new best while the target name exists already -> " (2)", never overwritten
    const std::wstring taken = fake::g_dir + L"$o$f00Test Map(00'45''00).Replay.Gbx";
    HANDLE h = CreateFileW(taken.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
    DWORD w = 0;
    WriteFile(h, "MINE", 4, &w, nullptr);
    CloseHandle(h);
    fake::run(45000);
    check(fake::g_archSaves == 2 && fileThere(fake::g_dir + L"$o$f00Test Map(00'45''00) (2).Replay.Gbx") &&
              fake::readFile(taken) == "MINE",
          "new best, name taken: saved as \"... (2).Replay.Gbx\", the existing file untouched");
    // 7. the game did not stop the recording (should not happen): skipped, nothing written
    fake::g_clearRecording = false;
    fake::run(44000);
    fake::g_clearRecording = true;
    check(fake::g_archSaves == 2 && legacy::stats().skipped == 1, "replay still recording at the race end: skipped");
    // 8. stunts map: "(score)"
    *reinterpret_cast<int*>(fake::challenge + legacy_off::kChallengePlayMode) = 5;
    fake::g_stuntScore = 123;
    fake::run(43000);
    *reinterpret_cast<int*>(fake::challenge + legacy_off::kChallengePlayMode) = 0;
    check(fileThere(fake::g_dir + L"$o$f00Test Map(123).Replay.Gbx"), "stunts map: <map>(score).Replay.Gbx, as the game names it");
    check(fake::g_live == 0, "no game string left allocated after all runs");

    // 9. Mode=all on a fresh image: a slower run is saved too
    fake::build(g);
    legacy::Config all;
    all.bestOnly = false;
    all.testKey = 0;
    check(legacy::start(g, base, all, 0), "Mode=all: hooks installed");
    fake::run(50000);
    fake::run(51000);
    check(fake::g_archSaves == 2, "Mode=all: both finishes saved (second one not a best)");
    // 10. ScanReplays=0
    fake::build(g);
    legacy::Config noScan;
    noScan.scanReplays = false;
    noScan.testKey = 0;
    legacy::start(g, base, noScan, 0);
    fake::run(52000);
    check(fake::g_archSaves == 1 && fake::g_scan == 0, "ScanReplays=0: saved, replay list not rescanned");
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    autosave::logOpen();
    testTable();
    std::vector<std::pair<std::wstring, Game>> real;
    if (argc < 2) printf("SKIP  real exes (none given: legacy_test.exe <exe> sunrise|original|eswc ...)\n");
    {
        for (int i = 1; i + 1 < argc; i += 2)
            real.push_back({argv[i], wcscmp(argv[i + 1], L"sunrise") == 0 ? Game::Sunrise
                                     : wcscmp(argv[i + 1], L"original") == 0 ? Game::Original : Game::Eswc});
    }
    for (auto& r : real) testRealExe(r.first.c_str(), r.second);
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring root = exe;
    root.resize(root.find_last_of(L'\\') + 1);
    testFakeGame(Game::Sunrise, root);
    testFakeGame(Game::Original, root);
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
