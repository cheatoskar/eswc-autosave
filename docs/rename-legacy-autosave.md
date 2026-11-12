# Rename to "Legacy Autosave" (prepared, not done)

Status: proposal. Nothing below is applied yet; the code, files and repo keep the name `eswc-autosave` until I approve the rename.

Update 1.3.0: the product name in the README, menu and log is now **Legacy Essentials** (autosave plus controller fix, login fix and menu), not "Legacy Autosave". The repo name, file names, export and Horizon module id are unchanged, as recommended below; a repo rename would use "Legacy Essentials" in the texts below.

Why: since 1.2.0 the mod supports TrackMania Nations ESWC, TrackMania Sunrise and TrackMania Original. "ESWC Autosave" undersells that and makes Sunrise/Original players skip it.

Proposed names: product **Legacy Autosave**, GitHub repo **`cheatoskar/tm-legacy-autosave`** (GitHub keeps a redirect from the old URL after a rename, for web and git).

## What changes on rename, exactly

| Item | Today | After | Note |
|---|---|---|---|
| GitHub repo name | `cheatoskar/eswc-autosave` | `cheatoskar/tm-legacy-autosave` | rename in the repo settings; old URLs redirect, but update links anyway |
| README title and first paragraph | `# eswc-autosave` | `# Legacy Autosave` (text below) | |
| Release zip name | `eswc-autosave-<ver>.zip` | `legacy-autosave-<ver>.zip` | the launcher looks for "a zip" in the release (eswcmods.rs `url("eswc-autosave")`), check the match string |
| Release title | `eswc-autosave <ver>` | `Legacy Autosave <ver>` | |
| Log prefix line | `---- eswc_autosave <ver> start ----` | `---- Legacy Autosave <ver> start ----` | text only |
| DLL / ini / log file names | `eswc_autosave.dll`, `eswc_autosave.ini`, `eswc_autosave.log`, export `EswcAutosaveInit` | **unchanged** (recommended) | renaming them breaks updates (old ini/log stay behind), the Horizon module's standalone detection (`eswc_autosave.dll` + `dinput8.chain.dll`), and the launcher (`eswcmods.rs`, `proxymods.rs`). If they ever change: ship both names for one version and migrate the ini |
| `dinput8.dll` loader | unchanged | unchanged | |
| Horizon module | id `eswc-autosave`, name "Autosave & Login-Fix" | unchanged | the id is stored in profiles and settings; the shown name is already game-neutral |
| trackmania-internals | `sources.md` row "eswc-autosave", links in `games/tmn-eswc/addresses.md` (source keys `as`, `online`), `modding/loaders-and-hooks.md`, `tools/scripts.md`, `server/masterserver.md`, `contradictions.md` rows 14/20 | new repo URL and name in the links; keep "eswc-autosave" as the old name once ("formerly eswc-autosave") | line numbers in `as:NN` keys refer to `src/autosave.cpp` and stay valid |
| TM Horizon launcher catalog (`tm-hub-mono`) | `packages/catalog/mods_seed.py` + `catalog.json` (`mod-eswc-autosave`, name "ESWC Autosave", games `["eswc"]`, URL `…/eswc-autosave/releases/latest`, install text "next to TmNationsESWC.exe"), `checks.json` (release tag URL), `sources.json`, `apps/launcher/src/generated/hub/projects.json` (generated from GitHub), `apps/launcher/src-tauri/src/eswcmods.rs` (`repo: "cheatoskar/eswc-autosave"`), i18n strings "ESWC Autosave" | name "Legacy Autosave", games `["eswc","sunrise","original"]`, new repo URL, install text "next to the game exe" | keep the catalog id `mod-eswc-autosave` (installed state refers to it) or add an alias |
| Horizon host docs | `docs/plugin-abi.md` row "own repo `cheatoskar/eswc-autosave`", plugin comments "standalone ESWC Autosave" | new repo name; toast text "Eigenständiges Legacy Autosave gefunden – Modul pausiert" | the toast text is in `plugins/eswc-autosave/autosave_plugin.cpp` (`kStandaloneNote`) |
| Discord / TMX posts | "ESWC Autosave" | "Legacy Autosave (formerly ESWC Autosave)" | manual |

## Prepared README start (after the rename)

```markdown
# Legacy Autosave

Automatic replay saving for the old TrackMania games: **TrackMania Nations ESWC**, **TrackMania Sunrise** and **TrackMania Original** (formerly "eswc-autosave"). Plus the online login fix for ESWC and Original.

Drop three files next to the game exe and every finish is saved as a normal `.Replay.Gbx`, the same file you get from "Save replay", just without clicking. The old games only keep a ghost of your best run in `Autosaves` (not validable); this mod saves the full replay with the game's own save code.
```

The rest of the README stays as it is (file names unchanged).

## Prepared release notes

### 1.2.0 (under the current name)

```markdown
eswc-autosave 1.2.0: now also TrackMania Sunrise and TrackMania Original

- TrackMania Sunrise (Win10 community repack, TmSunrise.exe) and TrackMania Original (Online Patch v2 exe, TMORIGINAL.EXE): your solo replay is saved after each new personal best, to GameData\Tracks\Replays\My Replays\<Track>(mm'ss''cc).Replay.Gbx, like "Save replay" on the end screen. Solo only; LAN and online races are not saved in these two games.
- TrackMania Original: the online login fix (TLS 1.0 and no revocation check, for the game process only) that the patch's readme asks you to set system-wide.
- The exe is now checked by size, PE timestamp and SHA-256; any other exe: the mod stays off and says so in eswc_autosave.log.
- ESWC: unchanged.

Sunrise and Original are tested against a fake game and the real exes, not yet in a real race: please send your eswc_autosave.log if something looks wrong.

Install: copy dinput8.dll, eswc_autosave.dll and eswc_autosave.ini next to the game exe. With TM Horizon use its "Autosave & Login-Fix" module instead.
```

### First release after the rename

```markdown
Legacy Autosave <ver> (formerly eswc-autosave)

Same mod, new name: it now covers TrackMania Nations ESWC, TrackMania Sunrise and TrackMania Original. File names stay the same (eswc_autosave.dll / .ini / .log), so updating works as before: replace the two DLLs, keep your ini.
```
