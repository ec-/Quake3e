# AGENTS.md – doomwarriors-clan/Quake3e

Guidance for coding agents (and people) working on this repository. It lives
only on the `dwc` branch, the default branch of this fork.

## The project around this repo

The doomwarriors clan (https://www.doomwarriors.de) runs four Quake III Arena
servers (Duel, TDM, CTF, Instagib) with the OSP mod. Repos in the GitHub org
`doomwarriors-clan`:

| Repo | What | Default branch |
|---|---|---|
| `Quake3e` (this) | engine: fork of ec-/Quake3e | `dwc` |
| `q3-servers` (private) | Docker Compose deployment of the servers | `main` |
| `dwc-hp` | clan homepage (Play/Java), reads the servers' logs and demos | `master` |
| `osp` (planned) | OSP mod sources, based on JKornev/xq3osp `security-patches`, released as "OSP 1.03a-dwc" | – |

Maintainers: Micha (`n4cer`) and Hannes (`dwc-motter`), both org owners.

Flow: a tag `dwc-YYYY.MM.DD` here builds a release binary → `q3-servers`
pins it in `versions.env` → the server image downloads it. New versions run on
the test instance (q3-servers, UDP 27970) before the live servers.

## Working rules

- Open topics are GitHub issues. Run `gh issue list -R doomwarriors-clan/Quake3e`
  before starting and reference the issue in the PR.
- Every change goes through a pull request; `dwc` and `main` are protected
  (no direct push, no force push).
- **`gh pr create` in a fork targets the upstream repo (ec-/Quake3e) by
  default.** Always pass `-R doomwarriors-clan/Quake3e --base dwc` (or
  `--base main` / `--base multiview` where intended). Never open PRs against
  ec-/Quake3e unless a maintainer asks for it.
- Commit messages in English, imperative mood.

## Branches

| Branch | Purpose | Rules |
|---|---|---|
| `main` | mirror of ec-/Quake3e `main` | fast-forward only from upstream, no own commits |
| `multiview` | server-side multiview demos, kept upstream-ready | based on `main`, no DWC-only files (no `AGENTS.md`, no workflows) |
| `dwc` | integration branch the servers are built from | `multiview` + bot memory + DWC tweaks (`sv_onRestart`), plus `AGENTS.md` and `.github/workflows/dwc-release.yml` |

Feature work meant for upstream branches off `main`; everything else is a PR
against `dwc`.

Upstream sync (roughly monthly): fast-forward `main` from ec-/Quake3e, rebase
or merge `multiview` onto it, merge into `dwc`, build and test, tag.

## Build

The servers only use the dedicated server, built on Debian bookworm (the
server image's base, glibc 2.36):

```sh
docker run --rm -v "$PWD:/src" -w /src debian:bookworm-slim sh -euc '
  apt-get update -qq && apt-get install -y -qq --no-install-recommends build-essential
  make -j"$(nproc)" BUILD_CLIENT=0 BUILD_SERVER=1 USE_MV=1 release'
# -> build/release-linux-x86_64/quake3e.ded.x64
```

A change must build without new compiler warnings. There is no automated test
suite: run the binary on the test instance (q3-servers README, "Testinstanz",
`QUAKE3E_BINARY`) with stock OSP and with bots.

## Releases

Tag a commit on `dwc` as `dwc-YYYY.MM.DD` (`dwc-YYYY.MM.DD.N` for a second
build that day) and push the tag. `.github/workflows/dwc-release.yml` builds
the server and publishes `quake3e.ded.x64-dwc-<commit>` plus `.sha256`. To roll
it out, open a PR in `q3-servers` that updates `versions.env` (tag, asset name,
SHA-256). Releases created by that workflow do not trigger upstream's
`build.yml`.

## Compatibility rules (important)

The servers must keep working with stock QVMs, and our QVMs with stock
Quake3e.

- **Never insert into the syscall enums** (`gameImport_t` in
  `code/game/g_public.h`, and the cgame/ui equivalents). QVMs call syscalls by
  number (`-1 - enum value`); inserting shifts every later number and breaks
  existing mods. New syscalls go at the end, after
  `G_TRAP_GETVALUE = COM_TRAP_GETVALUE` (700).
- Expose new syscalls through `trap_GetValue` as well (`SV_GetValue` in
  `code/server/sv_game.c`, names ending in `_DWC`), so a QVM can probe for them
  and fall back on engines without them.
- New behaviour is off or identical to stock by default (e.g. `bot_travel_exp`
  defaults to 1, the stock travel-time weighting).
- Server-only changes: the servers run the dedicated binary; clients use stock
  Quake3e or any Q3 client (protocol 68).

## DWC-specific features on `dwc`

- `USE_MV`: server-side multiview demo recording (`sv_mv*` cvars, `mvconvert`
  to split into per-player `.dm_68`). q3-servers and dwc-hp rely on the demo
  file names (`mv-<yymmdd>-<hhmmss>.dm_71`, `…-<nr>-<name>.dm_68`).
- `sv_onRestart`: cvar executed after quake3e's automatic 12h restart, so
  configs can re-add bots.
- Bot memory (botlib/`sv_game.c`): syscalls for per-bot goal-weight learning
  used by Hannes' botmem QVM (`bot_memory`, `bot_learn_enable`,
  `bot_learn_use` in the server configs). Learn files are written by the QVM
  to `fs_homepath/osp/bot_memory/`.
