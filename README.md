# neon-ingvy-audio-plugins

Audio plugins and a Schwung module, built from shared Rust engines and one
Solid UI kit. A monorepo: everything that ships from here is in here.

| product | ships as | engine |
|---|---|---|
| [NI Trance Gate](plugins/trance-gate/README.md) | VST3 · a Schwung module for the Move | `engines/trance-gate` |
| [NI Spectrogram](plugins/spectrogram/README.md) | VST3 | `engines/spectro` |
| [NI Listen-In](plugins/listen-in/README.md) | VST3 · AU · CLAP | `engines/audio-bus` |
| [NI Side-Chain](plugins/side-chain/README.md) | VST3 · a Schwung module for the Move | `engines/side-chain` |

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
plugins/<product>/           the VST3/AU/CLAP shell, its web editor and its native one
plugins/_shared/ui/          the native Ultraviolet kit (JUCE); ui/ builds it with the editors
modules/<product>/           a Schwung module: module.json, module.env, its UI
modules/_shared/             the one Dockerfile, package.sh and install.sh for all of them
ui-kit/                      @ultraviolet/ui — tokens, controls, the iPlug2 bridge
site/                        the documentation site, from this repo's own Markdown
docs/tech/                   how it is built, in prose
spike/                       the JUCE class-ID spike; tests/fixtures/iplug2 its contract
tools/docker/, tools/cross/  the cross-build kit: Linux and Windows build images, the JUCE smoke plugin
design/scheme/               the Ultraviolet design system, vendored
design/designs/              the "NI Plugin Layouts" canvas, mirrored
versions.json                one version per product
```

## Build

```sh
git submodule update --init --recursive   # iPlug2 and JUCE. The engines are subtrees.
scripts/fetch-sdks.sh                     # the VST3 and CLAP SDKs, at pinned versions
npm ci                                    # the kit and every editor
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build                       # the macOS plugins, universal
scripts/test.sh quick                     # the developer loop: seconds, no bundles
scripts/test.sh full                      # everything: bundles, hosts, browser, coverage
```

The Move modules are further targets, each a Linux cross-build in Docker:

```sh
cmake --build build --target schwung              # -> dist/trance-gate-module.tar.gz
cmake --build build --target schwung-side-chain   # -> dist/ni-side-chain-module.tar.gz
```

Artefacts land in `build/out/` and are copied into `~/Library/Audio/Plug-Ins/`
(`-DIPLUG_DEPLOY_PLUGINS=OFF` keeps them in `build/out/`). The AU tests load
`build/out`'s bundles by path, never the installed ones, so they run the same
either way; a missing bundle fails them, it never skips.
Each bundle carries `LICENSE` and `THIRD_PARTY_LICENSES.md` in
`Contents/Resources/`.

**The editors are built, not checked in.** `plugins/*/resources/web` is vite's
output: configuring runs it, building re-runs it, and a configure that finds no
editor stops and says to run `npm ci`.

**Needs cargo.** If it is installed and not found, the error names where it
looked; `cmake/RustToolchain.cmake` searches every layout rustup.rs, Homebrew
and a bare toolchain use. Homebrew's keeps its shims in
`/opt/homebrew/opt/rustup/bin`, which is not `~/.cargo/bin`.

**iPlug2's SDKs are downloaded rather than tracked.** A fresh clone needs
`scripts/fetch-sdks.sh` before the first configure, or CMake stops on a
non-existent include path in `iPlug2::VST3`. It fetches the VST3 SDK, CLAP and
clap-helpers at the versions pinned in the script — iPlug2's own download
scripts default to whatever is on `master` today — and `--verify` checks a
tree that already has them.

**Linux and Windows are built locally too**, in Docker: `scripts/build-all.sh`
builds a JUCE plugin project for macOS, Linux and Windows in turn, runs its
tests and validates every VST3 with pluginval
([docs/tech/cross-build.md](docs/tech/cross-build.md)). It is proven on
`tools/cross/smoke`; the plugins join it with their move to JUCE.

## What the tests are for

**Two tiers** ([docs/tech/testing.md](docs/tech/testing.md)). `scripts/test.sh
quick` builds only the test programs and runs `ctest -L quick`: every crate's
unit tests, the C/C++ wire, state and parameter tests, the oracles, all the
JavaScript and the lint-like checks — a few seconds warm, no bundle, host or
browser. `scripts/test.sh full` adds the render goldens, the AU host renders,
the bus across processes and architectures, the Playwright end-to-end suite,
coverage with its floor and the validators. Every test carries a ctest label
saying which tier it is in (`cmake/NiTest.cmake`); CI runs quick on every push
and pull request, and full on `main` and on demand.

Most of them are not smoke tests, and the repository leans on them hard:

| | |
|---|---|
| `tg_render_ab` | four seconds through the plugin's audio path, hashed against the Move module's reference render. **The check that a refactor did not change the sound.** |
| `tg_curves`, `tg_envelope` | the editor's envelope maths against the engine's own *measured* output — the engine is run with a DC input at amount 1, where the gain it applies IS the envelope |
| `ui_tokens` | no colour is spelled outside `ui-kit/src/tokens.css`, and that file agrees with the vendored design system |
| `versions` | every spelling of a product's version agrees with `versions.json`, and every AU plist names the factory, view class and sandbox claim its binary actually has |
| `release` | a release tag means what both release workflows think it means, and `release.json` is written in the shape Schwung Manager reads |
| `licenses`, `licenses_bundles` | everything that ships has a row in `THIRD_PARTY_LICENSES.md`, nothing listed has stopped shipping, and every built bundle carries the notices. The Rust crates' rows are generated by cargo-about and held to what `cargo metadata` says ships |
| `cargo_deny` | every crate in the Rust graph is under a licence on the allowlist in `deny.toml` and comes from crates.io |
| `spdx` | every source file this repository owns opens with its licence and its copyright |
| `e2e` | the four editors in Chrome, driven through their review harnesses against the mock hosts: gestures, keyboard, resize, the session handshake, and a screenshot each held to a committed baseline |
| `sc_processor`, `sc_host` | NI Side-Chain on the real engine: the iPlug2 sets reopened and saved back byte for byte, a note ducking from its own sample, CC 120 and CC 123 opening a held duck, a key ducking the track, the shape drawn against the duck played — in the processor, and in the built VST3 as a DAW hosts it, the engine's golden render bit for bit |
| `spectro_core` | the FFT against a naive DFT, the band mapping, and `assert_no_alloc`'s guard proving the audio path allocates nothing |
| `sg_processor`, `sg_host` | NI Spectrogram on the real analyzer: the iPlug2 sets reopened and saved back byte for byte, a tone drawn at its frequency straight into the editor's buffers, a Listen-In's bus from another process listed by its name and drawn, the audio through bit for bit — in the processor, and in the built VST3 as a DAW hosts it |
| `abus_ipc` | a bus written in one process and read in another. **The only test that would fail over a process-local ring, which is the whole reason the transport is shared memory.** `abus_ipc_rosetta` and `abus_ipc_rosetta_reader` do it between the x86_64 and arm64 slices, as Live under Rosetta and a native host would |
| `abus_core` | the ring's wrap and overrun, the claim protocol, and a writer running flat out against a slow reader with every delivered block checked for continuity — a spliced buffer looks exactly like audio |
| `listenin_wire`, `listenin_wire_js` | the state string and the label sanitiser, both sides pinned to one table the plugin's own C++ generates |
| `ni_wire` | the pieces of plugin arithmetic where being wrong is silent — the scope quantiser, the message split, the editor height, the transport advance |
| `tg_fade`, `tg_fade_js` | the fade's per-step level factors in both directions, against the engine's own *measured* gain — DC in with no envelope, so the gain during a step IS that factor. The editor mirrors the formula for the pads and the ring, so the mirror is pinned |

### Coverage

```sh
./scripts/coverage.sh
```

One command, four languages. `build/coverage/` gets `coverage.json` for a
machine, `summary.txt` and `html/index.html` for a person, and `lcov.info` for
an editor's gutter. It builds into `build-coverage/` and never into `build/`:
the instrumented build is `-O0`, one architecture and has no `NDEBUG`, so
nothing from it can be shipped by mistake.

**One engine for all three languages.** Rust compiles through LLVM, so the same
`-fprofile-instr-generate` instrumentation and the same `llvm-cov` reader serve
the C, the C++ and both cargo suites; node's own `--experimental-test-coverage`
covers the kit's libraries without adding a dependency to a tree that is kept
small for the licence audit, and the components — which only a browser runs —
are measured by Chrome itself during the e2e suite and mapped back to their
sources through the bundle's source map (`scripts/e2e-coverage.mjs`). It needs `cargo install cargo-llvm-cov` and
`rustup component add llvm-tools-preview` once — rustc carries its own LLVM, and
a profile it writes is refused by Xcode's reader with an error that names
neither. The script names both tools if they are missing rather than failing
somewhere inside `llvm-cov`.

**The figure is an upper bound, and says so.** `lcov` records only what was
loaded, so a file no test reaches is absent from the report rather than zero —
which means the total *rises* when untested code is added. That is a number
worth less than none, because it is trusted. So the report walks the source
tree, names every file no tracefile mentions, fails `coverage_floor` on them and
puts them in a box at the top of the HTML. The first honest run read 54.4% with
27 files invisible, including 1,315 lines of `ui_chain.js`.

**Lines are the coarse metric here, and the report prints all three.** This
code is dense with ternaries and one-line guards — `isfinite(v) ? clamp : 0.f`
is a whole rule on one line — so deleting the test that covers the NaN case
leaves line coverage at 100% and moves only regions and branches. That was
measured rather than assumed: `Wire.cpp` goes 20/20 regions and 12/12 branches
to 19/20 and 11/12, with lines unchanged at 20/20. Read the branch column when
judging whether a file is actually exercised.

`tests/coverage.floors.json` holds the floor — 80%, which is AGENTS.md's number
— and the exemptions. An exemption must name a unit or file that still exists
and carry a reason rather than a note, and a test enforces both: a stale excuse
is how a floor quietly stops meaning anything. Floors apply to units **derived
from paths**, so a plugin arriving in this repository is measured on arrival
rather than being silently absent from the denominator.

**It is enforcing**: every counted unit clears 80%, and `coverage_floor` (full
tier) fails when one does not. What is exempt is only what cannot be built into
anything a test runs: the Schwung module's `ui_chain.js`, which imports the
Move's own shared modules by their device paths, and the five files that compile
only inside a plugin-format target — `WebPlugin.cpp` and the four plugin classes.
The files lifted out of those classes for testing (`Params`, `Patch`, `State`,
`Wire`, `Editor`) are counted, and each exemption names the logic still left
behind.

## The products

Each product's manual lives with it, and this site renders those same files:

| | |
|---|---|
| [NI Trance Gate](plugins/trance-gate/README.md) | a tempo-locked step gate — [in Live](plugins/trance-gate/docs/live.md), [on the Move](plugins/trance-gate/docs/schwung.md) |
| [NI Spectrogram](plugins/spectrogram/README.md) | a rolling STFT analyzer — [in Live](plugins/spectrogram/docs/live.md) |
| [NI Listen-In](plugins/listen-in/README.md) | a tap that publishes a track on a numbered bus — [in Live](plugins/listen-in/docs/live.md) |
| [NI Side-Chain](plugins/side-chain/README.md) | a ducker on the transport, a MIDI note or a key input |

What changed in each release, per product, is in [CHANGELOG.md](CHANGELOG.md).

Published at **https://graebe.github.io/neon-ingvy-audio-plugins/**, built from this
repository's own Markdown — see [site/README.md](site/README.md).

## Licence

**GPL-3.0-or-later.** Copyright (C) 2026 Torben Gräber.

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version. It is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. [LICENSE](LICENSE) is the licence's full text.

It was MIT until 2026-10-06, and the reasons for the change are in
[docs/adr/0001-gpl-3.0-or-later.md](docs/adr/0001-gpl-3.0-or-later.md).

### How the licences combine

Everything this repository builds on is under a licence GPLv3 can take in, and
every part keeps its own notice: a permissive licence's conditions travel with
the copy rather than being replaced by ours.
[THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) holds them all, and ships
inside every bundle and every module beside `LICENSE`.

| | licence | how it combines |
|---|---|---|
| this repository | **GPL-3.0-or-later** | |
| JUCE 9, the plugin shell that replaces iPlug2 | **AGPLv3** | GPLv3 section 13 allows a GPLv3 work to be combined with an AGPLv3 one and the result conveyed: our part stays under GPLv3, and AGPLv3's own section 13, on interaction through a network, applies to the combination as such |
| VST3 SDK | **MIT** since 3.8 | permissive; Steinberg withdrew the GPLv3-or-proprietary dual licence with 3.8.0 |
| iPlug2 with WDL, the shell until JUCE replaces it | **zlib** | permissive |
| CLAP, clap-helpers, JSON for Modern C++ (compiled in by iPlug2) | **MIT** | permissive |
| Rust crates from crates.io | each under a licence on the allowlist in [`deny.toml`](deny.toml): MIT, Apache-2.0 (also WITH LLVM-exception), BSD-1-Clause, BSD-2-Clause, BSD-3-Clause, ISC, Zlib, MPL-2.0, Unicode-3.0, CC0-1.0, Unlicense, GPL-3.0-or-later or GPL-3.0-only | `cargo deny` refuses anything else, GPL-2.0-only included, which cannot be combined with GPLv3; cargo-about writes the crates' notices into THIRD_PARTY_LICENSES.md |
| the Side-Chain's MIDI trigger, ported from [schwung-ducker](https://github.com/charlesvestal/schwung-ducker) | **MIT**, © 2026 Charles Vestal | a port is a derivative work, so its notice stays beside ours |
| JetBrains Mono, in every editor | **SIL OFL 1.1** | `OFL.txt` travels with the font |

### The source

GPLv3 gives everyone who receives a plugin or a module the right to the source
it was built from (section 6). **Every release links its tagged source**: the
release notes name the tag the build came from and link that tag's tree, whose
scripts fetch the pinned submodule and SDKs (`scripts/fetch-sdks.sh`). A build
made from anything but a tag is not a release.
