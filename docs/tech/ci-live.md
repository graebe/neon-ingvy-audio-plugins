---
title: Build & CI — Ableton Live
order: 4
slug: ci-live
---

## Locally

```sh
git submodule update --init --recursive   # iPlug2. The engines are subtrees.
npm ci                                    # the kit and both editors
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build                       # the macOS plugins, universal
ctest --test-dir build
```

Artefacts land in `build/out/` and are copied into `~/Library/Audio/Plug-Ins/`.

**iPlug2's SDKs are downloaded rather than tracked.** A fresh clone needs
`external/iPlug2/Dependencies/IPlug/download-vst3-sdk.sh` and
`download-clap-sdks.sh` before the first configure, or CMake stops on a
non-existent include path in `iPlug2::VST3`.

**Needs cargo.** If it is installed and not found, the error names where it
looked — `cmake/RustToolchain.cmake` searches every layout rustup.rs, Homebrew
and a bare toolchain use, because Homebrew's keeps its shims in
`/opt/homebrew/opt/rustup/bin`, which is not `~/.cargo/bin`.

## CI

`.github/workflows/ci.yml` runs on every push and every pull request, on macOS —
the plugins are macOS bundles and `auval` and the AU host test only exist there.

It installs both Apple Rust targets, runs `npm ci` once for the whole workspace,
configures, builds, and runs the suite. Then it checks something the suite
cannot: that the bundles **exist** and are genuinely universal, `lipo`-ing each
of the six for `arm64` and `x86_64`. A build that produced no bundle passes every
test above it and ships nothing.

**The suite is the asset here**, and it is not a smoke test:

| | |
|---|---|
| `tg_render_ab` | four seconds through the plugin's audio path, hashed against the Move module's reference render. **The check that a refactor did not change the sound.** |
| `tg_curves`, `tg_envelope` | the editor's envelope maths against the engine's own *measured* output — the engine is run with a DC input at amount 1, where the gain it applies **is** the envelope |
| `ui_tokens` | no colour is spelled outside `ui-kit/src/tokens.css`, and that file agrees with the vendored design system |
| `versions` | every spelling of a product's version agrees with `versions.json` |
| `spectro_core` | the FFT against a naive DFT, the band mapping, and a counting allocator proving the audio path allocates nothing |

The JS suites skip rather than fail when node is absent: a C++ developer building
the plugin should not need a JS toolchain to run the C tests.

## Releasing

`trance-gate-v1.0.0` or `spectrogram-v0.1.0` — **one product per tag, and the
prefix says which.** A bare `v*` namespace would make a Spectrogram tag publish a
Trance Gate, which is the kind of mistake only ever noticed by whoever downloads
the wrong thing.

The workflow checks the tag against `versions.json`, builds, runs the suite again
(a tag is the worst possible moment to discover the render A/B moved), then packs
the three bundles with `ditto` rather than `zip` — `ditto` preserves the bundle
structure and the resource forks a macOS plug-in carries — and attaches
`<product>-<version>-macOS.zip`.

Because the asset name carries the version, this site composes its download links
from `versions.json` rather than hard-coding them. That file is already checked
against every other spelling of a version, so a link here cannot point at a
version the tree does not build.

## The bundles are unsigned

That is a decision rather than an oversight: signing needs an Apple Developer ID,
four repository secrets and a notarytool round trip. Until those exist, the
release notes carry the one command that gets macOS to load an unsigned plugin:

```sh
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/TranceGate.vst3
```

Adding signing later changes that workflow and nothing about the build.

## Deploying this site

`.github/workflows/pages.yml` builds `site/` and deploys it to GitHub Pages on
every push to `main` that touches documentation, and on every published release
so the download links refresh without a commit.

**One manual step, once:** repository Settings → Pages → Source must be set to
**GitHub Actions**. Until it is, the deploy step fails with a message that does
not obviously say so.
