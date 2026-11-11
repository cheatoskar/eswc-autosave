#pragma once
// The exes this mod knows. Each one is pinned by file size, PE timestamp and SHA-256; any other exe
// (another patch, a cracked or re-linked copy) is refused and the mod stays off.
//
// ESWC's addresses stay in autosave.cpp (unchanged since 1.0). Sunrise and Original use legacy.cpp,
// with the addresses below. How each one was found and checked: see the table in legacy.cpp and
// trackmania-internals games/tm-sunrise/replay-save.md, games/tm-original/addresses.md.
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>

namespace autosave {

enum class Game { Unknown, Eswc, Sunrise, Original };

struct ExeId {
    Game game;
    const char* name;    // for the log
    const char* sha256;  // lower-case hex
    uint32_t timestamp;  // PE TimeDateStamp
    uint64_t size;       // file size in bytes
};

// Game addresses for the solo autosave of Sunrise and Original (link addresses, image base 0x400000).
struct LegacyGame {
    Game game;
    const char* name;
    // hooks (prologue checked byte for byte before patching)
    uintptr_t onFinishLine;           // CTrackManiaRace::OnFinishLine(CTrackManiaPlayer*), ret 4
    unsigned char finishPrologue[11]; // 8b 44 24 04 53 55 56 33 db 3b c3
    uintptr_t soloRaceEnd;            // CTrackManiaRace1P solo race end (Race1P vtable slot 88), ret
    unsigned char soloEndPrologue[7]; // 83 ec 08 53 55 56 57
    // game functions the save calls (the same ones CTrackMania::SaveReplayRecord calls)
    uintptr_t localPlayerInfo;  // CTrackManiaPlayerInfo* CTrackManiaRace::GetLocalPlayerInfo()
    uintptr_t strSet;           // CFastStringInt::SetString(const SStringParamInt&), ret 4
    uintptr_t strEmpty;         // CFastStringInt::Empty()
    uintptr_t filterFileName;   // cdecl (CFastStringInt& dst, const CFastStringInt& src, 0, 0)
    uintptr_t fileExists;       // cdecl int (const CFastStringInt& name, CSystemFids* folder)
    uintptr_t bestGhostTime;    // CGameCtnReplayRecord::GetBestGhostRaceTime(int&, int&), ret 8
    uintptr_t bestStuntScore;   // CGameCtnReplayRecord::GetBestGhostStuntsScore()
    uintptr_t archiveCtor;      // CSystemArchiveNod::CSystemArchiveNod()
    uintptr_t archiveSave;      // int CSystemArchiveNod::Save(name&, nod, folder, 10, 7, 1, 1), ret 0x1c
    uintptr_t archiveDtor;      // CSystemArchiveNod::~CSystemArchiveNod()
    uintptr_t unbindFid;        // CSystemEngine::UnbindFid(CMwNod*), ret 4
    uintptr_t scanReplays;      // CTrackMania::ScanDiskForReplays()
    // globals
    uintptr_t emptyWideString;  // wchar_t* of every empty CFastStringInt
    uintptr_t mainEngine;       // CMwEngineMain*; ->+0x28 ->+0x2c = CSystemEngine*
};

// Struct members, the same in Sunrise and Original (reflection tables and the save code agree).
namespace legacy_off {
constexpr size_t kRaceApp = 0x3c;           // CTrackManiaRace.Game -> CTrackMania
constexpr size_t kPlayerInfo = 0x34;        // CTrackManiaPlayer.PlayerInfo
constexpr size_t kInfoRaceState = 0x228;    // 1 = racing, 2 = finished
constexpr size_t kInfoTime = 0x1c0;         // RaceTime (ms)
constexpr size_t kInfoBest = 0x1c4;         // RaceBestTime (ms, 0xffffffff = none)
constexpr size_t kAppReplay = 0x130;        // CConstructionGame.ReplayRecord: the run being / just recorded
constexpr size_t kAppMyReplays = 0x1d8;     // CSystemFids* "My Replays" (subfolder of app+0x1d4 "Replays")
constexpr size_t kReplayChallenge = 0x58;   // CConstructionReplayRecord.Challenge
constexpr size_t kReplayRecording = 0xac;   // != 0 while recording (StopRecord clears it)
constexpr size_t kChallengePlayMode = 0x11c;// CConstructionChallenge.PlayMode, 5 = stunts
constexpr size_t kChallengeName = 0x128;    // CConstructionChallenge.ChallengeName, CFastStringInt {len, ptr}
constexpr size_t kFidsGetFullPath = 0xac;   // CSystemFids vtable: GetFullPath(CFastStringInt& out)
} // namespace legacy_off

inline const ExeId* knownExes(int* count) {
    static const ExeId k[] = {
        {Game::Eswc, "TmNationsESWC 0.1.7.9 (unofficial 1.80 patch)",
         "983bd12724739dcb0f876aeb3a4259154b068940f5167f57ede750ca452d8068", 0x5ce7272f, 8987136},
        {Game::Sunrise, "TmSunrise (Win10 community repack)",
         "e59dfc7eaa5314e85b2b0df6f40ef7ba01a23bd5a9525f84407f5dff301df4fe", 0x5cc8f0cf, 7904768},
        {Game::Original, "TMOriginal (Online Patch v2 exe)",
         "75e129d769f4595ddde54a8ef484162915bc2bf998c1a2139cc963294fe8175e", 0x5d337477, 7883264},
    };
    *count = sizeof(k) / sizeof(k[0]);
    return k;
}

// The exe whose hash, timestamp and size all match, or nullptr.
inline const ExeId* identifyExe(const char* sha256, uint32_t timestamp, uint64_t size) {
    int n = 0;
    const ExeId* k = knownExes(&n);
    for (int i = 0; i < n; i++) {
        const char* a = sha256;
        const char* b = k[i].sha256;
        while (*a && *a == *b) a++, b++;
        if (*a == 0 && *b == 0 && k[i].timestamp == timestamp && k[i].size == size) return &k[i];
    }
    return nullptr;
}

inline const LegacyGame* legacyGame(Game g) {
    static const LegacyGame kSunrise = {
        Game::Sunrise, "Sunrise",
        0x007ec7a0, {0x8b, 0x44, 0x24, 0x04, 0x53, 0x55, 0x56, 0x33, 0xdb, 0x3b, 0xc3},
        0x007dd7b0, {0x83, 0xec, 0x08, 0x53, 0x55, 0x56, 0x57},
        0x0082aeb0, 0x0041a2a0, 0x00401280, 0x00480ae0, 0x0044e860, 0x007abd30, 0x007abe10,
        0x0041e220, 0x00421e60, 0x0041e330, 0x00414e00, 0x00821f30,
        0x00abc0e8, 0x00b8b494,
    };
    static const LegacyGame kOriginal = {
        Game::Original, "Original",
        0x007ead30, {0x8b, 0x44, 0x24, 0x04, 0x53, 0x55, 0x56, 0x33, 0xdb, 0x3b, 0xc3},
        0x007dc250, {0x83, 0xec, 0x08, 0x53, 0x55, 0x56, 0x57},
        0x00829200, 0x004055b0, 0x00401280, 0x00480c10, 0x0044ea70, 0x007aaab0, 0x007aab90,
        0x0041e2b0, 0x00421ee0, 0x0041e3b0, 0x00414e60, 0x00820410,
        0x00ab80e8, 0x00b866f4,
    };
    if (g == Game::Sunrise) return &kSunrise;
    if (g == Game::Original) return &kOriginal;
    return nullptr;
}

// "(mm'ss''cc)" exactly as SaveReplayRecord formats it (negative times by their absolute value).
inline void formatRaceTime(int time, wchar_t* out, size_t cap) {
    int t = time < 0 ? -time : time;
    const int mm = t / 60000;
    const unsigned ss = static_cast<unsigned>(t - mm * 60000) / 1000;
    const unsigned cc = static_cast<unsigned>(t - static_cast<int>(ss) * 1000 - mm * 60000) / 10;
    swprintf_s(out, cap, L"(%.2d'%.2d''%.2d)", mm, ss, cc);
}

} // namespace autosave
