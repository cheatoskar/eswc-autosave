// Auto "Save previous replay" for TrackMania Nations ESWC (unofficial 1.80 patch, exe 0.1.7.9).
//
// What the game does when the player picks "Save previous replay" in the in-game menu
// (network loop FUN_0073cf10, menu result 0x30): CGameCtnApp::SaveReplay (FUN_0070b2c0) with the
// app's previous-race replay (app+0x25c). That fiber builds the default name
// "<challenge name>(mm'ss''cc)", shows the Save-As dialog, then (on OK) appends ".Replay.Gbx",
// filters the name, and writes the file with CSystemArchiveNod (FUN_0062e1c0) into the
// "My Replays" folder (app+0x210). We run exactly that post-dialog part, without the dialogs.
//
// When: the previous-race replay is (re)filled by FUN_0074ab60 ("stop replay record and keep
// copy"). We remember a finish of the local player in CTrackManiaRace::OnFinishLine
// (FUN_006d9f50) and save the copy made by the next FUN_0074ab60. Everything runs in the game's
// own thread, inside the game's calls.
//
// Solo (campaign, single tracks): there is no previous-race copy. The end-race screen's
// "Save replay" (solo loop FUN_006eb540, menu result 0x12) calls the same SaveReplay with the
// current replay app+0x144. After a solo finish the race class calls FUN_006c5060 (vtable slot
// after OnFinishLine), which stops that recording (vtable+0x8c). We save app+0x144 right after it.
#include "autosave.h"

#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "online.h"
#include "sha256.h"

namespace autosave {
namespace {

// ---------------------------------------------------------------- build ---------------------
constexpr char kExeSha256[] = "983bd12724739dcb0f876aeb3a4259154b068940f5167f57ede750ca452d8068";
constexpr DWORD kExeTimestamp = 0x5ce7272f;
constexpr uintptr_t kPreferredBase = 0x00400000;

// Functions (link addresses, the exe has no relocations/ASLR, rebased anyway).
constexpr uintptr_t kOnFinishLine = 0x006d9f50;   // void CTrackManiaRace::OnFinishLine(CTrackManiaPlayer*)  ret 4
constexpr uintptr_t kKeepCopy = 0x0074ab60;       // void RaceNet::StopReplayRecordAndKeepCopy()  ret
constexpr uintptr_t kSoloRaceEnd = 0x006c5060;    // void CTrackManiaRace1P::<race end>(): stops app+0x144 recording  ret
constexpr uintptr_t kLocalPlayerInfo = 0x0071c730; // CTrackManiaPlayerInfo* CTrackManiaRace::GetLocalPlayerInfo()
constexpr uintptr_t kStrSet = 0x00405f40;         // void CFastStringInt::SetString(const SStringParamInt&)  ret 4
constexpr uintptr_t kStrEmpty = 0x00402530;       // void CFastStringInt::Empty()
constexpr uintptr_t kFilterFileName = 0x00635870; // cdecl (CFastStringInt& dst, const CFastStringInt& src, int, int)
constexpr uintptr_t kFileExists = 0x0063f260;     // cdecl int (const CFastStringInt& name, CSystemFids* folder)
constexpr uintptr_t kBestGhostTime = 0x0068c390;  // void CGameCtnReplayRecord::GetBestGhostRaceTime(int&, int&)  ret 8
constexpr uintptr_t kBestStuntScore = 0x0068c470; // int CGameCtnReplayRecord::GetBestGhostStuntsScore()
constexpr uintptr_t kArchiveCtor = 0x0062a850;    // CSystemArchiveNod::CSystemArchiveNod()
constexpr uintptr_t kArchiveSave = 0x0062e1c0;    // int CSystemArchiveNod::Save(name&, nod, folder, 10, 7, 1, 1)  ret 0x1c
constexpr uintptr_t kArchiveDtor = 0x0062a950;    // CSystemArchiveNod::~CSystemArchiveNod()
constexpr uintptr_t kUnbindFid = 0x00623f90;      // void CSystemEngine::UnbindFid(CMwNod*)  ret 4
constexpr uintptr_t kScanReplays = 0x007173b0;    // void CTrackMania::ScanDiskForReplays()
// Globals
constexpr uintptr_t kEmptyWideString = 0x00bb45d4; // wchar_t* (text of every empty CFastStringInt)
constexpr uintptr_t kMainEngine = 0x00c96140;      // CMwEngineMain* ; ->+0x28 ->+0x2c = CSystemEngine*

// Offsets
constexpr size_t kRaceApp = 0x40;            // CTrackManiaRace -> CTrackMania (app)
constexpr size_t kPlayerInfo = 0x34;         // CTrackManiaPlayer -> CTrackManiaPlayerInfo
constexpr size_t kInfoRaceState = 0x25c;     // 1 = racing, 2 = finished
constexpr size_t kInfoTime = 0x1f4;          // last race time (ms)
constexpr size_t kInfoBest = 0x1f8;          // best race time (ms, 0xffffffff = none)
constexpr size_t kAppPrevReplay = 0x25c;     // CGameCtnReplayRecord* previous race (menu "Save previous replay")
constexpr size_t kAppCurReplay = 0x144;      // CGameCtnReplayRecord* current race (solo "Save replay")
constexpr size_t kAppReplaysFolder = 0x210;  // CSystemFids* "My Replays"
constexpr size_t kReplayFid = 0x08;          // CSystemFidFile* while bound
constexpr size_t kReplayFidBufCount = 0x30;  // count of a fid buffer (1 = replay came from a file)
constexpr size_t kReplayFidBufData = 0x34;   // CSystemFidFile** of that buffer
constexpr size_t kFidParentFolder = 0x18;    // CSystemFidFile -> CSystemFids* (folder)
constexpr size_t kReplayChallenge = 0x60;    // CGameCtnChallenge*
constexpr size_t kReplayRecording = 0xb4;    // != 0 while recording
constexpr size_t kReplaySavedFid = 0xdc;     // fid of the last save (0 = never saved)
constexpr size_t kChallengeType = 0x118;     // 5 = stunts
constexpr size_t kChallengeName = 0x124;     // CFastStringInt {len, ptr}

struct FastStringInt {
    uint32_t len;
    wchar_t* ptr;
};
struct StringParamInt {
    const wchar_t* ptr;
    uint32_t len;
    uint32_t pad; // unused by this build, zero
};

// thiscall functions are called as fastcall with a dummy edx.
using FnLocalInfo = void*(__fastcall*)(void* race, void* edx);
using FnStrSet = void(__fastcall*)(FastStringInt* self, void* edx, const StringParamInt* src);
using FnStrEmpty = void(__fastcall*)(FastStringInt* self, void* edx);
using FnFilter = void(__cdecl*)(FastStringInt* dst, const FastStringInt* src, int, int);
using FnExists = int(__cdecl*)(const FastStringInt* name, void* folder);
using FnBestTime = void(__fastcall*)(void* replay, void* edx, int* time, int* other);
using FnBestScore = int(__fastcall*)(void* replay, void* edx);
using FnArchCtor = void(__fastcall*)(void* archive, void* edx);
using FnArchSave = int(__fastcall*)(void* archive, void* edx, FastStringInt* name, void* nod, void* folder, int version,
                                    int flags, int a, int b);
using FnArchDtor = void(__fastcall*)(void* archive, void* edx);
using FnUnbind = void(__fastcall*)(void* systemEngine, void* edx, void* nod);
using FnScan = void(__fastcall*)(void* app, void* edx);
using FnVoidMethod = void(__fastcall*)(void* self, void* edx);
using FnGetFullPath = void(__fastcall*)(void* fids, void* edx, FastStringInt* out);

uintptr_t g_base = kPreferredBase;
uintptr_t rebase(uintptr_t link) { return g_base - kPreferredBase + link; }
template <typename T> T fn(uintptr_t link) { return reinterpret_cast<T>(rebase(link)); }
template <typename T> T field(const void* obj, size_t off) {
    return *reinterpret_cast<const T*>(static_cast<const uint8_t*>(obj) + off);
}

// ---------------------------------------------------------------- config --------------------
struct Config {
    bool enabled = true;
    bool bestOnly = false;
    UINT testKey = VK_F9; // 0 = off
    bool scanReplays = true;
    bool onlineFix = true;
} g_cfg;

void loadConfig() {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* slash = (n && n < MAX_PATH) ? wcsrchr(path, L'\\') : nullptr;
    if (!slash) return;
    wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"eswc_autosave.ini");
    const bool exists = GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
    g_cfg.enabled = GetPrivateProfileIntW(L"autosave", L"Enabled", 1, path) != 0;
    wchar_t mode[32] = L"";
    GetPrivateProfileStringW(L"autosave", L"Mode", L"best", mode, 32, path);
    g_cfg.bestOnly = _wcsicmp(mode, L"best") == 0;
    wchar_t key[32] = L"";
    GetPrivateProfileStringW(L"autosave", L"TestKey", L"0x78", key, 32, path);
    g_cfg.testKey = static_cast<UINT>(wcstoul(key, nullptr, 0)) & 0xff;
    g_cfg.scanReplays = GetPrivateProfileIntW(L"autosave", L"ScanReplays", 1, path) != 0;
    g_cfg.onlineFix = GetPrivateProfileIntW(L"online", L"OnlineFix", 1, path) != 0;
    AS_LOG("config: %s, Enabled=%d Mode=%s TestKey=0x%02X ScanReplays=%d OnlineFix=%d",
           exists ? "eswc_autosave.ini" : "no ini, defaults", g_cfg.enabled, g_cfg.bestOnly ? "best" : "all", g_cfg.testKey,
           g_cfg.scanReplays, g_cfg.onlineFix);
}

// ---------------------------------------------------------------- build check ---------------
bool checkBuild() {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(g_base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(g_base + dos->e_lfanew);
    const DWORD stamp = nt->FileHeader.TimeDateStamp;
    char hash[65] = "";
    if (!sha256File(path, hash)) {
        AS_LOG("build: could not hash the exe (error %lu), autosave off", GetLastError());
        return false;
    }
    char narrow[MAX_PATH * 2];
    WideCharToMultiByte(CP_UTF8, 0, path, -1, narrow, sizeof(narrow), nullptr, nullptr);
    if (strcmp(hash, kExeSha256) != 0 || stamp != kExeTimestamp) {
        AS_LOG("build: UNKNOWN exe %s (sha256 %s, timestamp %08lX), autosave off", narrow, hash, stamp);
        return false;
    }
    AS_LOG("build: TmNationsESWC 0.1.7.9 (unofficial 1.80 patch) recognised, sha256 ok, base %08X", (unsigned)g_base);
    return true;
}

// ---------------------------------------------------------------- inline hooks --------------
struct InlineHook {
    uintptr_t link;
    unsigned char prologue[8];
    size_t length; // whole instructions, >= 5, no relative operands
    const char* name;
    void* trampoline;
};

bool installHook(InlineHook& h, void* detour) {
    BYTE* target = reinterpret_cast<BYTE*>(rebase(h.link));
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

InlineHook g_finishHook = {kOnFinishLine, {0x8b, 0x44, 0x24, 0x04, 0x83, 0xec, 0x1c}, 7, "CTrackManiaRace::OnFinishLine", nullptr};
InlineHook g_keepCopyHook = {kKeepCopy, {0x51, 0x56, 0x8b, 0xf1, 0x8b, 0x46, 0x40}, 7, "StopReplayRecordAndKeepCopy", nullptr};
InlineHook g_soloEndHook = {kSoloRaceEnd, {0x83, 0xec, 0x08, 0x53, 0x55, 0x56, 0x57}, 7, "CTrackManiaRace1P race end", nullptr};

// ---------------------------------------------------------------- state ---------------------
// All of it is touched only from the game thread that runs the race (hooks) and the window
// thread (test key); in this game both are the main thread (checked and logged).
void* g_app = nullptr;
DWORD g_mainThread = 0;
int g_finishDepth = 0;      // inside OnFinishLine
unsigned g_copyCounter = 0; // KeepCopy calls that produced a new copy
void* g_lastCopy = nullptr;
unsigned g_soloEndCounter = 0; // solo race-end calls
void* g_lastSoloReplay = nullptr;
bool g_armed = false;       // a local finish is waiting for its copy
void* g_armedRace = nullptr; // ... made by this race object
unsigned g_armedTime = 0;
bool g_saving = false;
bool g_warnedThread = false;

void noteThread(const char* where) {
    if (!g_warnedThread && GetCurrentThreadId() != g_mainThread) {
        g_warnedThread = true;
        AS_LOG("note: %s runs in thread %lu, DirectInput8Create ran in thread %lu", where, GetCurrentThreadId(), g_mainThread);
    }
}

// ---------------------------------------------------------------- saving --------------------
void toUtf8(const wchar_t* w, char* out, int size) {
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, out, size, nullptr, nullptr)) out[0] = 0;
}

struct SaveResult {
    int ok;           // 1 saved, 0 failed, -1 skipped
    const char* why;  // for skipped/failed
    wchar_t name[300];
    wchar_t folder[300];
    int time;
    bool fromFile;
};

void initString(FastStringInt& s) {
    s.len = 0;
    s.ptr = *reinterpret_cast<wchar_t**>(rebase(kEmptyWideString));
}

void setString(FastStringInt& s, const wchar_t* text) {
    StringParamInt p = {text, static_cast<uint32_t>(wcslen(text)), 0};
    fn<FnStrSet>(kStrSet)(&s, nullptr, &p);
}

void copyOut(const FastStringInt& s, wchar_t* out, size_t cap) {
    size_t n = s.len < cap - 1 ? s.len : cap - 1;
    if (s.ptr && n) memcpy(out, s.ptr, n * sizeof(wchar_t));
    out[n] = 0;
}

// The part of FUN_0070b2c0 after the Save-As dialog, with the dialog's default answer.
// No C++ objects in here (SEH).
// solo: the game's current replay (app+0x144), which the game keeps using; we leave its
// "saved as" field (+0xdc) alone there so the game's own "Save replay" behaves as before.
void saveReplaySeh(void* app, void* replay, bool solo, SaveResult* r) {
    __try {
        r->ok = -1;
        if (!replay) { r->why = solo ? "no current replay" : "no previous replay"; return; }
        if (field<int>(replay, kReplayRecording) != 0 && solo) { r->why = "replay is still recording"; return; }
        if (!solo && field<void*>(replay, kReplaySavedFid)) { r->why = "already saved"; return; }
        void* challenge = field<void*>(replay, kReplayChallenge);
        if (!challenge) { r->why = "replay has no challenge"; return; }
        // Folder as SaveReplay picks it: the folder of the file the replay came from, else "My Replays".
        void* folder = field<void*>(app, kAppReplaysFolder);
        r->fromFile = false;
        if (field<uint32_t>(replay, kReplayFidBufCount) == 1) {
            void** files = field<void**>(replay, kReplayFidBufData);
            void* fidFile = files ? files[0] : nullptr;
            void* parent = fidFile ? field<void*>(fidFile, kFidParentFolder) : nullptr;
            if (parent) {
                folder = parent;
                r->fromFile = true;
            }
        }
        if (!folder) { r->why = "no replays folder"; return; }

        // Default name exactly as the game builds it: challenge name + "(mm'ss''cc)" (stunts: "(score)").
        wchar_t base[300];
        const FastStringInt name = field<FastStringInt>(challenge, kChallengeName);
        copyOut(name, base, 200);
        wchar_t suffix[40];
        if (field<int>(challenge, kChallengeType) == 5) {
            const int score = fn<FnBestScore>(kBestStuntScore)(replay, nullptr);
            swprintf_s(suffix, L"(%d)", score);
            r->time = score;
        } else {
            int time = -1, other = 0;
            fn<FnBestTime>(kBestGhostTime)(replay, nullptr, &time, &other);
            r->time = time;
            int t = time < 0 ? -time : time;
            const int mm = t / 60000;
            const unsigned ss = static_cast<unsigned>(t - mm * 60000) / 1000;
            const unsigned cc = static_cast<unsigned>(t - static_cast<int>(ss) * 1000 - mm * 60000) / 10;
            swprintf_s(suffix, L"(%.2d'%.2d''%.2d)", mm, ss, cc);
        }

        FastStringInt raw, filtered;
        initString(raw);
        initString(filtered);
        const FnExists exists = fn<FnExists>(kFileExists);
        bool found = false;
        for (int attempt = 1; attempt <= 20 && !found; attempt++) {
            wchar_t full[360];
            if (attempt == 1) swprintf_s(full, L"%s%s.Replay.Gbx", base, suffix);
            else swprintf_s(full, L"%s%s (%d).Replay.Gbx", base, suffix, attempt);
            setString(raw, full);
            fn<FnFilter>(kFilterFileName)(&filtered, &raw, 0, 0);
            if (!exists(&filtered, folder)) found = true; // the game would ask "Overwrite?"; we never overwrite
        }
        copyOut(filtered, r->name, 300);
        if (!found) {
            fn<FnStrEmpty>(kStrEmpty)(&raw, nullptr);
            fn<FnStrEmpty>(kStrEmpty)(&filtered, nullptr);
            r->why = "file exists (20 names tried)";
            return;
        }

        if (field<int>(replay, kReplayRecording) != 0) {
            void** vt = field<void**>(replay, 0);
            reinterpret_cast<FnVoidMethod>(vt[0xa0 / 4])(replay, nullptr);
        }

        alignas(16) static unsigned char archive[0x400];
        memset(archive, 0, sizeof(archive));
        fn<FnArchCtor>(kArchiveCtor)(archive, nullptr);
        const int saved = fn<FnArchSave>(kArchiveSave)(archive, nullptr, &filtered, replay, folder, 10, 7, 1, 1);
        fn<FnArchDtor>(kArchiveDtor)(archive, nullptr);

        if (saved && !solo) {
            *reinterpret_cast<void**>(static_cast<uint8_t*>(replay) + kReplaySavedFid) = field<void*>(replay, kReplayFid);
        }
        void* engineMain = *reinterpret_cast<void**>(rebase(kMainEngine));
        void* systemEngine = field<void*>(field<void*>(engineMain, 0x28), 0x2c);
        fn<FnUnbind>(kUnbindFid)(systemEngine, nullptr, replay);

        // Folder path, for the log only.
        FastStringInt path;
        initString(path);
        void** fvt = field<void**>(folder, 0);
        reinterpret_cast<FnGetFullPath>(fvt[0xac / 4])(folder, nullptr, &path);
        copyOut(path, r->folder, 300);
        fn<FnStrEmpty>(kStrEmpty)(&path, nullptr);
        fn<FnStrEmpty>(kStrEmpty)(&raw, nullptr);
        fn<FnStrEmpty>(kStrEmpty)(&filtered, nullptr);

        if (saved && g_cfg.scanReplays) fn<FnScan>(kScanReplays)(app, nullptr); // as the game does after a save
        r->ok = saved ? 1 : 0;
        r->why = saved ? "" : "CSystemArchiveNod save returned 0";
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        r->ok = 0;
        r->why = "exception inside the game's save code";
    }
}

// Reads the header the game wrote (the XML chunk) and reports what matters for validation.
void checkHeader(const wchar_t* path, char* out, size_t cap) {
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

void saveNow(void* replay, bool solo, const char* reason) {
    const char* mode = solo ? "Solo" : "LAN";
    if (g_saving) return;
    if (!g_app) {
        AS_LOG("[%s] save (%s): no race seen yet, nothing to save", mode, reason);
        return;
    }
    g_saving = true;
    SaveResult r = {};
    const DWORD t0 = GetTickCount();
    saveReplaySeh(g_app, replay, solo, &r);
    const DWORD ms = GetTickCount() - t0;
    g_saving = false;
    char name[600], folder[600];
    toUtf8(r.name, name, sizeof(name));
    toUtf8(r.folder, folder, sizeof(folder));
    if (r.ok == 1) {
        wchar_t full[620];
        swprintf_s(full, L"%s%s", r.folder, r.name);
        WIN32_FILE_ATTRIBUTE_DATA fa = {};
        const BOOL have = GetFileAttributesExW(full, GetFileExInfoStandard, &fa);
        AS_LOG("[%s] SAVED (%s): %s%s  time=%d  size=%lu bytes  took %lu ms%s", mode, reason, folder, name, r.time,
               have ? fa.nFileSizeLow : 0ul, ms, r.fromFile ? "  (folder of the replay's source file, as the game does)" : "");
        char hdr[200];
        checkHeader(full, hdr, sizeof(hdr));
        AS_LOG("[%s] %s", mode, hdr);
    } else if (r.ok == 0) {
        AS_LOG("[%s] ERROR (%s): %s  [%s]", mode, reason, r.why, name);
    } else {
        AS_LOG("[%s] skipped (%s): %s", mode, reason, r.why);
    }
}

// ---------------------------------------------------------------- hooks ---------------------
using FnFinish = void(__fastcall*)(void* race, void* edx, void* player);
using FnRaceVoid = void(__fastcall*)(void* race, void* edx);

void __fastcall finishDetour(void* race, void* edx, void* player) {
    void* info = nullptr;
    int stateBefore = 0;
    unsigned bestBefore = 0xffffffffu;
    const unsigned copiesBefore = g_copyCounter;
    const unsigned soloBefore = g_soloEndCounter;
    __try {
        if (player) {
            info = field<void*>(player, kPlayerInfo);
            if (info) {
                stateBefore = field<int>(info, kInfoRaceState);
                bestBefore = field<unsigned>(info, kInfoBest);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        info = nullptr;
    }
    g_finishDepth++;
    reinterpret_cast<FnFinish>(g_finishHook.trampoline)(race, edx, player);
    g_finishDepth--;
    if (!info || stateBefore != 1) return;
    __try {
        if (field<int>(info, kInfoRaceState) != 2) return; // a lap or a rejected crossing
        void* local = fn<FnLocalInfo>(kLocalPlayerInfo)(race, nullptr);
        if (local != info) return;
        g_app = field<void*>(race, kRaceApp);
        noteThread("OnFinishLine");
        const unsigned time = field<unsigned>(info, kInfoTime);
        const unsigned best = field<unsigned>(info, kInfoBest);
        const bool newBest = best != bestBefore || time < bestBefore;
        AS_LOG("finish: local player, time=%u best=%d (best before %d)%s", time, (int)best, (int)bestBefore,
               newBest ? "  NEW BEST" : "");
        if (g_cfg.bestOnly && !newBest) {
            AS_LOG("finish: Mode=best and no new best, not saving");
            g_armed = false;
            return;
        }
        if (g_copyCounter != copiesBefore) {
            saveNow(g_lastCopy, false, "finish"); // the game made the copy during the finish already
        } else if (g_soloEndCounter != soloBefore) {
            saveNow(g_lastSoloReplay, true, "finish");
        } else {
            g_armed = true;
            g_armedRace = race;
            g_armedTime = time;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        AS_LOG("ERROR: exception while reading the finish state");
    }
}

void __fastcall keepCopyDetour(void* race, void* edx) {
    void* before = nullptr;
    void* app = nullptr;
    __try {
        app = field<void*>(race, kRaceApp);
        before = app ? field<void*>(app, kAppPrevReplay) : nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        app = nullptr;
    }
    reinterpret_cast<FnRaceVoid>(g_keepCopyHook.trampoline)(race, edx);
    if (!app) return;
    g_app = app;
    void* after = nullptr;
    __try {
        after = field<void*>(app, kAppPrevReplay);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    if (after == before || !after) {
        // Was not recording: no new copy. A pending finish must not wait for a later run's copy.
        if (g_armed && g_finishDepth == 0 && g_armedRace == race) {
            g_armed = false;
            AS_LOG("[LAN] skipped (finish): the game made no copy of the finished race");
        }
        return;
    }
    g_copyCounter++;
    g_lastCopy = after;
    noteThread("KeepCopy");
    if (g_armed && g_finishDepth == 0 && g_armedRace == race) {
        g_armed = false;
        AS_LOG("[LAN] previous replay updated after the finish (time=%u), saving", g_armedTime);
        saveNow(after, false, "finish");
    }
}

void __fastcall soloEndDetour(void* race, void* edx) {
    reinterpret_cast<FnRaceVoid>(g_soloEndHook.trampoline)(race, edx);
    void* app = nullptr;
    void* replay = nullptr;
    __try {
        app = field<void*>(race, kRaceApp);
        replay = app ? field<void*>(app, kAppCurReplay) : nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    if (!app) return;
    g_app = app;
    g_soloEndCounter++;
    g_lastSoloReplay = replay;
    noteThread("Solo race end");
    if (g_armed && g_finishDepth == 0 && g_armedRace == race) {
        g_armed = false;
        AS_LOG("[Solo] race end after the finish (time=%u), saving the race replay", g_armedTime);
        saveNow(replay, true, "finish");
    }
}

// ---------------------------------------------------------------- test key ------------------
HHOOK g_msgHook = nullptr;

LRESULT CALLBACK getMsgProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && wParam == PM_REMOVE && g_cfg.testKey) {
        const MSG* m = reinterpret_cast<const MSG*>(lParam);
        if ((m->message == WM_KEYDOWN || m->message == WM_SYSKEYDOWN) && m->wParam == g_cfg.testKey &&
            !(m->lParam & (1 << 30))) {
            void* replay = nullptr;
            bool solo = false;
            if (g_app) {
                __try {
                    replay = field<void*>(g_app, kAppPrevReplay);
                    if (!replay) { // no previous-race copy (solo): the current replay, once it no longer records
                        replay = field<void*>(g_app, kAppCurReplay);
                        solo = true;
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    replay = nullptr;
                }
            }
            saveNow(replay, solo, "test key");
        }
    }
    return CallNextHookEx(g_msgHook, code, wParam, lParam);
}

bool g_done = false;
} // namespace

void initOnce() {
    if (g_done) return;
    g_done = true;
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    g_mainThread = GetCurrentThreadId();
    AS_LOG("---- eswc_autosave start (thread %lu) ----", g_mainThread);
    if (!checkBuild()) return;
    loadConfig();
    if (g_cfg.onlineFix) onlineFixInstall(g_base);
    else AS_LOG("online: OnlineFix=0, nothing changed");
    if (!g_cfg.enabled) {
        AS_LOG("autosave disabled in the ini, no hooks");
        return;
    }
    const bool a = installHook(g_finishHook, reinterpret_cast<void*>(&finishDetour));
    if (!a) {
        AS_LOG("finish hook missing, autosave inactive");
        return;
    }
    const bool b = installHook(g_keepCopyHook, reinterpret_cast<void*>(&keepCopyDetour));
    const bool c = installHook(g_soloEndHook, reinterpret_cast<void*>(&soloEndDetour));
    if (g_cfg.testKey) {
        g_msgHook = SetWindowsHookExW(WH_GETMESSAGE, getMsgProc, nullptr, g_mainThread);
        AS_LOG(g_msgHook ? "test key 0x%02X active (saves the previous/last replay now)" : "test key 0x%02X: hook failed",
               g_cfg.testKey);
    }
    AS_LOG("ready: saving after every %s of the local player (LAN %s, Solo %s)",
           g_cfg.bestOnly ? "new best finish" : "finish", b ? "on" : "OFF", c ? "on" : "OFF");
}

} // namespace autosave
