#pragma once
// Controller fix in this mod: the glue around the Horizon Client's controller-hotplug code
// (vendor/horizon/controller-hotplug, the same files the client's module and its standalone build use).
// Everything here runs on the game's main thread.
#include <windows.h>
#include <stdint.h>

namespace autosave {
namespace controller {

enum class State {
    Off,      // switched off, not started
    Running,  // ours runs
    Paused,   // ours was started, then switched off in the menu (worker idle, no game calls)
    Client,   // the Horizon Client's module (or another copy) has it in this process
    Unknown,  // this exe's input code is not the one the fix knows (logged)
    Failed,   // could not start (logged)
};

using KickFn = void (*)();               // any thread: call tick() on the game thread soon
using ToastFn = void (*)(const char* t); // game thread, UTF-8

// Remembers the game and starts the fix if `on`. exeTimestamp: PE TimeDateStamp.
State init(uintptr_t exeBase, uint32_t exeTimestamp, KickFn kick, ToastFn toast, bool on);
void tick();
// From the menu. On while Off starts it (claim, code check, worker); off pauses it.
void setEnabled(bool on);
State state();
int padsInGame(); // controllers the fix registered that are plugged in
const char* stateName(State s);

} // namespace controller
} // namespace autosave
