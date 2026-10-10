---
title: Build & CI — Schwung
order: 3
slug: ci-schwung
---

The Move module is a Linux cross-build. It is **not** built by the CI workflow —
that would double the CI time for a target whose own release workflow already
builds and verifies it on every tag.

## Locally

```sh
cmake --build build --target schwung              # -> dist/trance-gate-module.tar.gz
cmake --build build --target schwung-side-chain   # -> dist/ni-side-chain-module.tar.gz
```

Every module is built by the same script, `modules/_shared/package.sh <module>`,
where `<module>` is the directory under `modules/`. What differs between modules
— the catalog id, the title and the `*-move` crate — is in that directory's
`module.env`; `tests/release.test.mjs` holds it to `module.json` and the Cargo
workspace.

The script re-execs itself inside Docker unless it is already in a container.
The image (`modules/_shared/Dockerfile`) is Ubuntu 22.04 with
`gcc-aarch64-linux-gnu` and a pinned Rust toolchain targeting
`aarch64-unknown-linux-gnu`, installed by a rustup-init that is pinned by
version and verified by SHA-256 — never `curl | sh`. The C toolchain is still
there because the aarch64 linker is gcc's.

Inside, it runs `cargo build --release -p <crate> --target aarch64-unknown-linux-gnu`
**from the repository root**, so cargo walks up to the one workspace and picks
up `.cargo/config.toml` — which sets `target-cpu=cortex-a72` for that target and
nothing else.

Two things in that script are load-bearing and commented as such:

- **The `.so` filename matters.** For a `component_type: audio_fx` module the
  chain host builds the path `modules/audio_fx/<id>/<id>.so` itself and never
  reads `module.json`'s `dsp` field.
- **`-Ofast` is gone permanently.** Clang's FMA contraction made the shipped
  `.so` and its tests non-bit-identical, which is exactly the failure the render
  A/B exists to catch.

The tarball carries `LICENSE` and `THIRD_PARTY_LICENSES.md` beside the `.so`:
what reaches a device has no repository near it, and the GPL asks that every
copy come with the licence.

## Testing on the Move's architecture

```sh
./modules/_shared/test.sh trance-gate    # or: ctest --test-dir build -L move
./modules/_shared/test.sh side-chain
```

First, on the Mac side, `test.sh` verifies the pinned Schwung and holds the
vendored host headers to it (`schwung.sh headers`, **Host compatibility**
below) — there, because the image has no git, and as a failure rather than a
skip, because this is the final verification. Then it builds the module with
`package.sh` in the same image and tests it there, on aarch64 Linux — Docker
on Apple Silicon runs the image as arm64; on an x86_64 host the script stops
rather than test the wrong architecture:

- **The tarball as a device receives it.** The `.so` is aarch64, exports
  `move_audio_fx_init_v2` and the engine's C ABI and nothing of the plugin's
  (`tg_shell_*`, `sc_shell_*`, `gnd_*`: the capi crates' `shell` feature is off
  for the Move); `module.json` says GPL-3.0-or-later and the `ui_chain` it
  names is in the tarball; `THIRD_PARTY_LICENSES.md` has a notice for every
  crates.io crate `cargo tree` says the `.so` links.
- **The module's crates** (`MODULE_TEST_CRATES` in `module.env`: the core and
  the vtable crate) under `cargo test`: the `chain_params` contract — every
  engine parameter declared, or deliberately absent and named as such — the
  host panic, CC 120 or CC 123, which resets the Side-Chain and which the
  Trance Gate accepts without changing a sample.
- **The module's test script** (`MODULE_TEST_SCRIPT`), the Trance Gate's
  `engines/trance-gate/tests/run.sh`: its C suites against the engine's ABI,
  and the golden render's md5.

The full tier runs it for every module (`ctest -L move`), one at a time, since
both build into `target/aarch64-unknown-linux-gnu/`.

`modules/_shared/install.sh <module>` scps the result to `ableton@move.local`.
It refuses to create the base directory if it is not already there — a wrong
path silently creating a tree is worse than an error — and leaves the module
owner-writable but never world-writable.

## Host compatibility

### The pinned Schwung

Every module is tested against **one** Schwung release, named once in
`modules/_shared/schwung.version`:

```sh
SCHWUNG_REPO=charlesvestal/schwung
SCHWUNG_TAG=v1.7.3
SCHWUNG_COMMIT=a6f7ec9e5cf4d0df172a1dbc00b29d0b5eb67124
```

The format is deliberately trivial, for scripts and CI to read: `KEY=VALUE`
lines, no quotes or spaces, `#` comment lines — split on the first `=`, or
source it from a POSIX shell.

`modules/_shared/schwung.sh` is the only way in:

```sh
modules/_shared/schwung.sh checkout   # prints the verified checkout's path
modules/_shared/schwung.sh headers    # vendored headers == the pin's, byte for byte
```

It fetches the tag once (`git fetch --depth 1`) into a cache outside every
checkout — `~/Library/Caches/neon-ingvy/schwung/<tag>-<commit>` on macOS,
`$XDG_CACHE_HOME/neon-ingvy/schwung/…` elsewhere, `NI_SCHWUNG_CACHE` to
override — and **refuses it unless the tag still names the pinned commit**. A
cached copy is verified locally on every use (HEAD is the commit, `git status`
is clean), so it works offline once fetched. `NI_SCHWUNG_REMOTE` fetches from a
mirror or a local clone instead; the commit check is the same, so it can supply
the pinned Schwung but never another. Exit 1 is a mismatch, 77 (ctest's skip)
is "not cached and not fetchable".

The SHA is upstream's, and that matters: `v1.7.3` was re-tagged after a
history rewrite, so a clone fetched before then has `v1.7.3` at `911136d`, the
same tree with another parent. The pin is what GitHub serves now, and a local
clone with the old tag is refused as a source.

Who uses it: `engines/trance-gate/tests/ui.sh` (the editor's smoke test, on the
pin's `src/shared`), `ctest -R schwung_pin` (the headers), and `test.sh` on the
Mac side before the container. They used to search for any `schwung/` checkout
above the repository and test against whatever version it was on.

To move the pin: change all three keys, run `schwung.sh headers` and vendor
what it reports, then check the offsets in ni-schwung's
`the_host_table_matches_the_c_layout` against the new headers. They are
copied from `plugin_api_v1.h` by hand, so nothing compares them for you.

## Releasing

A tag `<product>-<version>` for a product with a module — `trance-gate-v*` or
`side-chain-v*` — triggers `.github/workflows/release-schwung.yml`, one workflow
for every module. The same tag also triggers the plugin release, because the
module and the plugin are one product in two shells — one tag ships both, and
their versions cannot drift apart.

The workflow, in four jobs:

1. **`check`, before anything is built** (ubuntu-24.04):
   - **The tag matches the source.** `scripts/release.mjs resolve` checks it
     against `versions.json` and the module's `module.json`. Since
     `ctest -R versions` already holds every other spelling to
     `versions.json`, checking those two checks all of them. (The previous
     inline check stripped the version's own `v` along with the product
     prefix, so no correct tag could pass it; `ctest -R release` now runs the
     parser against every product's current tag.) A beta Schwung Manager
     would never offer is refused here (**Betas**, below).
   - **The commit was tested locally**: a passing `scripts/test.sh product
     <product>` or `full` recorded on it under `refs/notes/ni-tests` and pushed
     before the tag ([Build & CI — Live](ci-live.md), **The order of a
     release**).
   - **The supply chain**: `cargo deny check advisories licenses bans
     sources` — the licence gate every local tier runs, and RustSec's
     advisories as of the release day.
2. **`test`, on the Move's architecture** (ubuntu-24.04-arm):
   `modules/_shared/test.sh <module>` builds the tarball with `package.sh` in
   the module image and runs every check in **Testing on the Move's
   architecture** on it, as `ctest -L move` does on a Mac. A module that ships
   a `ui_chain.js` then has its editor test (`engines/<product>/tests/ui.sh`,
   `tg_move_ui`) run against Schwung's shared modules from the release pinned
   in `modules/_shared/schwung.version`, which `modules/_shared/schwung.sh`
   fetches and verifies by commit; on the runner a skip is a failure.
   The tarball it built and tested is the job's artifact.
3. **`release`** (ubuntu-24.04), with **that** tarball — never one built again:
   - **Verify the artifact.** It must exist, must contain `<id>/<id>.so`,
     `module.json`, `LICENSE` and `THIRD_PARTY_LICENSES.md`, and `file` must
     report the `.so` as ARM aarch64. The release action *warns* on a missing
     file rather than failing, so a build that produced nothing would
     otherwise publish an empty release the catalog happily points at.
   - Attach the tarball. `prerelease` is set explicitly and never inferred:
     without that, a `-beta.` tag publishes as a normal release and GitHub
     shows it as **Latest**.
   - **Record it in `release.json` on `main`**, with
     `scripts/release.mjs release-json`, run from the tagged tree onto main's
     current file.
4. **`pages`**: the site, rebuilt from `main` now that its `release.json`
   names this release ([Build & CI — Live](ci-live.md), **Deploying this
   site**).

The image the module is built in is pinned twice over: its base by digest
(`ubuntu:22.04@sha256:…`), and its Rust by `rust-toolchain.toml`'s version,
which the Dockerfile installs and sets as `RUSTUP_TOOLCHAIN`, so the plugin and
the module of one product are built by one compiler.

### `release.json`

One repository publishes two catalog modules, so the file uses Schwung's
multi-module shape, keyed by each module's catalog id (`module.json`'s `id`):

```json
{
  "version": "…", "download_url": "…", "channels": { … },
  "modules": {
    "trance-gate":   { "version": "…", "download_url": "…", "channels": { "stable": { … } } },
    "ni-side-chain": { "version": "…", "download_url": "…", "channels": { "stable": { … } } }
  }
}
```

The top-level fields mirror the Trance Gate, for managers and catalog entries
that predate the `modules` map. A `-beta.` version goes to that module's
`channels.beta` and touches nothing else; anything else is stable and also
moves the entry's own `version`/`download_url`.

**Versions here have no leading `v`** — `2026.09.29.3`, and the same in
`module.json`. Schwung Manager compares versions with `parseInt` on each dotted
part, and `parseInt("v2026")` is `NaN`, read as 0: with the `v`, the year would
be ignored and a January release would sort before December's.

### Betas

A beta, `v2026.10.11.1-beta.2`, is `2026.10.11.1-beta.2` in `module.json` and
`release.json`, and goes to the module's `channels.beta`. Schwung Manager
(`schwung-manager/module_channel.go`, `versionNewer`) offers it to a device
on the beta channel only when it is newer than the stable entry, and its order
is right for this spelling with one exception:

| | the manager says | |
|---|---|---|
| `2026.10.11.1-beta.1` vs stable `2026.10.10.3` | newer | the date decides |
| `2026.10.10.1` vs `2026.10.10.1-beta.2` | newer | the release replaces its beta |
| `2026.10.10.1-beta.2` vs `2026.10.10.1-beta.1` | newer | |
| `2026.10.10.2-beta.1` vs stable `2026.10.10.1` | **older** | Go's `Atoi("2-beta")` is 0 |

So a beta of a later subversion **on the stable's own day** would be
published and offered to nobody. `scripts/release.mjs resolve` refuses that
tag before anything is built, and says to release it under the next day's
date or as stable. `tests/release.test.mjs` holds a port of the manager's
compare to every row above.

The module store on the device (`src/shared/store_utils.mjs`) reads only the
stable entry, and compares with `parseInt` per dotted part, which reads
`1-beta` as 1: a device that installed a beta through the manager is not
offered the release that replaces it by the on-device store — the manager's
own pages offer it.

## Rolling back

The catalog is `release.json` on `main`, so rolling a module back is a commit
there; the release on GitHub can stay, or be withdrawn as the plugin's is
([Build & CI — Live](ci-live.md), **Rolling back**).

- **A stable release**: set the module's entry in `modules` — its `version`,
  `download_url` and `channels.stable` — back to the previous release, and for
  the Trance Gate the top-level mirror too (`ctest -R versions` fails if the
  two disagree). A device that installed the withdrawn version is not
  downgraded — the manager offers only newer versions — so the fix forward,
  a new subversion, is what reaches it.
- **A beta**: remove the module's `channels.beta`, or point it at the
  previous beta. Beta users then resolve to stable.
- Commit (`Schwung catalog: revert <id> <version>`), push to `main`, and run
  **Pages** by hand so the site's link follows.

`ctest -R versions` checks every entry is one the manager can use and is not
ahead of `versions.json`; a rollback only ever moves an entry backwards, so it
passes.

## Protecting `main` (the owner's step, not yet taken)

`main` is not protected, and the release records each module in
`release.json` by pushing to it as `github-actions[bot]` with the workflow's
own token, beside the agent sessions that also push there. When it is
protected, the protection must still admit that one push, or every module
release publishes its tarball and then fails to record it (GH006; the step
prints what to do by hand). In the repository's Settings → Rules → Rulesets, a
ruleset on the default branch:

- **block force pushes** and **restrict deletions** — these stop nothing the
  workflow does;
- if it also **requires a pull request**, add the **GitHub Actions** app to
  the ruleset's bypass list, or the push is rejected. The alternative is for
  the step to open a pull request instead of pushing, which is a workflow
  change, not a setting.

## Outstanding: the catalog still names the old repository

The Trance Gate module used to live in its own repository, and the Schwung
catalog entry in `charlesvestal/schwung` still points there. Until that entry's
`github_repo` is updated to `graebe/neon-ingvy-audio-plugins` and a `trance-gate-v*` tag has
published a release here, installing by catalog resolves against the old
repository.

`release.json` in this repository has already been repointed, so the two are
briefly out of step. That is stated here rather than left to be discovered: the
fix is a one-line upstream pull request plus the first tag from this repository.
