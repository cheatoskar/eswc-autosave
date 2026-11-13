# Security policy

## Reporting a vulnerability

Please do not open a public issue for a security problem. Use GitHub's private reporting instead: open the [Security tab](../../security), choose "Report a vulnerability" and describe the problem, with steps to reproduce if you can. The report stays private until a fix is out.

Reports are answered as soon as possible, usually within a few days. This is a hobby project, so there is no fixed deadline.

## Supported versions

Only the latest release gets fixes. Please check that the problem is still there in the latest release before reporting.

## Scope

In scope:

- the `dinput8.dll` loader and `eswc_autosave.dll`: anything that lets a file in the game folder run code it should not, or that weakens the exe check (known SHA-256) that keeps the mod off in unknown builds
- the login fix: it allows TLS 1.0 and skips the revocation check for the game process only. Anything that makes this reach other processes or the system is a vulnerability
- replay files the mod writes (autosave and the TMX copy): path handling, overwriting existing files

Out of scope:

- the old protocol and expired certificate of the game's own login server, which the login fix works around on purpose
- vulnerabilities in the games or other mods loaded next to it
- problems that need an attacker who already controls your PC
