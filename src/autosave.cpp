// Auto "Save previous replay" for TrackMania Nations ESWC (unofficial 1.80 patch, exe 0.1.7.9).
// TrackMania Sunrise and TrackMania Original (solo only) live in legacy.cpp; initOnce picks by exe.
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
//
// TMX copy (TmxCopy=1): after a LAN/online save, a worker thread re-reads the file the game wrote
// and, if it holds more than one ghost, writes "<name> TMX.Replay.Gbx" with only the local
// player's ghost (see tmxcopy.cpp). The game thread only starts the thread.
#include "autosave.h"

#include <windows.h>
#include <errno.h>
#include <process.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "games.h"
#include "header.h"
#include "hook.h"
#include "legacy.h"
#include "essentials.h"
#include "log.h"
#include "settings.h"
#include "sha256.h"
#include "tmxcopy.h"

namespace autosave {
namespace {

// ---------------------------------------------------------------- build ---------------------
// The exe is identified in games.h (ESWC, Sunrise, Original: size, PE timestamp, SHA-256).
// The login fix (HttpOpenRequestA import slots) and everything else besides the autosave: essentials.cpp.
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
// The autosave part of the ini (settings.cpp reads it). enabled and bestOnly change from the menu.
struct Config {
    bool enabled = true;
    bool bestOnly = false;
    UINT testKey = VK_F9; // 0 = off
    bool scanReplays = true;
    bool tmxCopy = true;
} g_cfg;

const ExeId* g_exe = nullptr;
bool g_hooksTried = false;
bool g_hooksOk = false;

void loadConfig(Settings* s) {
    wchar_t path[MAX_PATH];
    iniPath(path, MAX_PATH);
    bool exists = false;
    loadSettings(path, s, &exists);
    g_cfg.enabled = s->autosave;
    g_cfg.bestOnly = s->bestOnly;
    g_cfg.testKey = s->testKey;
    g_cfg.scanReplays = s->scanReplays;
    g_cfg.tmxCopy = s->tmxCopy;
    wchar_t key[48];
    formatHotkey(s->menuKey, key, 48);
    AS_LOG("config: %s, Enabled=%d Mode=%s TestKey=0x%02X ScanReplays=%d TmxCopy=%d OnlineFix=%d ControllerFix=%d MenuKey=%ls "
           "Music=%d Sounds=%d WindowMode=%s",
           exists ? "eswc_autosave.ini" : "no ini, defaults", g_cfg.enabled, g_cfg.bestOnly ? "best" : "all", g_cfg.testKey,
           g_cfg.scanReplays, g_cfg.tmxCopy, s->onlineFix, s->controllerFix, key, s->music, s->sounds,
           s->windowMode == 1 ? "fullscreen" : s->windowMode == 0 ? "windowed" : "game");
}

// ---------------------------------------------------------------- build check ---------------
// Size, PE timestamp and SHA-256 of the running exe against games.h. nullptr = unknown, mod off.
const ExeId* identifyBuild() {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return nullptr;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(g_base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(g_base + dos->e_lfanew);
    const DWORD stamp = nt->FileHeader.TimeDateStamp;
    WIN32_FILE_ATTRIBUTE_DATA fa = {};
    const uint64_t size = GetFileAttributesExW(path, GetFileExInfoStandard, &fa)
                              ? (static_cast<uint64_t>(fa.nFileSizeHigh) << 32) | fa.nFileSizeLow
                              : 0;
    char hash[65] = "";
    if (!sha256File(path, hash)) {
        AS_LOG("build: could not hash the exe (error %lu), autosave off", GetLastError());
        return nullptr;
    }
    char narrow[MAX_PATH * 2];
    WideCharToMultiByte(CP_UTF8, 0, path, -1, narrow, sizeof(narrow), nullptr, nullptr);
    const ExeId* id = identifyExe(hash, stamp, size);
    if (!id) {
        AS_LOG("build: UNKNOWN exe %s (sha256 %s, timestamp %08lX, size %llu), autosave off", narrow, hash, stamp,
               static_cast<unsigned long long>(size));
        return nullptr;
    }
    AS_LOG("build: %s recognised (size, timestamp, sha256 ok), base %08X", id->name, (unsigned)g_base);
    return id;
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
        wchar_t stem[260];
        swprintf_s(stem, L"%s%s", base, suffix);
        for (int attempt = 1; attempt <= 20 && !found; attempt++) {
            wchar_t full[360];
            // "<stem>.Replay.Gbx" / "<stem> (n).Replay.Gbx"
            if (attempt <= 1) swprintf_s(full, L"%s.Replay.Gbx", stem);
            else swprintf_s(full, L"%s (%d).Replay.Gbx", stem, attempt);
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

// ---------------------------------------------------------------- TMX copy ------------------
// Runs in its own thread after the game's save has returned, so the game never waits for it.
// It only reads the saved file and writes a new one next to it; no game memory is touched.
struct TmxJob {
    wchar_t path[620];
    int localTime; // local player's finish time that triggered the save, -1 = unknown (test key)
};

unsigned __stdcall tmxThread(void* arg) {
    TmxJob* job = static_cast<TmxJob*>(arg);
    tmxcopy::Report r;
    tmxcopy::makeCopy(job->path, nullptr, nullptr, job->localTime, &r);
    char path[2100];
    switch (r.status) {
    case tmxcopy::Status::Converted:
        toUtf8(r.outPath, path, sizeof(path));
        AS_LOG("[TMX] SAVED: %s ghosts %u->1 time=%d validable=%d  (EventsDuration=%u, kept ghost #%d, numNodes %u->%u, "
               "took %.1f ms off the game thread)",
               path, r.ghostsBefore, r.time, r.validable, r.eventsDuration, r.keptIndex, r.nodesBefore, r.nodesAfter,
               r.ms);
        break;
    case tmxcopy::Status::NotNeeded:
        AS_LOG("[TMX] not needed: %s", r.reason);
        break;
    case tmxcopy::Status::Refused:
        AS_LOG("[TMX] skipped: %s", r.reason);
        break;
    default:
        AS_LOG("[TMX] ERROR: %s", r.reason);
        break;
    }
    free(job);
    return 0;
}

void startTmxCopy(const wchar_t* path, int localTime) {
    TmxJob* job = static_cast<TmxJob*>(malloc(sizeof(TmxJob)));
    if (!job) return;
    if (wcslen(path) >= _countof(job->path)) {
        AS_LOG("[TMX] skipped: path too long");
        free(job);
        return;
    }
    wcscpy_s(job->path, path);
    job->localTime = localTime;
    const uintptr_t h = _beginthreadex(nullptr, 256 * 1024, tmxThread, job, 0, nullptr);
    if (!h) {
        AS_LOG("[TMX] ERROR: could not start the worker thread (errno %d)", errno);
        free(job);
        return;
    }
    SetThreadPriority(reinterpret_cast<HANDLE>(h), THREAD_PRIORITY_BELOW_NORMAL);
    CloseHandle(reinterpret_cast<HANDLE>(h));
}

// localTime: the local player's finish time that led to this save (-1 = unknown); the TMX copy
// keeps only a ghost with exactly this time.
void saveNow(void* replay, bool solo, const char* reason, int localTime) {
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
        checkReplayHeader(full, hdr, sizeof(hdr));
        AS_LOG("[%s] %s", mode, hdr);
        essentials::noteSaved(full);
        if (!solo && g_cfg.tmxCopy) startTmxCopy(full, localTime); // network race: maybe several ghosts
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
    if (!info || stateBefore != 1 || !g_cfg.enabled) return; // switched off in the menu: the game's code only
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
            saveNow(g_lastCopy, false, "finish", static_cast<int>(time)); // the game made the copy during the finish already
        } else if (g_soloEndCounter != soloBefore) {
            saveNow(g_lastSoloReplay, true, "finish", static_cast<int>(time));
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
        saveNow(after, false, "finish", static_cast<int>(g_armedTime));
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
        saveNow(replay, true, "finish", static_cast<int>(g_armedTime));
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
            saveNow(replay, solo, "test key", -1);
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
    AS_LOG("---- Legacy Essentials %s start (eswc_autosave.dll, thread %lu) ----", kProductVersion, g_mainThread);
    const ExeId* exe = identifyBuild();
    if (!exe) return;
    g_exe = exe;
    Settings settings;
    loadConfig(&settings);
    // login fix, controller fix, menu, Horizon Client check
    essentials::start(*exe, settings, g_base, g_mainThread);
    if (!g_cfg.enabled) {
        AS_LOG("autosave: Enabled=0, no hooks (the menu can switch it on)");
        return;
    }
    autosaveInstall();
}

bool autosaveInstall() {
    if (g_hooksTried) return g_hooksOk;
    if (!g_exe) return false;
    g_hooksTried = true;
    if (g_exe->game != Game::Eswc) {
        // Sunrise / Original: solo autosave only (legacy.cpp).
        legacy::Config lc;
        lc.bestOnly = g_cfg.bestOnly;
        lc.scanReplays = g_cfg.scanReplays;
        lc.testKey = g_cfg.testKey;
        lc.enabled = g_cfg.enabled;
        lc.onSaved = &essentials::noteSaved;
        g_hooksOk = legacy::start(*legacyGame(g_exe->game), g_base, lc, g_mainThread);
        return g_hooksOk;
    }
    const bool a = installHook(g_finishHook, reinterpret_cast<void*>(&finishDetour), g_base);
    if (!a) {
        AS_LOG("finish hook missing, autosave inactive");
        return false;
    }
    const bool b = installHook(g_keepCopyHook, reinterpret_cast<void*>(&keepCopyDetour), g_base);
    const bool c = installHook(g_soloEndHook, reinterpret_cast<void*>(&soloEndDetour), g_base);
    if (g_cfg.testKey) {
        g_msgHook = SetWindowsHookExW(WH_GETMESSAGE, getMsgProc, nullptr, g_mainThread);
        AS_LOG(g_msgHook ? "test key 0x%02X active (saves the previous/last replay now)" : "test key 0x%02X: hook failed",
               g_cfg.testKey);
    }
    AS_LOG("ready: saving after every %s of the local player (LAN %s, Solo %s)",
           g_cfg.bestOnly ? "new best finish" : "finish", b ? "on" : "OFF", c ? "on" : "OFF");
    g_hooksOk = true;
    return true;
}

bool autosaveInstalled() { return g_hooksOk; }

void autosaveSetEnabled(bool on) {
    g_cfg.enabled = on;
    g_armed = false; // a finish waiting for its copy is dropped
    legacy::setEnabled(on);
    AS_LOG("autosave: %s", on ? "on" : "off (finishes are not saved)");
}

void autosaveSetBestOnly(bool on) {
    g_cfg.bestOnly = on;
    legacy::setBestOnly(on);
    AS_LOG("autosave: Mode=%s", on ? "best" : "all");
}

} // namespace autosave
