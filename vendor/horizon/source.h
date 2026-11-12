// Written by tools/sync-horizon.ps1. The files in vendor\horizon\ are unchanged copies from these commits of
// horizon-host (cheatoskar/horizon-client); do not edit them here, fix them there and sync again.
#pragma once
// login fix, controller fix, Dear ImGui
#define HORIZON_SOURCE_COMMIT "f74d892d86d7ab3b6b3b0bba2f1c90a06716e0f6"
#define HORIZON_SOURCE_SHORT "f74d892"
#define HORIZON_SOURCE_DATE "2026-11-12"
// branches holding it when synced: pad-fix-2, release-0.3.1
// subject: Controller fix 1.2.0: a late pad gets its bindings at once, like a pad present at start
// game settings (volumes, window mode)
#define HORIZON_SETTINGS_COMMIT "6fdd677787d96343c19a9e62d141d062a6fd1b52"
#define HORIZON_SETTINGS_SHORT "6fdd677"
#define HORIZON_SETTINGS_DATE "2026-11-12"
// branches holding it when synced: release-0.3.1
// subject: Game settings 1.0.1: no stutter while dragging a volume slider
