// Solo autosave for TrackMania Sunrise (Win10 community repack) and TrackMania Original (Online
// Patch v2 exe). Both run the Sunrise engine; the code and the struct members used here are the same
// in both exes, only the addresses differ (games.h).
//
// What the game does on "Save replay" on the solo end-race screen: CTrackMania::SaveReplayRecord
// (Sunrise 0x008177f0, Original 0x00815c50; the fiber that uses the reflection name
// "SaveReplayRecord") builds the default name "<map name>(mm'ss''cc)" (stunts: "(score)"), shows the
// Save-As dialog, appends ".Replay.Gbx", filters the name, and writes the replay record with
// CSystemArchiveNod::Save(name, replay, folder, 10, 7, 1, 1) into "My Replays" (app+0x1d8). Then it
// unbinds the replay from the new file and rescans the replay list. We run exactly those calls, with
// the dialog's default answer, and never overwrite a file (" (2)" ... instead).
//
// When: after a finish of the local player (CTrackManiaRace::OnFinishLine, RaceState 1 -> 2), the
// solo race class calls its race end (CTrackManiaRace1P vtable slot 88), which stops the recording of
// app+0x130 (ReplayRecord). We save that replay right after it. The race end exists only in the
// solo race vtables (1P, 1PGhosts, 1PSpecial), so network races are never saved.
//
// Nothing in the game is written except the two 5-byte hook jumps; the replay's content and its
// "validable" flag are whatever the game's own save code writes.
//
// Every address was found by comparing with ESWC's verified code (same call order and arguments in
// SaveReplayRecord, same function bodies) and checked a second way (reflection member offsets,
// strings, vtable slots, call sites): trackmania-internals games/tm-sunrise/replay-save.md.
#include "legacy.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "header.h"
#include "hook.h"
#include "log.h"

namespace autosave {
namespace legacy {
namespace {

namespace off = legacy_off;

struct FastStringInt {
    uint32_t len;
    wchar_t* ptr;
};
struct StringParamInt {
    const wchar_t* ptr;
    uint32_t len;
    uint32_t pad;
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
using FnGetFullPath = void(__fastcall*)(void* fids, void* edx, FastStringInt* out);
using FnFinish = void(__fastcall*)(void* race, void* edx, void* player);
using FnRaceVoid = void(__fastcall*)(void* race, void* edx);

const LegacyGame* g_game = nullptr;
uintptr_t g_base = 0x00400000;
Config g_cfg;
Stats g_stats;

uintptr_t rebase(uintptr_t link) { return g_base - 0x00400000 + link; }
template <typename T> T fn(uintptr_t link) { return reinterpret_cast<T>(rebase(link)); }
template <typename T> T field(const void* obj, size_t o) {
    return *reinterpret_cast<const T*>(static_cast<const uint8_t*>(obj) + o);
}

InlineHook g_finishHook;
InlineHook g_soloEndHook;

// State, touched only on the game's main thread (hooks, test key).
void* g_app = nullptr;
int g_finishDepth = 0;
unsigned g_soloEndCounter = 0;
void* g_lastSoloReplay = nullptr;
bool g_armed = false;
void* g_armedRace = nullptr;
unsigned g_armedTime = 0;
bool g_saving = false;

void toUtf8(const wchar_t* w, char* out, int size) {
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, out, size, nullptr, nullptr)) out[0] = 0;
}

struct SaveResult {
    int ok; // 1 saved, 0 failed, -1 skipped
    const char* why;
    wchar_t name[300];
    wchar_t folder[300];
    int time;
};

void initString(FastStringInt& s) {
    s.len = 0;
    s.ptr = *reinterpret_cast<wchar_t**>(rebase(g_game->emptyWideString));
}

void setString(FastStringInt& s, const wchar_t* text) {
    StringParamInt p = {text, static_cast<uint32_t>(wcslen(text)), 0};
    fn<FnStrSet>(g_game->strSet)(&s, nullptr, &p);
}

void emptyString(FastStringInt& s) { fn<FnStrEmpty>(g_game->strEmpty)(&s, nullptr); }

void copyOut(const FastStringInt& s, wchar_t* out, size_t cap) {
    size_t n = s.len < cap - 1 ? s.len : cap - 1;
    if (s.ptr && n) memcpy(out, s.ptr, n * sizeof(wchar_t));
    out[n] = 0;
}

// The calls of SaveReplayRecord after its Save-As dialog, with the dialog's default name.
// The replay must be the game's finished solo replay (no longer recording). No C++ objects (SEH).
void saveSeh(void* app, void* replay, SaveResult* r) {
    __try {
        r->ok = -1;
        if (!replay) { r->why = "no race replay"; return; }
        if (field<int>(replay, off::kReplayRecording) != 0) { r->why = "replay is still recording"; return; }
        void* challenge = field<void*>(replay, off::kReplayChallenge);
        if (!challenge) { r->why = "replay has no map"; return; }
        void* folder = field<void*>(app, off::kAppMyReplays);
        if (!folder) { r->why = "no My Replays folder"; return; }

        // Default name exactly as the game builds it: map name + "(mm'ss''cc)" (stunts: "(score)").
        wchar_t base[300];
        copyOut(field<FastStringInt>(challenge, off::kChallengeName), base, 200);
        wchar_t suffix[40];
        if (field<int>(challenge, off::kChallengePlayMode) == 5) {
            const int score = fn<FnBestScore>(g_game->bestStuntScore)(replay, nullptr);
            swprintf_s(suffix, L"(%d)", score);
            r->time = score;
        } else {
            int time = -1, other = 0;
            fn<FnBestTime>(g_game->bestGhostTime)(replay, nullptr, &time, &other);
            r->time = time;
            formatRaceTime(time, suffix, _countof(suffix));
        }

        FastStringInt raw, filtered;
        initString(raw);
        initString(filtered);
        const FnExists exists = fn<FnExists>(g_game->fileExists);
        bool found = false;
        wchar_t stem[260];
        swprintf_s(stem, L"%s%s", base, suffix);
        for (int attempt = 1; attempt <= 20 && !found; attempt++) {
            wchar_t full[360];
            if (attempt == 1) swprintf_s(full, L"%s.Replay.Gbx", stem);
            else swprintf_s(full, L"%s (%d).Replay.Gbx", stem, attempt);
            setString(raw, full);
            fn<FnFilter>(g_game->filterFileName)(&filtered, &raw, 0, 0);
            if (!exists(&filtered, folder)) found = true; // the game would ask "Overwrite?"; we never overwrite
        }
        copyOut(filtered, r->name, 300);
        if (!found) {
            emptyString(raw);
            emptyString(filtered);
            r->why = "file exists (20 names tried)";
            return;
        }

        alignas(16) static unsigned char archive[0x400];
        memset(archive, 0, sizeof(archive));
        fn<FnArchCtor>(g_game->archiveCtor)(archive, nullptr);
        const int saved = fn<FnArchSave>(g_game->archiveSave)(archive, nullptr, &filtered, replay, folder, 10, 7, 1, 1);
        fn<FnArchDtor>(g_game->archiveDtor)(archive, nullptr);

        void* engineMain = *reinterpret_cast<void**>(rebase(g_game->mainEngine));
        void* systemEngine = field<void*>(field<void*>(engineMain, 0x28), 0x2c);
        fn<FnUnbind>(g_game->unbindFid)(systemEngine, nullptr, replay);

        FastStringInt path;
        initString(path);
        void** fvt = field<void**>(folder, 0);
        reinterpret_cast<FnGetFullPath>(fvt[off::kFidsGetFullPath / 4])(folder, nullptr, &path);
        copyOut(path, r->folder, 300);
        emptyString(path);
        emptyString(raw);
        emptyString(filtered);

        if (saved && g_cfg.scanReplays) fn<FnScan>(g_game->scanReplays)(app, nullptr); // as the game does after a save
        r->ok = saved ? 1 : 0;
        r->why = saved ? "" : "CSystemArchiveNod save returned 0";
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        r->ok = 0;
        r->why = "exception inside the game's save code";
    }
}

void saveNow(void* replay, const char* reason) {
    if (g_saving) return;
    if (!g_app) {
        AS_LOG("[Solo] save (%s): no race seen yet, nothing to save", reason);
        return;
    }
    g_saving = true;
    SaveResult r = {};
    const DWORD t0 = GetTickCount();
    saveSeh(g_app, replay, &r);
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
        AS_LOG("[Solo] SAVED (%s): %s%s  time=%d  size=%lu bytes  took %lu ms", reason, folder, name, r.time,
               have ? fa.nFileSizeLow : 0ul, ms);
        char hdr[200];
        checkReplayHeader(full, hdr, sizeof(hdr));
        AS_LOG("[Solo] %s", hdr);
        g_stats.saved++;
        wcscpy_s(g_stats.lastPath, full);
    } else if (r.ok == 0) {
        AS_LOG("[Solo] ERROR (%s): %s  [%s]", reason, r.why, name);
        g_stats.failed++;
    } else {
        AS_LOG("[Solo] skipped (%s): %s", reason, r.why);
        g_stats.skipped++;
    }
}

// ---------------------------------------------------------------- hooks
void __fastcall finishDetour(void* race, void* edx, void* player) {
    void* info = nullptr;
    int stateBefore = 0;
    unsigned bestBefore = 0xffffffffu;
    const unsigned soloBefore = g_soloEndCounter;
    __try {
        if (player) {
            info = field<void*>(player, off::kPlayerInfo);
            if (info) {
                stateBefore = field<int>(info, off::kInfoRaceState);
                bestBefore = field<unsigned>(info, off::kInfoBest);
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
        if (field<int>(info, off::kInfoRaceState) != 2) return; // a lap or a rejected crossing
        void* local = fn<FnLocalInfo>(g_game->localPlayerInfo)(race, nullptr);
        if (local != info) return;
        g_app = field<void*>(race, off::kRaceApp);
        const unsigned time = field<unsigned>(info, off::kInfoTime);
        const unsigned best = field<unsigned>(info, off::kInfoBest);
        const bool newBest = best != bestBefore || time < bestBefore;
        AS_LOG("finish: local player, time=%u best=%d (best before %d)%s", time, (int)best, (int)bestBefore,
               newBest ? "  NEW BEST" : "");
        if (g_cfg.bestOnly && !newBest) {
            AS_LOG("finish: Mode=best and no new best, not saving");
            g_armed = false;
            return;
        }
        if (g_soloEndCounter != soloBefore) {
            saveNow(g_lastSoloReplay, "finish"); // the race end ran inside the finish already
        } else {
            g_armed = true;
            g_armedRace = race;
            g_armedTime = time;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        AS_LOG("ERROR: exception while reading the finish state");
    }
}

void __fastcall soloEndDetour(void* race, void* edx) {
    reinterpret_cast<FnRaceVoid>(g_soloEndHook.trampoline)(race, edx);
    void* app = nullptr;
    void* replay = nullptr;
    __try {
        app = field<void*>(race, off::kRaceApp);
        replay = app ? field<void*>(app, off::kAppReplay) : nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    if (!app) return;
    g_app = app;
    g_soloEndCounter++;
    g_lastSoloReplay = replay;
    if (g_armed && g_finishDepth == 0 && g_armedRace == race) {
        g_armed = false;
        AS_LOG("[Solo] race end after the finish (time=%u), saving the race replay", g_armedTime);
        saveNow(replay, "finish");
    }
}

// ---------------------------------------------------------------- test key
HHOOK g_msgHook = nullptr;

LRESULT CALLBACK getMsgProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && wParam == PM_REMOVE && g_cfg.testKey) {
        const MSG* m = reinterpret_cast<const MSG*>(lParam);
        if ((m->message == WM_KEYDOWN || m->message == WM_SYSKEYDOWN) && m->wParam == g_cfg.testKey &&
            !(m->lParam & (1 << 30)))
            saveNowManual();
    }
    return CallNextHookEx(g_msgHook, code, wParam, lParam);
}

} // namespace

const Stats& stats() { return g_stats; }

void saveNowManual() {
    void* replay = nullptr;
    if (g_app) {
        __try {
            replay = field<void*>(g_app, off::kAppReplay);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            replay = nullptr;
        }
    }
    saveNow(replay, "test key");
}

bool start(const LegacyGame& game, uintptr_t base, const Config& cfg, DWORD mainThread) {
    g_game = &game;
    g_base = base;
    g_cfg = cfg;
    g_stats = Stats{};
    g_app = nullptr;
    g_finishDepth = 0;
    g_soloEndCounter = 0;
    g_lastSoloReplay = nullptr;
    g_armed = false;
    g_armedRace = nullptr;
    g_saving = false;

    g_finishHook = InlineHook{game.onFinishLine, {}, sizeof(game.finishPrologue), "CTrackManiaRace::OnFinishLine", nullptr};
    memcpy(g_finishHook.prologue, game.finishPrologue, sizeof(game.finishPrologue));
    g_soloEndHook = InlineHook{game.soloRaceEnd, {}, sizeof(game.soloEndPrologue), "CTrackManiaRace1P race end", nullptr};
    memcpy(g_soloEndHook.prologue, game.soloEndPrologue, sizeof(game.soloEndPrologue));

    // Race end first: a finish hook without it would only arm and never save.
    if (!installHook(g_soloEndHook, reinterpret_cast<void*>(&soloEndDetour), base)) {
        AS_LOG("solo race end hook missing, autosave inactive");
        return false;
    }
    if (!installHook(g_finishHook, reinterpret_cast<void*>(&finishDetour), base)) {
        AS_LOG("finish hook missing, autosave inactive");
        return false;
    }
    if (cfg.testKey && mainThread) {
        g_msgHook = SetWindowsHookExW(WH_GETMESSAGE, getMsgProc, nullptr, mainThread);
        AS_LOG(g_msgHook ? "test key 0x%02X active (saves the last solo replay now)" : "test key 0x%02X: hook failed",
               cfg.testKey);
    }
    AS_LOG("ready (%s): saving the solo race replay after every %s of the local player; LAN/online races are not saved",
           game.name, cfg.bestOnly ? "new best finish" : "finish");
    return true;
}

} // namespace legacy
} // namespace autosave
