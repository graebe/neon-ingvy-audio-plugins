---
title: Build & CI — Ableton Live
order: 4
slug: ci-live
---

## Locally

```sh
git submodule update --init --recursive   # iPlug2. The engines are subtrees.
scripts/fetch-sdks.sh                     # the VST3 and CLAP SDKs, at pinned versions
npm ci                                    # the kit and every editor
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build                       # the macOS plugins, universal
ctest --test-dir build
```

Artefacts land in `build/out/` and are copied into `~/Library/Audio/Plug-Ins/`.
With `-DIPLUG_DEPLOY_PLUGINS=OFF` they stay in `build/out/`, and `tg_au` and
`sc_au` — which render the *installed* Audio Unit — report **Skipped**.

**iPlug2's SDKs are downloaded rather than tracked.** A fresh clone needs
`scripts/fetch-sdks.sh` before the first configure, or CMake stops on a
non-existent include path in `iPlug2::VST3`. It fetches the VST3 SDK, CLAP and
clap-helpers at the versions pinned in the script; iPlug2's own download
scripts would clone whatever is on `master` today.

**The editors are built, not tracked.** `plugins/*/resources/web` is vite's
output, written at configure and at build time; a configure that finds no
editor stops and names `npm ci`.

**`-DCMAKE_OSX_ARCHITECTURES` is honoured.** Universal is the default, not a
decree: `-DCMAKE_OSX_ARCHITECTURES=arm64` builds one slice.

**`-DNI_SANITIZE=thread`** (or `address`, `undefined`) instruments every C and
C++ test — never a plugin. It needs one slice:

```sh
cmake -B build-tsan -DNI_SANITIZE=thread -DCMAKE_OSX_ARCHITECTURES=arm64 -DIPLUG_DEPLOY_PLUGINS=OFF
cmake --build build-tsan && ctest --test-dir build-tsan
```

**Needs cargo.** If it is installed and not found, the error names where it
looked — `cmake/RustToolchain.cmake` searches every layout rustup.rs, Homebrew
and a bare toolchain use, because Homebrew's keeps its shims in
`/opt/homebrew/opt/rustup/bin`, which is not `~/.cargo/bin`.

## CI

`.github/workflows/ci.yml` runs on pushes to `main`, on pull requests, and by
hand — once per change, with a newer commit cancelling the older run. It runs
on a pinned `macos-15` image: the plugins are macOS bundles and `auval` and the
AU host tests only exist there. Three jobs:

- **build-and-test** fetches the SDKs (cached on the iPlug2 submodule commit
  plus the pins), runs `npm ci`, configures, builds — installing the plugins —
  and runs the suite, `cargo test --workspace` and `npm test`. The AU render
  tests must have *run*: a skip fails the job. It then checks that every bundle
  exists and is genuinely universal.
- **validate** installs those bundles on a fresh runner and runs the hosts'
  own validators through `scripts/validate-plugins.sh`: `auval` on each AU (the
  type, subtype and manufacturer are read from each AU plist), `pluginval` at
  strictness 10 on each VST3 and AU, and `clap-validator` on each CLAP — all
  pinned releases, checked against SHA-256 digests. Steinberg's own VST3
  validator is not built separately: iPlug2's script builds it only through an
  Xcode project whose deployment target current Xcode refuses, and pluginval's
  VST3 checks cover the same ground from a host's side.
- **tsan** builds arm64 with `-DNI_SANITIZE=thread` and runs the suite.

**The suite is the asset here**, and it is not a smoke test:

| | |
|---|---|
| `tg_render_ab` | four seconds through the plugin's audio path, hashed against the Move module's reference render. **The check that a refactor did not change the sound.** |
| `tg_curves`, `tg_envelope` | the editor's envelope maths against the engine's own *measured* output — the engine is run with a DC input at amount 1, where the gain it applies **is** the envelope |
| `ui_tokens` | no colour is spelled outside `ui-kit/src/tokens.css`, and that file agrees with the vendored design system |
| `versions` | every spelling of a product's version agrees with `versions.json`, and every AU plist agrees with its `config.h` |
| `release` | a tag means what the release workflows think it means |
| `licenses` | everything that ships has a notice, and every bundle carries them |
| `spectro_core` | the FFT against a naive DFT, the band mapping, and a counting allocator proving the audio path allocates nothing |

The JS suites skip rather than fail when node is absent: a C++ developer building
the plugin should not need a JS toolchain to run the C tests.

## Releasing

`trance-gate-v2026.09.29.3` — **one product per tag, the prefix says which, and
the rest is the version exactly as `versions.json` spells it.** A bare `v*`
namespace would make a Spectrogram tag publish a Trance Gate, which is the kind
of mistake only ever noticed by whoever downloads the wrong thing. The tag is
parsed by `scripts/release.mjs`, which both release workflows use and
`ctest -R release` tests.

`release-plugins.yml` checks the tag against the tree, builds, runs the suite
again (a tag is the worst possible moment to discover the render A/B moved) and
the validators, stages the three bundles — each already carrying its editor and
`LICENSE`/`THIRD_PARTY_LICENSES.md` in `Contents/Resources/` — and packs them
with `ditto`, which preserves the bundle structure and resource forks, into
`<product>-<version>-macOS.zip` with the notices at its top level too.

Because the asset name carries the version, this site composes its download links
from `versions.json` rather than hard-coding them. That file is already checked
against every other spelling of a version, so a link here cannot point at a
version the tree does not build.

## Signing and notarisation

Gated on six repository secrets, and skipped cleanly without them:

| secret | |
|---|---|
| `MACOS_CERTIFICATE_P12` | base64 of the Developer ID Application certificate and key, as a `.p12` |
| `MACOS_CERTIFICATE_PASSWORD` | that `.p12`'s password |
| `MACOS_SIGNING_IDENTITY` | e.g. `Developer ID Application: Torben Gräber (TEAMID)` |
| `NOTARY_APPLE_ID`, `NOTARY_TEAM_ID`, `NOTARY_PASSWORD` | the Apple ID, team and an app-specific password for `notarytool` |

With them, every staged bundle is signed with `codesign --options runtime
--timestamp` *after* its web resources and notices are inside it (a signature
seals every file in the bundle), submitted with `notarytool --wait`, and
stapled. The certificate lives in a throwaway keychain that is deleted at the
end of the job. Without them the release notes carry the one command that gets
macOS to load an unsigned plugin:

```sh
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/NITranceGate.vst3
```

## Deploying this site

`.github/workflows/pages.yml` builds `site/` and deploys it to GitHub Pages on
every push to `main` that touches documentation, and on every published release
so the download links refresh without a commit.

**One manual step, once:** repository Settings → Pages → Source must be set to
**GitHub Actions**. Until it is, the deploy step fails with a message that does
not obviously say so.
