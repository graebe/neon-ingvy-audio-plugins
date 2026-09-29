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
cmake --build build --target schwung     # -> dist/trance-gate-module.tar.gz
```

Behind that target, `modules/trance-gate/package.sh` re-execs itself inside
Docker unless it is already in a container. The image is Ubuntu 22.04 with
`gcc-aarch64-linux-gnu` and a pinned rustup toolchain targeting
`aarch64-unknown-linux-gnu`; the C toolchain is still there because the aarch64
linker is gcc's.

Inside, it runs `cargo build --release -p tg-move --target aarch64-unknown-linux-gnu`
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

`modules/trance-gate/install.sh` scps the result to `ableton@move.local`. It
refuses to create the base directory if it is not already there — a wrong path
silently creating a tree is worse than an error.

## Releasing

A tag named `trance-gate-v*` triggers `.github/workflows/release-schwung.yml`.
The same tag also triggers the plugin release, because the module and the plugin
are one product in two shells — one tag ships both, and their versions cannot
drift apart.

The workflow, in order:

1. **The tag matches the source.** It is checked against
   `modules/trance-gate/module.json` *and* against `versions.json`. Since
   `ctest -R versions` already holds every other spelling to `versions.json`,
   checking those two checks all of them, and a release cannot claim a version
   the tree does not build.
2. Build in Docker, exactly as locally.
3. **Verify the artifact.** It must exist, must contain `trance-gate/trance-gate.so`,
   and `file` must report it as ARM aarch64. This step is the one addition over
   the house pattern, and it earns its place: the release action *warns* on a
   missing file rather than failing, so a build that produced nothing would
   publish an empty release the catalog happily points at — a 404 on install,
   reported as "the module is broken" rather than as a release that was never
   built.
4. Attach the tarball. `prerelease` is set explicitly and never inferred: without
   that, a `-beta.` tag publishes as a normal release and GitHub shows it as
   **Latest**, so the build that needs an unreleased host becomes the one every
   visitor is pointed at.
5. **Rewrite `release.json` on `main`.** A `-beta.` version routes to
   `channels.beta` and touches nothing else; anything else is stable and also
   updates the top-level `version` and `download_url`, because a manager that
   predates channels reads only those and would otherwise be pinned forever.

## How a Move finds the module

The Schwung catalog carries **no version**. Its entry names the repository, the
asset, and a `min_host_version`; the manager resolves the actual download at
install time from `release.json` on the default branch. So `release.json` is the
contract, the release workflow is the only thing that writes it, and this site
reads the same file — which is why the version shown here and the version a
device installs cannot disagree.

A beta is offered only when it is strictly newer than stable, which keeps beta
users from being stranded on a channel that has fallen behind.

## Outstanding: the catalog still names the old repository

The Trance Gate module used to live in its own repository, and the Schwung
catalog entry in `charlesvestal/schwung` still points there. Until that entry's
`github_repo` is updated to `graebe/vst-library` and a `trance-gate-v*` tag has
published a release here, installing by catalog resolves against the old
repository.

`release.json` in this repository has already been repointed, so the two are
briefly out of step. That is stated here rather than left to be discovered: the
fix is a one-line upstream pull request plus the first tag from this repository.
