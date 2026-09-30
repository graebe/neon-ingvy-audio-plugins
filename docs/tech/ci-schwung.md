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
what reaches a device has no repository near it, and MIT asks that the notice
travel with every copy.

`modules/_shared/install.sh <module>` scps the result to `ableton@move.local`.
It refuses to create the base directory if it is not already there — a wrong
path silently creating a tree is worse than an error — and leaves the module
owner-writable but never world-writable.

## Releasing

A tag `<product>-<version>` for a product with a module — `trance-gate-v*` or
`side-chain-v*` — triggers `.github/workflows/release-schwung.yml`, one workflow
for every module. The same tag also triggers the plugin release, because the
module and the plugin are one product in two shells — one tag ships both, and
their versions cannot drift apart.

The workflow, in order:

1. **The tag matches the source.** `scripts/release.mjs resolve` checks it
   against `versions.json` and the module's `module.json`. Since
   `ctest -R versions` already holds every other spelling to `versions.json`,
   checking those two checks all of them. (The previous inline check stripped
   the version's own `v` along with the product prefix, so no correct tag
   could pass it; `ctest -R release` now runs the parser against every
   product's current tag.)
2. Build in Docker with `modules/_shared/package.sh <module>`, exactly as
   locally.
3. **Verify the artifact.** It must exist, must contain `<id>/<id>.so`,
   `module.json`, `LICENSE` and `THIRD_PARTY_LICENSES.md`, and `file` must report
   the `.so` as ARM aarch64. The release action *warns* on a missing file
   rather than failing, so a build that produced nothing would otherwise
   publish an empty release the catalog happily points at.
4. Attach the tarball. `prerelease` is set explicitly and never inferred:
   without that, a `-beta.` tag publishes as a normal release and GitHub shows it
   as **Latest**.
5. **Record it in `release.json` on `main`**, with
   `scripts/release.mjs release-json`, run from the tagged tree onto main's
   current file.

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

## Outstanding: the catalog still names the old repository

The Trance Gate module used to live in its own repository, and the Schwung
catalog entry in `charlesvestal/schwung` still points there. Until that entry's
`github_repo` is updated to `graebe/neon-ingvy-audio-plugins` and a `trance-gate-v*` tag has
published a release here, installing by catalog resolves against the old
repository.

`release.json` in this repository has already been repointed, so the two are
briefly out of step. That is stated here rather than left to be discovered: the
fix is a one-line upstream pull request plus the first tag from this repository.
