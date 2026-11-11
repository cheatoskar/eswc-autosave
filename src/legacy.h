#pragma once
#include <windows.h>
#include <stdint.h>

#include "games.h"

namespace autosave {
namespace legacy {

struct Config {
    bool bestOnly = true;     // Mode=best: only a new personal best (this session) is saved
    bool scanReplays = true;  // refresh the game's replay list after a save
    UINT testKey = VK_F9;     // 0 = off
};

// Solo autosave for Sunrise and Original: hooks OnFinishLine and the solo race end of `game` in the
// exe loaded at `base`, then saves the race replay after each local finish (Mode=best: new best only).
// Returns false when the finish hook could not be installed (the mod then does nothing).
// mainThread: the thread for the test key (0 = no test key). Can be called again (tests): the
// previous hooks must be gone with their image.
bool start(const LegacyGame& game, uintptr_t base, const Config& cfg, DWORD mainThread);

// Counters for tests and the log.
struct Stats {
    unsigned saved;
    unsigned skipped;
    unsigned failed;
    wchar_t lastPath[620]; // full path of the last saved file
};
const Stats& stats();

// Saves the current race replay now, if the game holds a finished (no longer recording) one.
void saveNowManual();

} // namespace legacy
} // namespace autosave
