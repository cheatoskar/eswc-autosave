# Rename the repo to `legacy-essentials` (planned, not done)

Since 1.3.0 the product is **Legacy Essentials** (README, menu, log, release title). The GitHub repo is still
`cheatoskar/eswc-autosave`. After the rename in the repo settings GitHub redirects the old name for the web,
git and the API, so nothing breaks the same day; the list below is what to update afterwards.

## Stays as it is

| Item | Why |
|---|---|
| `dinput8.dll`, `eswc_autosave.dll`, `eswc_autosave.ini`, `eswc_autosave.log`, export `EswcAutosaveInit` | renaming breaks updates (old ini and log stay behind), the Horizon module's standalone detection and the TM Horizon launcher |
| Horizon module id `eswc-autosave` | stored in players' profiles and settings |
| Release asset prefix `eswc-autosave-` (`eswc-autosave-<ver>-<Game>.zip`) | TM Horizon 0.5.x picks the first release asset whose name starts with `eswc-autosave` (`eswcmods.rs`). Change only after a launcher that also accepts `legacy-essentials` is out |

## Update after the rename

| Where | What |
|---|---|
| this repo | `showcase.json` links, README links, local remotes: `git remote set-url origin https://github.com/cheatoskar/legacy-essentials.git` |
| TM Horizon hub | `apps/server/src/checks/dinput8.ts` accepts both repo names for the same project (prepared on `prod-setup`); the project's GitHub source URL in the portal |
| TM Horizon launcher | `eswcmods.rs` repo `cheatoskar/eswc-autosave` (works through the redirect; change in a later version), `GamePage.tsx` link, catalog entry `mod-eswc-autosave` (id stays, name and URL change), `release-policy.json` key, `showcases/eswc-autosave.json`, `checks.json` |
| Horizon Client | `docs/plugin-abi.md` row with the repo name |
| trackmania-internals | links in `sources.md` and the ESWC address pages |
| website | catalog and showcase data come from the hub, so a hub redeploy and catalog import update them |

Never create a new repo named `eswc-autosave` afterwards: that ends the redirect.
