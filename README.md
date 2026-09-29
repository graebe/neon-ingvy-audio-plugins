# neon-ingvy-audio-plugins

Audio plugins and a Schwung module, built from shared Rust engines and one
Solid UI kit. A monorepo: everything that ships from here is in here.

| product | ships as | engine |
|---|---|---|
| [Trance Gate](plugins/trance-gate/README.md) | VST3 · AU · CLAP · a Schwung module for the Move | `engines/trance-gate` |
| [Spectrogram](plugins/spectrogram/README.md) | VST3 · AU · CLAP | `engines/spectro` |

## How it is put together

**One core per product, and the wrappers around it are the only thing that
differs.** The Trance Gate's DSP is a single Rust crate; `tg-capi` wraps it in
a C ABI for the plugin and `tg-move` wraps it in Schwung's audio_fx vtable for
the Move. Both are members of one Cargo workspace and both reach `tg-core` by
relative path, so they cannot drift apart — not by policy, by construction.
That claim is also *tested*: `tests/render_plugin.c` renders four seconds
through the plugin's own audio path and the result is byte-for-byte identical
to the Move module's reference render.

The two engines never depend on each other. A crate belongs to exactly one
product.

```
engines/<product>/crates     the core, and its wrappers
plugins/<product>/           the VST3/AU/CLAP shell, and its editor
modules/<product>/           the Schwung module's shell and packaging
ui-kit/                      @ultraviolet/ui — tokens, controls, the iPlug2 bridge
site/                        the documentation site, from this repo's own Markdown
docs/tech/                   how it is built, in prose
design/files/                the Ultraviolet design system, vendored
versions.json                one version per product
```

## Build

```sh
git submodule update --init --recursive   # iPlug2. The engines are subtrees.
npm ci                                    # the kit and both editors
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build                       # the macOS plugins, universal
ctest --test-dir build                    # 14 tests
```

The Move module is the second target, and it is a Linux cross-build in Docker:

```sh
cmake --build build --target schwung       # -> dist/trance-gate-module.tar.gz
```

Artefacts land in `build/out/` and are copied into `~/Library/Audio/Plug-Ins/`.

**Needs cargo.** If it is installed and not found, the error names where it
looked; `cmake/RustToolchain.cmake` searches every layout rustup.rs, Homebrew
and a bare toolchain use. Homebrew's keeps its shims in
`/opt/homebrew/opt/rustup/bin`, which is not `~/.cargo/bin`.

**iPlug2's SDKs are downloaded rather than tracked.** A fresh clone needs
`external/iPlug2/Dependencies/IPlug/download-vst3-sdk.sh` and
`download-clap-sdks.sh` before the first configure, or CMake stops on a
non-existent include path in `iPlug2::VST3`.

## What the tests are for

Most of them are not smoke tests, and the repository leans on them hard:

| | |
|---|---|
| `tg_render_ab` | four seconds through the plugin's audio path, hashed against the Move module's reference render. **The check that a refactor did not change the sound.** |
| `tg_curves`, `tg_envelope` | the editor's envelope maths against the engine's own *measured* output — the engine is run with a DC input at amount 1, where the gain it applies IS the envelope |
| `ui_tokens` | no colour is spelled outside `ui-kit/src/tokens.css`, and that file agrees with the vendored design system |
| `versions` | every spelling of a product's version agrees with `versions.json` |
| `spectro_core` | the FFT against a naive DFT, the band mapping, and a counting allocator proving the audio path allocates nothing |

## The products

Each product's manual lives with it, and this site renders those same files:

| | |
|---|---|
| [Trance Gate](plugins/trance-gate/README.md) | a tempo-locked step gate — [in Live](plugins/trance-gate/docs/live.md), [on the Move](plugins/trance-gate/docs/schwung.md) |
| [Spectrogram](plugins/spectrogram/README.md) | a rolling STFT analyzer — [in Live](plugins/spectrogram/docs/live.md) |

Published at **https://graebe.github.io/neon-ingvy-audio-plugins/**, built from this
repository's own Markdown — see [site/README.md](site/README.md).

## Licence

**MIT**, © 2026 Torben Gräber — every part of it, with nothing copyleft in the
chain.

| | |
|---|---|
| this repository | **MIT** |
| [iPlug2](https://github.com/iPlug2/iPlug2) | **zlib**, with WDL/NanoVG/NanoSVG (Zlib) and MetalNanoVG/RTAudio (MIT) |
| VST3 SDK | **MIT**, © 2026 Steinberg Media Technologies GmbH |
| CLAP | **MIT** |
| the Trance Gate engine (`engines/trance-gate`) | **MIT**, and it has no external crates at all |
| the Spectrogram analyzer (`engines/spectro`) | **MIT**, and it has none either — the FFT is ninety lines rather than a crate |
| JetBrains Mono, bundled with both editors | **SIL OFL 1.1**, with `OFL.txt` beside the font in every bundle |

Two things had to go to get here, and neither was a licensing decision on its
own.

**JUCE 8 is AGPLv3-or-commercial** — a *stronger* obligation than GPL, not an
equal one: while that target shipped, the artefact had to be conveyed under
AGPLv3. It is gone, and so is the 2,586-line editor it drew. That editor is
not lost, it is the last commit before the removal, and whatever draws the UI
next is a translation of it rather than a fresh design.

**nih-plug is ISC, but `nih_export_vst3!()` is not.** It pulled in
[`vst3-sys`](https://github.com/RustAudio/vst3-sys), GPL-3.0-or-later — a
third-party reimplementation of interfaces Steinberg now publishes under MIT
themselves. One crate, and it made the whole build copyleft.

The premise behind both was that a VST3 plugin cannot be permissive. It can:
Steinberg withdrew the GPL-or-proprietary dual licence and the SDK is MIT.
That was worth checking rather than assuming, and checking it is what made
this repository MIT.

See [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) for the notices those
dependencies require.

