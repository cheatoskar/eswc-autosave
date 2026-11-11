# eswc-autosave

Automatic replay saving (and a fix for the online login) for **TrackMania Nations ESWC**, and since 1.2.0 also for **TrackMania Sunrise** and **TrackMania Original** (solo).

Drop three files next to the game exe and every finish is saved as a normal `.Replay.Gbx`, the same file you get from "Save replay" / "Save previous replay", just without clicking. The old games only keep a ghost of your best run in `Autosaves` (not validable); this mod saves the full replay. Made for [100% TMX](https://tm-exchange.com) runs together with [nostalgic-completionist](https://github.com/BigBang1112/nostalgic-completionist), but it works on its own too.

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
| `dinput8.dll` | a 4 KB loader. Windows loads it because the game asks for `dinput8.dll`. It passes every DirectInput call unchanged to the system's `dinput8.dll` and loads `eswc_autosave.dll` from the same folder. That's all it does (`src/loader.cpp`, ~120 lines, no C runtime). |
| `eswc_autosave.dll` | the mod: autosave, TMX copy, online fix, log. |
| `eswc_autosave.ini` | settings |

## Install

1. Close the game.
2. Download the zip from [Releases](../../releases) and copy `dinput8.dll`, `eswc_autosave.dll` and `eswc_autosave.ini` next to the game exe (`TmNationsESWC.exe`, `TmSunrise.exe` or `TMORIGINAL.EXE`). If the folder already has a `dinput8.dll` from another mod (for example TM Horizon's loader), use that mod's way of adding a second loader instead of overwriting it; with TM Horizon, use its "Autosave & Login-Fix" module instead of this standalone.
3. Start the game. `eswc_autosave.log` appears in the game folder and should contain `recognised, sha256 ok` and `ready: …`.

Updating from 1.0.0 (one `dinput8.dll` with everything inside): replace `dinput8.dll` and add `eswc_autosave.dll`; the ini stays. Both DLLs must come from the same release, otherwise the log says `loader api … different versions` and the mod stays off.

No `eswc_autosave.log` after starting the game means `eswc_autosave.dll` is missing or could not be loaded: the game then runs without the mod.

To remove it, delete `dinput8.dll` and `eswc_autosave.dll` (and the ini/log). Deleting only `eswc_autosave.dll` also turns the mod off.

## What it does

| Mode | What gets saved |
|---|---|
| Solo (campaign, single tracks), all three games | the race you just finished, like "Save replay" on the end screen |
| LAN / online server, ESWC only | your previous run, like Esc → "Save previous replay" |

Replays go to `GameData\Tracks\Replays\My Replays\` as `<Track>(mm'ss''cc).Replay.Gbx` (stunts: `<Track>(score).Replay.Gbx`), the name the game itself suggests. Existing files are never overwritten (you get `… (2).Replay.Gbx` instead). Every save is logged with `[Solo]` or `[LAN]` plus a header check (`validable=1`).

### Sunrise and Original

Solo only: campaign and single tracks (the race classes `1P`, `1PGhosts`, `1PSpecial`). LAN and online races are never saved there, and neither are hot-seat and Crazy mode. With `Mode=best` (the default) a replay is saved only when the run is a new personal best on the map since the game started. The mod calls the game's own save code, the same calls "Save replay" makes after you press OK, so the replay and its `validable` flag are exactly what the game would write. The addresses were found by comparing with the ESWC code and checked twice each (see `src/games.h`, `src/legacy.cpp`); they have not been run in the game yet (status below).

### TMX copy (servers with other players)

A replay saved on a server holds a ghost for every player there, plus one for the server account on a dedicated server. The game marks such a file `validable="0"`: its rule is "exactly one ghost and recorded inputs". TM-Exchange does not accept it.

The TMX admins are fine with these copies for TMN-X uploads, as long as the run was driven legit, your ghost is in it and the replay can be played against in-game. Upload the TMX copy, not the original.

With `TmxCopy=1` in the ini (the default) the DLL writes a second file next to the normal one, `<Track>(mm'ss''cc) TMX.Replay.Gbx`:

- It keeps **only your ghost and your inputs**, byte for byte: ghost, inputs, times, the mode (`Net-TimeAttack`) and the exe checksum stay as the game wrote them. Removed are the other ghosts and what follows from that (ghost count, node numbers). The header's `validable` is then computed from the new file with the game's own rule, never set by hand.
- The result has **the same structure the game itself writes for a LAN race you play alone**.
- Your ghost is the one whose finish time equals the length of the recorded inputs (the inputs always belong to your run) and equals your finish time at the line. Exactly one ghost must match, otherwise there is no copy.

What it does **not** do: the game cannot re-validate network TimeAttack replays ("Validation of replays recorded in mode Net-TimeAttack not implemented"), neither the original nor the copy. The TMX copy is therefore exactly as trustworthy as a LAN replay, no more.

No copy, logged as `[TMX] skipped: <reason>`, when the replay has no inputs (e.g. edited in MediaTracker), no ghost matches the inputs (e.g. the saved run is an unfinished attempt), it contains nodes the converter does not know, or it is not an ESWC replay. Solo saves and replays with one ghost are left alone, they are validable already.

Each copy is written to a temp file, read back and checked (one ghost, inputs = ghost time, everything else unchanged) before it gets its name; existing files are never overwritten (`… TMX (2).Replay.Gbx`). The work runs in a background thread after the normal save, so the game does not wait for it. The in-game replay list shows the copy after its next refresh. Log line on success: `[TMX] SAVED: <path> ghosts 4->1 time=31190 validable=1`.

### Online fix

Clicking "Internet" in ESWC on Windows 10/11 fails with error **12157**. The login server is still up, but it only speaks TLS 1.0 and its certificate expired in 2020, and Windows no longer allows TLS 1.0 by default. With `OnlineFix=1` the DLL allows TLS 1.0 and skips the revocation check **for the game process only**. Nothing system-wide changes, and requests and servers stay the same.

The connection is weak by today's standards, so use a password for your ESWC account that you don't use anywhere else.

TrackMania Original (Online Patch v2): the patch's readme asks you to allow TLS 1.0 and to turn off the revocation check in the Windows Internet Options, for the whole system. With `OnlineFix=1` the mod does the same two things for the game process only, at the exe's own `HttpOpenRequestA` import. Sunrise: no online fix (not known to need one, nothing changed).

## Settings (`eswc_autosave.ini`)

| Key | Default | |
|---|---|---|
| `Enabled` | `1` | autosave on/off |
| `Mode` | `best` | `best` = only new personal bests on the track (since game start), `all` = every finish |
| `TestKey` | `0x78` (F9) | save right now, `0` = off |
| `ScanReplays` | `1` | refresh the in-game replay list after saving |
| `TmxCopy` | `1` | write the TMX copy after LAN/online saves (ESWC only) |
| `OnlineFix` | `1` | online login fix on/off (ESWC and Original) |

## Status

Version 1.2.0. ESWC: tested on Windows 11: solo campaign, LAN with nostalgic-completionist, online server and the online login. Not covered: hotseat/split-screen modes.

Sunrise and Original: built and tested against a fake game image (`build\test\legacy_test.exe`: the real prologues, the game's calls as stubs, the hook route of the real DLL) and against the real exes on disk (hash, prologues, addresses); not yet run in the game. Until a first in-game test the log is the place to look: `build: TmSunrise … recognised`, `hook: … installed` (twice), `ready (Sunrise)`, then `[Solo] SAVED (finish): …` after a new best.

## Changes

- 1.2.0: TrackMania Sunrise (Win10 repack) and TrackMania Original (Online Patch v2 exe): solo autosave; online fix also for Original. The exe is now also checked by its file size.
- 1.1.0: TMX copy for LAN/online replays.
- 1.0.1: split into a small loader (`dinput8.dll`) and the mod (`eswc_autosave.dll`). Behaviour unchanged.
- 1.0.0: first release.

## How it works

The game loads `dinput8.dll` from its own folder before the system one. The loader in it forwards DirectInput to the system DLL and, the first time the game creates DirectInput (main thread, before any race), loads `eswc_autosave.dll` and calls its `EswcAutosaveInit`. The mod then checks the exe (size, timestamp, hash) and installs a few small hooks: the finish line, the moment the game stops recording and keeps a copy of the run (LAN, ESWC), and the solo race end. After a finish it calls the game's own synchronous save routine on the game thread, i.e. the same steps "Save replay" does after you press OK. Addresses are documented in `src/autosave.cpp` (ESWC) and `src/games.h` / `src/legacy.cpp` (Sunrise, Original).

## Build

`build.cmd` (MSVC Build Tools, x86) builds `dist\dinput8.dll` and `dist\eswc_autosave.dll`. `build\test\legacy_test.exe` runs the Sunrise/Original autosave against a fake game image (with `legacy_test.exe <exe> sunrise|original|eswc …` it also checks real exes, read only: hash, size, timestamp, hook prologues, address ranges). `test\run_harness.cmd` loads the loader into a small test program twice: with `eswc_autosave.dll` next to it (DirectInput works, the log reports an unknown exe) and without it (DirectInput works, no log).

`build\test\tmxcopy_test.exe <outdir> [--login L] [--time ms] [--ref DIR] <replays…>` runs the TMX-copy conversion of the DLL on replay files and writes the copies to `<outdir>`; the inputs are only read. It prints a summary of input and output (ghosts, times, EventsDuration, validable, mode) and, with `--ref`, compares each copy byte for byte with `<DIR>\<name> solo.Replay.Gbx`. No replays are in the repo (they contain other players' names).

## License

MIT
