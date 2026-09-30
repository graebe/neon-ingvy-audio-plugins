# Third-party licences

Copyright © 2026 Torben Gräber. This project is MIT-licensed (see
[LICENSE](LICENSE)); what follows are the notices its dependencies require.

**Everything below is permissive, and nothing in the chain is copyleft.** This
file ships inside every artefact beside `LICENSE` — each plugin bundle's
`Contents/Resources/`, the release zips and each Schwung module tarball — so
the notices reach whoever receives a copy. `scripts/check-licenses.mjs`
(`ctest -R licenses`) fails when something that ships has no entry here.

## Where each part ends up

| Artefact | What is in it |
|---|---|
| Plugin bundles (`NI*.vst3`, `NI*.component`, `NI*.clap`) | the plugin framework, the SDKs, JSON for Modern C++, the Rust standard library, the engines, and each editor's web bundle and fonts |
| Schwung module tarballs (`*-module.tar.gz`) | the Rust standard library and the engines; the Trance Gate's also carries its own `ui_chain.js` |
| The documentation site | JetBrains Mono |

## The plugin framework and SDKs — compiled into every plugin bundle

| Component | Version | Licence |
|---|---|---|
| `iPlug2` | the submodule's commit | **zlib**, © the iPlug 2 Developers; based on WDL-OL/iPlug by Oli Larkin (2011-2018) and iPlug v1 (2008) by John Schwartz / Cockos |
| `WDL` (Cockos, the parts iPlug2 compiles in) | as vendored in iPlug2 | **zlib**, © 2005 and later Cockos Incorporated |
| `VST3 SDK` | `v3.8.1_build_84` | **MIT**, © 2026 Steinberg Media Technologies GmbH |
| `CLAP` | `1.2.10` | **MIT**, © 2021 Alexandre Bique |
| `clap-helpers` | commit `55a5dd5d` | **MIT**, © 2021 Alexandre Bique |
| `JSON for Modern C++` (nlohmann/json) | 3.12.0, as vendored in iPlug2 | **MIT**, © 2013-2026 Niels Lohmann; portions © 2008-2009 Björn Hoehrmann, © 2009 Florian Loitsch, © 2018 The Abseil Authors (all MIT); Hedley © 2016-2021 Evan Nemerson (**CC0-1.0**) |

The SDK versions are the pins in `scripts/fetch-sdks.sh`; nothing is fetched
from a moving branch. The VST3 SDK is the one most often assumed to force
copyleft and no longer does: Steinberg withdrew the GPLv3-or-proprietary dual
licence with 3.8.0, and the pinned SDK's `LICENSE.txt` is plain MIT.

JSON for Modern C++ is compiled in by iPlug2's WebView editor bridge
(`IPlugWebViewEditorDelegate.h`), which every plugin here uses.

iPlug2's other bundled libraries — NanoVG, NanoSVG, MetalNanoVG, yoga, RTAudio,
RTMidi — belong to its IGraphics UI and its standalone app. These plugins use
the WebView editor and build no app, so none of them is linked into anything
that ships.

## The Rust standard library — in every plugin and every module

| Component | Licence |
|---|---|
| `Rust standard library` (`core`, `alloc`, `std`, and the crates `std` links in for backtraces: `addr2line`, `gimli`, `object`, `miniz_oxide`, `rustc-demangle`, `memchr`, `hashbrown`, `adler2`) | **MIT OR Apache-2.0**, © The Rust Project Developers and the respective crate authors; distributed here under MIT |

Each engine is a Rust static library (in a plugin) or shared object (in a
module), so the parts of the standard library it uses are compiled into it.

## The editors — in every plugin bundle's `Contents/Resources/web/`

| Package | Licence |
|---|---|
| `solid-js` | **MIT**, © 2016-2025 Ryan Carniato |
| `vite` (only its module-preload polyfill, a few hundred bytes at the top of ui.js) | **MIT**, © 2019-present VoidZero Inc. and Vite contributors |

The minifier strips comments, so each editor's build writes the full licence
text of every package it bundled to `assets/ui.js.LICENSE.txt` beside `ui.js`,
and marks `ui.js` with a comment pointing there. The build **fails** if it
bundles a package that has no row in the table above
(`scripts/vite-licenses.mjs`).

`@ultraviolet/ui` (in `ui-kit/`) is this repository's own design-system kit,
© 2026 Torben Gräber, MIT, and needs no row.

## Bundled font

**JetBrains Mono** (© 2020 The JetBrains Mono Project Authors, **SIL Open Font
License 1.1**), `Regular` and `Medium`. It is the design system's one typeface
and everything that draws the design system bundles it:

| Bundle | Path inside it |
|---|---|
| `NITranceGate.{vst3,clap,component}` | `Contents/Resources/web/fonts/` |
| `NISpectrogram.{vst3,clap,component}` | `Contents/Resources/web/fonts/` |
| `NIListenIn.{vst3,clap,component}` | `Contents/Resources/web/fonts/` |
| `NISideChain.{vst3,clap,component}` | `Contents/Resources/web/fonts/` |
| the documentation site | `/neon-ingvy-audio-plugins/fonts/` |

**The OFL requires its text to travel beside the font**, so `OFL.txt` sits in
each `fonts/` directory — copied into every plugin bundle with the editor, and
into the site's by `site/scripts/stage-assets.mjs`, which fails the build if it
is not there. The OFL is permissive and GPL-compatible.

## The engines — this repository's own

| Crates | Where | Licence |
|---|---|---|
| `tg-core`, `tg-capi`, `tg-move` | `engines/trance-gate` | **MIT**, © 2026 Torben Gräber |
| `spectro-core`, `spectro-recv`, `spectro-capi` | `engines/spectro` | **MIT**, © 2026 Torben Gräber |
| `bus-core`, `bus-capi` | `engines/audio-bus` | **MIT**, © 2026 Torben Gräber |
| `sc-core`, `sc-capi`, `sc-move` | `engines/side-chain` | **MIT**, © 2026 Torben Gräber, with a ported part — see below |
| `ground-core`, `ground-capi` | `engines/ground` | **MIT**, © 2026 Torben Gräber |
| `shell-core`, `shell-capi` | `engines/shell` | **MIT**, © 2026 Torben Gräber |
| `ni-dsp`, `ni-schwung` | `engines/shared` | **MIT**, © 2026 Torben Gräber |

**No crate here has a third-party dependency.** `Cargo.lock` holds only these
workspace members, which `scripts/check-licenses.mjs` verifies: a crate from
crates.io would need a row in this file before the check passes.

That is deliberate rather than incidental. The FFT is ninety lines in
`spectro-core` rather than a crate, and `bus-core` declares the six POSIX calls
the transport needs — `shm_open`, `ftruncate`, `mmap`, `fstat`, `kill`,
`getpid` — rather than depending on `libc`, which would have added nothing to
this file (it is MIT/Apache-2.0) but is a large thing to borrow `mmap` from.

## Ported source, which carries a notice even though no library does

**A port is a derivative work.** Nothing below is linked, vendored or
downloaded — the code was read and rewritten in another language — and that is
exactly the case MIT's notice requirement covers.

| Ported into | From | Licence |
|---|---|---|
| `engines/side-chain/crates/sc-core/src/midi.rs` | [`schwung-ducker`](https://github.com/charlesvestal/schwung-ducker)'s `src/dsp/ducker.c` | **MIT**, © charlesvestal |

What was taken: the MIDI trigger semantics — the channel filter, the note
match, Trigger versus Gate, a note-on at velocity zero read as a note-off, and
velocity scaling the depth. It ships in NI Side-Chain's plugin bundle and its
Move module.

What was not: the envelope's structure (this one has a Delay that goes negative
and cycle-relative times, and its stage machine is a different one), the sample
offsets (`ducker.c` applies a note at the top of its block), and every other
trigger source. The file itself says which lines it came from.

A second thing was taken and has since been removed: the `Pump` curve — linear
going down, a cubic ease-out coming back — was ported into `shape.rs` and later
dropped, along with the direction argument that existed only to serve it.
Nothing of it remains, so it no longer needs a notice.

## Test-only, and linked into nothing that ships

| Component | Licence |
|---|---|
| `doctest` 2.4.12 | **MIT**, © 2016-2023 Viktor Kirilov (portions derived from Catch2, **BSL-1.0**) |

Vendored as a single header at `external/doctest/doctest.h` and reached only by
the targets in `tests/cpp/`. No plugin, module or bundle links it, so it adds
nothing to any artefact's notices.

The coverage tooling adds no row: `llvm-cov`, `llvm-profdata` and
`cargo-llvm-cov` are developer tools that run *on* the build rather than inside
it. Likewise the CI validators (`pluginval`, `clap-validator`, `auval`) and the
build tools (vite, astro, CMake, cargo) are run, not shipped — vite's one
exception is its preload polyfill, listed above.

## No longer dependencies

`vst3-sys`, `vst3-com*` (GPL-3.0-or-later), `nih_plug*`/`nih_log` (ISC) and the
crates.io dependencies they brought were removed when the nih-plug wrapper was
replaced by iPlug2; JUCE (AGPLv3-or-commercial) went before that. None is linked
into anything, so their notices no longer apply and their rows were removed.

---

## Licence texts

### MIT

Applies to every component above marked **MIT**, each under its own copyright
line as listed:

> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to deal
> in the Software without restriction, including without limitation the rights
> to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
> copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in all
> copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
> OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
> SOFTWARE.

### zlib (iPlug2, WDL)

> This software is provided 'as-is', without any express or implied warranty.
> In no event will the authors be held liable for any damages arising from the
> use of this software.
>
> Permission is granted to anyone to use this software for any purpose,
> including commercial applications, and to alter it and redistribute it
> freely, subject to the following restrictions:
>
> 1. The origin of this software must not be misrepresented; you must not claim
>    that you wrote the original software. If you use this software in a
>    product, an acknowledgment in the product documentation would be
>    appreciated but is not required.
> 2. Altered source versions must be plainly marked as such, and must not be
>    misrepresented as being the original software.
> 3. This notice may not be removed or altered from any source distribution.

### SIL Open Font License 1.1 (JetBrains Mono)

The full text is `OFL.txt`, beside the font files in every `fonts/` directory
that carries them.
