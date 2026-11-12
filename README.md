# Legacy Essentials

Small fixes for the old TrackMania games: **TrackMania Nations ESWC**, **TrackMania Sunrise** and **TrackMania Original** (formerly "eswc-autosave").

- **Autosave**: every finish is saved as a normal `.Replay.Gbx`, the same file you get from "Save replay" / "Save previous replay", just without clicking. The old games only keep a ghost of your best run in `Autosaves` (not validable); this saves the full replay. Made for [100% TMX](https://tm-exchange.com) runs together with [nostalgic-completionist](https://github.com/BigBang1112/nostalgic-completionist), but it works on its own too.
- **Controller fix**: a controller plugged in or switched on while the game runs works at once, with the game's own bindings for it. Without it the game only sees controllers that were there at start.
- **Login fix** (ESWC, Original): the online login works again on Windows 10/11 (error 12157).
- **Volume, Music, Sounds, Window mode**: change them in the game, like in the launcher's settings. Each changes at once.
- **Menu**: `Ctrl+F2` in the game switches each of these on or off.

Three files next to the game exe, no installer, no other program needed.

## Requirements

This only works with these exact exes (file size, PE timestamp and SHA-256 must all match):

| Game | Exe | SHA-256 |
|---|---|---|
| TrackMania Nations ESWC 1.80, unofficial community patch (2021), StarForce-free, in-game version 0.1.7.9 | `TmNationsESWC.exe`, 8,987,136 bytes | `983bd12724739dcb0f876aeb3a4259154b068940f5167f57ede750ca452d8068` |
| TrackMania Sunrise, Win10 community repack | `TmSunrise.exe`, 7,904,768 bytes | `e59dfc7eaa5314e85b2b0df6f40ef7ba01a23bd5a9525f84407f5dff301df4fe` |
| TrackMania Original, "Online Patch v2" exe (2023) | `TMORIGINAL.EXE`, 7,883,264 bytes | `75e129d769f4595ddde54a8ef484162915bc2bf998c1a2139cc963294fe8175e` |

With any other exe the mod does nothing (DirectInput is still passed through), and says so in its log.

## Files

| File | What it is |
|---|---|
| `dinput8.dll` | a small loader (a few KB, no C runtime). Windows loads it because the game asks for `dinput8.dll`. It passes every DirectInput call unchanged to the system's `dinput8.dll` (or to `dinput8.chain.dll`, see below) and loads `eswc_autosave.dll` from the same folder. That's all it does (`src/loader.cpp`). |
| `eswc_autosave.dll` | the mod: autosave, TMX copy, login fix, controller fix, menu, log. |
| `eswc_autosave.ini` | settings |

The file names stay as they were in eswc-autosave, so updating keeps your ini and the TM Horizon launcher finds the mod.

## Install

1. Close the game.
2. Download the zip from [Releases](../../releases) and copy `dinput8.dll`, `eswc_autosave.dll` and `eswc_autosave.ini` next to the game exe (`TmNationsESWC.exe`, `TmSunrise.exe` or `TMORIGINAL.EXE`).
   - Another mod's `dinput8.dll` already there: rename it to `dinput8.chain.dll` first. The loader passes DirectInput on to it, so that mod keeps working.
   - With the Horizon Client: see [With the Horizon Client](#with-the-horizon-client).
3. Start the game. `eswc_autosave.log` appears in the game folder and should contain `recognised, sha256 ok` and `ready`. `Ctrl+F2` opens the menu.

Updating from 1.x: replace `dinput8.dll` and `eswc_autosave.dll`; your ini keeps working (new keys get their defaults). Both DLLs must come from the same release, otherwise the log says `loader api … different versions` and the mod stays off.

No `eswc_autosave.log` after starting the game means `eswc_autosave.dll` is missing or could not be loaded: the game then runs without the mod.

To remove it, delete `dinput8.dll` and `eswc_autosave.dll` (and the ini/log), and rename a `dinput8.chain.dll` back to `dinput8.dll`. Deleting only `eswc_autosave.dll` also turns the mod off.

## Menu

`Ctrl+F2` opens and closes it (`Esc` closes it too). Mouse or keyboard (arrow keys, `Space`). While it is open the game gets no keyboard or mouse input; controllers are not touched.

| Item | |
|---|---|
| Autosave | on/off; status `On`, `Off` or how many replays were saved this start |
| Best times only | save only new personal bests (`Mode=best`) |
| Open folder | opens the folder of the last saved replay (`My Replays` before the first save) |
| Controller fix | on/off; status `On`, the number of pads it added, or `Unsupported` (the game code is not the one it knows) |
| Login fix | ESWC and Original; a change takes effect at the next start (`Restart game`) |
| Volume | the game's volume in Windows (the same slider as the game in the Windows volume mixer) |
| Music, Sounds | the game's own music and sound volume, heard at once |
| Window mode | `Windowed` or `Fullscreen`, switched at once |
| Version, game, menu key | |

Every change is written to `eswc_autosave.ini` at once. A short note in the top right corner shows saved replays and added controllers. The key is `MenuKey` in the ini (`0` = no menu). `Ctrl+F2` because the plain F-keys are taken in these games by other mods (TM Vibrant Shaders: F3, F6, F7, F8, F12; CenterSpeed: F5; this mod's own test key: F9). The key that opened the menu is held back from the game until it is released.

## What it does

### Autosave

| Mode | What gets saved |
|---|---|
| Solo (campaign, single tracks), all three games | the race you just finished, like "Save replay" on the end screen |
| LAN / online server, ESWC only | your previous run, like Esc → "Save previous replay" |

Replays go to `GameData\Tracks\Replays\My Replays\` as `<Track>(mm'ss''cc).Replay.Gbx` (stunts: `<Track>(score).Replay.Gbx`), the name the game itself suggests. Existing files are never overwritten (you get `… (2).Replay.Gbx` instead). Every save is logged with `[Solo]` or `[LAN]` plus a header check (`validable=1`).

Sunrise and Original: solo only: campaign and single tracks (the race classes `1P`, `1PGhosts`, `1PSpecial`). LAN and online races are never saved there, and neither are hot-seat and Crazy mode. With `Mode=best` (the default) a replay is saved only when the run is a new personal best on the map since the game started. The mod calls the game's own save code, the same calls "Save replay" makes after you press OK, so the replay and its `validable` flag are exactly what the game would write.

### TMX copy (servers with other players, ESWC)

A replay saved on a server holds a ghost for every player there, plus one for the server account on a dedicated server. The game marks such a file `validable="0"`: its rule is "exactly one ghost and recorded inputs". TM-Exchange does not accept it.

The TMX admins are fine with these copies for TMN-X uploads, as long as the run was driven legit, your ghost is in it and the replay can be played against in-game. Upload the TMX copy, not the original.

With `TmxCopy=1` in the ini (the default) the DLL writes a second file next to the normal one, `<Track>(mm'ss''cc) TMX.Replay.Gbx`:

- It keeps **only your ghost and your inputs**, byte for byte: ghost, inputs, times, the mode (`Net-TimeAttack`) and the exe checksum stay as the game wrote them. Removed are the other ghosts and what follows from that (ghost count, node numbers). The header's `validable` is then computed from the new file with the game's own rule, never set by hand.
- The result has **the same structure the game itself writes for a LAN race you play alone**.
- Your ghost is the one whose finish time equals the length of the recorded inputs (the inputs always belong to your run) and equals your finish time at the line. Exactly one ghost must match, otherwise there is no copy.

What it does **not** do: the game cannot re-validate network TimeAttack replays ("Validation of replays recorded in mode Net-TimeAttack not implemented"), neither the original nor the copy. The TMX copy is therefore exactly as trustworthy as a LAN replay, no more.

No copy, logged as `[TMX] skipped: <reason>`, when the replay has no inputs (e.g. edited in MediaTracker), no ghost matches the inputs (e.g. the saved run is an unfinished attempt), it contains nodes the converter does not know, or it is not an ESWC replay. Solo saves and replays with one ghost are left alone, they are validable already.

Each copy is written to a temp file, read back and checked (one ghost, inputs = ghost time, everything else unchanged) before it gets its name; existing files are never overwritten (`… TMX (2).Replay.Gbx`). The work runs in a background thread after the normal save, so the game does not wait for it. Log line on success: `[TMX] SAVED: <path> ghosts 4->1 time=31190 validable=1`.

### Controller fix

The games look for controllers once, at start. A pad plugged in or switched on later (an Xbox pad turned on during a race, a wheel plugged in after the menu) is not seen until a restart. The controller fix watches for new controllers and adds them the way the game adds a controller it finds at start: the game's own device setup, then the game's own bindings for that pad (the ones saved in your profile, or the game's defaults), also into the race that is running. A pad unplugged and plugged in again keeps its place and bindings. It never reads or sends input.

Before it does anything it checks the game's input code byte for byte (6 code signatures and 5 table entries, plus 15 code signatures of the bindings code). If one differs, the fix stays off (`Unsupported` in the menu, the reason in the log); if only the bindings part differs, the pad is still added and gets its bindings in the game's input settings.

This is the Horizon Client's controller fix (module "Controller fix" 1.2.1), the same code.

### Login fix (ESWC, Original)

Clicking "Internet" in ESWC on Windows 10/11 fails with error **12157**. The login server is still up, but it only speaks TLS 1.0 and its certificate expired in 2020, and Windows no longer allows TLS 1.0 by default. With `OnlineFix=1` the DLL allows TLS 1.0 and skips the revocation check **for the game process only**. Nothing system-wide changes, and requests and servers stay the same.

The connection is weak by today's standards, so use a password for your ESWC account that you don't use anywhere else.

TrackMania Original (Online Patch v2): the patch's readme asks you to allow TLS 1.0 and to turn off the revocation check in the Windows Internet Options, for the whole system. With `OnlineFix=1` the mod does the same two things for the game process only, at the exe's own `HttpOpenRequestA` import. Sunrise: no login fix (not known to need one, nothing changed).

This is the Horizon Client's login fix, the same code.

### Volume, Music, Sounds, Window mode

The three games keep music volume, sound volume and fullscreen in their settings file (`Default.SystemConfig.Gbx`, written by the launcher's settings dialog). At start the game reads it, makes the window and Direct3D device from it, and hands the two volumes to its sound code, which uses them for every sound and the music each frame.

- **Music** and **Sounds** change those two values in the running game (the settings and the sound code's copy, as the game's own start does), so you hear the change at once.
- **Window mode** uses the game's own switch: the same functions the game calls at start when its settings say fullscreen, and that its (hidden) developer command "ToggleFullscreen" calls while it runs. They keep the window's size and place for the way back, and use the fullscreen resolution of the launcher's settings. The switch runs between two frames, where the game's window procedure runs. The fullscreen setting follows, so the next start uses the same mode.
- **Volume**: the games have no master volume, so this is the game's volume in Windows (Windows remembers it for the game).

The game never writes these back to its settings file while it runs, so Legacy Essentials keeps your choice in the ini (`[sound]`, `[display]`) and puts it into the game's settings at every start, before the game uses them. Until you change a value in the menu, the game's own value stays. The launcher's settings dialog still shows its own value.

Before anything is changed, the game's code that holds these settings and the game's own description of each field are checked (per game, logged). If one differs, only that row says `Unavailable`. This is the Horizon Client's Game settings module, the same code.

### Online safety

Nothing here sends input, changes physics or touches a race. Autosave calls the game's own save after the finish; the controller fix only does what the game does for a controller it finds at start; the login fix changes two connection settings of the game's own HTTPS requests; volume and window mode are the settings the game's own launcher dialog changes; the menu draws on top of the picture and holds back keyboard and mouse only while it is open.

## With the Horizon Client

The Horizon Client (its loader `dinput8.dll` + `horizon\host.dll`) has the controller fix, the login fix, game settings and its own menu. Legacy Essentials steps aside for them, so nothing is hooked twice:

| | Horizon Client present |
|---|---|
| Autosave | Legacy Essentials (the client's Autosave module pauses itself when it finds `eswc_autosave.dll`) |
| Controller fix | the client's module (whoever starts first claims it; the client's module starts first) |
| Login fix | the client's (`OnlineFix` in the client's settings) |
| Volume, Music, Sounds, Window mode | the client's Game settings module (its own saved values); with an older client without that module, Legacy Essentials still puts the ini's values into the game at start |
| Menu | the client's (`Insert`); Legacy Essentials draws nothing |

Install: rename Legacy Essentials' `dinput8.dll` to `dinput8.chain.dll` and put it next to the client's `dinput8.dll`, with `eswc_autosave.dll` and the ini. The log then says `Horizon Client expected` at start and `Horizon Client runs` on the first message. If the client does not start (Ctrl+Shift held at start), Legacy Essentials runs its login fix and controller fix itself; its menu stays off for that start.

## Settings (`eswc_autosave.ini`)

| Section | Key | Default | |
|---|---|---|---|
| `[autosave]` | `Enabled` | `1` | autosave on/off (menu) |
| | `Mode` | `best` | `best` = only new personal bests on the track (since game start), `all` = every finish (menu) |
| | `TestKey` | `0x78` (F9) | save right now, `0` = off |
| | `ScanReplays` | `1` | refresh the in-game replay list after saving |
| | `TmxCopy` | `1` | write the TMX copy after LAN/online saves (ESWC only) |
| `[online]` | `OnlineFix` | `1` | login fix on/off (ESWC and Original; next start) (menu) |
| `[controller]` | `ControllerFix` | `1` | controller fix on/off (menu) |
| `[menu]` | `MenuKey` | `Ctrl+F2` | menu key, `0` = no menu |
| `[sound]` | `Music` | empty | music volume `0`-`100`, empty = the game's own (menu) |
| | `Sounds` | empty | sound volume `0`-`100`, empty = the game's own (menu) |
| `[display]` | `WindowMode` | empty | `windowed` or `fullscreen` at start, empty = the game's own (menu) |

## Status

Version 1.3.0. Autosave on ESWC: tested on Windows 11 (solo, LAN with nostalgic-completionist, online server, login). Sunrise and Original autosave: tested against a fake game image and the real exes on disk; not yet in a real race. Controller fix and login fix: the Horizon Client's code; in the client the controller fix is in test, the login fix is tested on ESWC. The menu: tested on a Direct3D 9 test window (`build\test\menu_test.exe`), not yet in the games. Volume, Music, Sounds, Window mode: from a study of the three exes, checked against them on disk and against a fake game; not yet in the games.

## How it works

The game loads `dinput8.dll` from its own folder before the system one. The loader in it forwards DirectInput to the system DLL (or `dinput8.chain.dll`) and, after the game first creates DirectInput (main thread, before any race), loads `eswc_autosave.dll` and calls its `EswcAutosaveInit`. The mod checks the exe (size, timestamp, hash), reads the ini and starts each part:

- autosave: a few small hooks on the game's code (finish line, the moment the game keeps a copy of the run, the solo race end), then the game's own synchronous save on the game thread. Addresses: `src/autosave.cpp` (ESWC), `src/games.h` / `src/legacy.cpp` (Sunrise, Original).
- login fix and controller fix: on the game thread's first message (by then the Horizon Client, if there, has started). `src/essentials.cpp`, `src/controller.cpp`.
- game settings: the ini's values go into the game's settings right away (still inside the game's DirectInput call, before it makes the window, the Direct3D device and its sound). `src/gamesettings.cpp`, `vendor/horizon/game-settings/gs_game.h`.
- menu: Direct3D 9 method-table hooks (CreateDevice, Present, Reset) and the game window's message procedure; Dear ImGui draws it. `src/overlay.cpp`.

## Code shared with the Horizon Client

The login fix, the controller fix, the game settings and Dear ImGui are the Horizon Client's files, copied unchanged into `vendor/horizon/` from the client's repo (`vendor/horizon/source.h` names the commits: one for the login fix, controller fix and ImGui, one for the game settings, until both are on one branch there). A fix is made in the client first and brought over with

```
powershell -ExecutionPolicy Bypass -File tools/sync-horizon.ps1 -Commit <commit>
powershell -ExecutionPolicy Bypass -File tools/sync-horizon.ps1 -SettingsCommit <commit>
```

`tools/sync-horizon.ps1 -Check` confirms that every vendored file is byte for byte the client's file at its commit (`test/run_tests.cmd` runs it). `src/gamesettings.cpp` is the glue for the game settings. `src/shim/host/host.h` gives the client's `onlinefix.cpp` this mod's log; `src/controller.cpp` is the glue the client's standalone controller build also has.

## Build

`build.cmd` (MSVC Build Tools, x86) builds `dist\dinput8.dll`, `dist\eswc_autosave.dll` and the tests; `test\run_tests.cmd [folder with the three exes]` runs them:

| Test | |
|---|---|
| `essentials_test` | menu key, ini read/write-back, the Horizon Client plan |
| `hp_tests` | the client's controller-fix unit tests, on the vendored code |
| `gs_tests` | the client's game-settings unit tests (fake game in memory, the Windows audio session), on the vendored code |
| `menu_test` | the real overlay on a Direct3D 9 test window: hooks, `Ctrl+F2`, drawing, keyboard toggles written to the ini, Music and Window mode on a fake game, toast (writes `menu_test.bmp`) |
| `legacy_test` | Sunrise/Original autosave against a fake game image; with exes also hash, prologues, addresses |
| `realexe_test` | the vendored controller fix's and game settings' code checks and the login fix's import slots against the real exes (read only) |
| `tmxcopy_test <outdir> [--login L] [--time ms] [--ref DIR] <replays…>` | the TMX-copy conversion on replay files |
| `onlinetest` | the login fix inside a test exe, one real HTTPS request to the ESWC server (by hand) |
| `test\run_harness.cmd` | the loader in a test program: with and without the mod, with a `dinput8.chain.dll`, and as `dinput8.chain.dll` itself |

## Changes

See [CHANGELOG.md](CHANGELOG.md).

## License

MIT. `vendor/horizon/imgui` is Dear ImGui (MIT, see its `LICENSE.txt`).
