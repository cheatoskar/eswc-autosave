# Contributing

Bug reports, ideas and pull requests are welcome. The project is called Legacy Essentials; the repository is still named `eswc-autosave` (see [docs/rename-legacy-essentials.md](docs/rename-legacy-essentials.md)).

## Reporting a bug

Open an [issue](../../issues) and include:

- the game and its version (and which exe, if you know it)
- the version of Legacy Essentials (shown in its menu and in the log), and which of the three games: ESWC, Sunrise or Original
- what you did, what you expected and what happened
- `eswc_autosave.log` from the game folder (it is written at every start). No log at all means the mod was not loaded: the README says what to check.

A screenshot or a short clip helps. Search the existing issues first.

## Feature ideas

Open an issue and describe the problem you want solved. Check the rules below first: some things are out of scope on purpose.

## Pull requests

Pull requests are welcome. Keep them small and focused: one fix or one feature per pull request. For a larger change, open an issue first so the direction is clear before you spend time on it.

In the description, say what changed and how you tested it in the game (game, version, what you did). Sunrise and Original are only partly tested in a real race, so notes on those games are especially useful.

## Build and test

MSVC Build Tools (x86), as in the [README](README.md#build):

```
build.cmd
test\run_tests.cmd [folder with the three exes]
```

`build.cmd` builds `dist\dinput8.dll`, `dist\eswc_autosave.dll` and the tests. Files under `vendor\horizon` are copies of the Horizon Client's code: do not edit them here. `tools\sync-horizon.ps1 -Check` (also run by `run_tests.cmd`) verifies that they match the client's files byte for byte.

## Rules for in-game mods

These mods run inside the game, also in online play, so a few things are out of scope. A pull request that does any of this will not be merged:

- features that give a competitive advantage online or on leaderboards
- anything that touches physics, inputs or timing of the simulation, or edits runs and replays
- checkpoint or trigger display, ideal or brake line, disabling the camera or MediaTracker behaviour
- anything that gets around the Competition Patch or other anti-cheat and replay validation

The README section "Online safety" states what the mod does and does not touch: it sends no input, changes no physics and touches no race. The autosave only calls the game's own save after the finish. Changes must keep that true: [Online safety](README.md#online-safety). The rules come from what the TrackMania Exchange moderators accept: [TMX rules for mods](https://github.com/cheatoskar/trackmania-internals/blob/main/modding/tmx-rules-for-mods.md).

## AI-assisted contributions

Contributions written with AI tools are fine, as long as the contributor has tested them and can explain what the change does. Untested generated code will be sent back.
