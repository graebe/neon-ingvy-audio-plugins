---
title: Build & CI — Ableton Live
order: 4
slug: ci-live
---

## Locally

Everything is built and tested on the developer's machine first; GitHub Actions
only publish. The presets in `CMakePresets.json` are the builds:

| preset | directory | what |
|---|---|---|
| `dev` | `build-dev/` | this machine's architecture, RelWithDebInfo, no LTO: the loop to iterate in |
| `release` | `build/` | Release with LTO, universal on macOS: what ships |
| `coverage` | `build-coverage/` | instrumented for llvm-cov, never shipped |

**macOS**, natively:

```sh
git submodule update --init --recursive   # JUCE 9. The engines are subtrees.
cmake --preset release                    # universal, LTO: what ships (build/)
cmake --build build                       # the five VST3s, into build/out
scripts/test.sh quick                     # the developer loop (build-dev/)
scripts/test.sh full                      # everything, the validators included
```

**Linux and Windows**, from the same Mac in Docker, with the cross-build kit
(see **Cross-platform builds**):

```sh
scripts/build-linux.sh . -- -L full -LE 'move|site' -E '^cargo_deny$'
XWIN_ACCEPT_LICENSE=yes scripts/build-windows.sh . -- -L quick
scripts/build-all.sh . -- -L full -LE 'move|site' -E '^cargo_deny$'   # all three
```

On a Windows or Linux machine the same presets build natively:
`cmake --preset release` and `cmake --build build`, on Windows from a Visual
Studio developer prompt (Ninja needs the MSVC environment).

Artefacts land in `build/out/`, where every test and validator loads them by
path. No build copies a bundle into the system's VST3 folder unless it is
configured with `-DNI_DEPLOY_PLUGINS=ON`, so a build never replaces what a host
has installed.

**`-DNI_SANITIZE=thread`** (or `address`, `undefined`) instruments every C and
C++ test — never a plugin. It needs one slice:

```sh
cmake -B build-tsan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DNI_SANITIZE=thread -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build-tsan && ctest --test-dir build-tsan -L full -LE host
```

**Needs cargo.** If it is installed and not found, the error names where it
looked — `cmake/RustToolchain.cmake` searches every layout rustup.rs, Homebrew
and a bare toolchain use, because Homebrew's keeps its shims in
`/opt/homebrew/opt/rustup/bin`, which is not `~/.cargo/bin`.

## CI

`.github/workflows/ci.yml` runs **by hand only**. Every change is built and
tested locally before it is pushed (`scripts/test.sh quick` and `full`); that
is the gate, and Actions publish rather than discover. Started by hand, it
checks a clean runner on a pinned `macos-15` image, with a newer run cancelling
the older one. Three jobs: **quick** and **full** run `scripts/test.sh` in each
tier, and **tsan** builds the tests under ThreadSanitizer and runs them.

**The suite is the asset here**, and it is not a smoke test:

| | |
|---|---|
| `tg_render_ab` | four seconds through the plugin's audio path, hashed against the Move module's reference render. **The check that a refactor did not change the sound.** |
| `tg_curves`, `tg_envelope` | the editor's envelope maths against the engine's own *measured* output — the engine is run with a DC input at amount 1, where the gain it applies **is** the envelope |
| `ui_tokens_native` | the kit's generated tokens agree with the vendored design system, and no colour is spelled anywhere else |
| `versions` | every spelling of a product's version agrees with `versions.json`: the build, the bundle's plist and moduleinfo.json, module.json and every crate |
| `release` | a tag means what the release workflows think it means |
| `licenses` | everything that ships has a notice, and every bundle carries them |
| `spectro_core` | the FFT against a naive DFT, the band mapping, and `assert_no_alloc`'s guard proving the audio path allocates nothing |

The JS suites skip rather than fail when node is absent: a C++ developer building
the plugin should not need a JS toolchain to run the C tests.

## Releasing

`trance-gate-v2026.09.29.3` — **one product per tag, the prefix says which, and
the rest is the version exactly as `versions.json` spells it.** A bare `v*`
namespace would make a Spectrogram tag publish a Trance Gate, which is the kind
of mistake only ever noticed by whoever downloads the wrong thing. The tag is
parsed by `scripts/release.mjs`, which both release workflows use and
`ctest -R release` tests.

`release-plugins.yml` checks the tag against the tree and builds that one
product's VST3 natively on macOS, Windows and Linux. Each runs the suite again
(a tag is the worst possible moment to discover the render A/B moved: the full
tier on macOS, the quick tier and the hosted bundles on the other two), then
the validators — pluginval at strictness 10 with its editor tests, Steinberg's
VST3 validator and, on macOS, `codesign --verify --deep --strict` — stages the
bundle with `LICENSE` and `THIRD_PARTY_LICENSES.md` beside it, and packs one
zip per OS: `<product>-<version>-macOS.zip`, `-Windows.zip` and `-Linux.zip`.

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

With them, the staged macOS bundle is signed with `codesign --options runtime
--timestamp` *after* its notices are inside it (a signature seals every file in
the bundle), submitted with `notarytool --wait`, and stapled. The certificate lives in a throwaway keychain that is deleted at the
end of the job. Without them the bundle ships with the ad hoc signature the
build gave it, and the release notes carry the one command that gets macOS to
load it:

```sh
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/NITranceGate.vst3
```

## Deploying this site

`.github/workflows/pages.yml` builds `site/` and deploys it to GitHub Pages
when a release is published, and by hand. Never on a push: the site's download
links name release tags, and a push to `main` that bumps `versions.json` would
otherwise link a version that has not been released yet.

**One manual step, once:** repository Settings → Pages → Source must be set to
**GitHub Actions**. Until it is, the deploy step fails with a message that does
not obviously say so.
