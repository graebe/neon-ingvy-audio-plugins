# Third-party licences

Copyright (C) 2026 Torben Gräber. This project is licensed under the GNU
General Public License, version 3 or (at your option) any later version —
GPL-3.0-or-later, see [LICENSE](LICENSE). What follows are the notices its
dependencies require.

**Every licence below is one GPL-3.0-or-later can take in**, and each keeps its
own conditions: a permissive licence asks for its notice to travel with the
copy, and ours does not replace it. This file ships inside every artefact
beside `LICENSE` — each plugin bundle's `Contents/Resources/`, the release zips
and each Schwung module tarball — so the notices reach whoever receives a copy.
`scripts/check-licenses.mjs` (`ctest -R licenses`) fails when something that
ships has no entry here, or when an entry is for something that no longer
ships.

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
© 2026 Torben Gräber, GPL-3.0-or-later like the rest, and needs no row.

## Bundled font

**JetBrains Mono** (© 2020 The JetBrains Mono Project Authors, **SIL Open Font
License 1.1**), `Regular` and `Medium`. It is the design system's one typeface
and everything that draws the design system bundles it. The copy is the kit's
(`ui-kit/src/fonts`): subset to the characters the editors draw and converted to
WOFF by `scripts/subset-fonts.sh`, which the OFL permits; each editor carries it
inside its stylesheet (`web/assets/style.css`) and the site as an emitted asset.
It keeps its name: the OFL's Reserved Font Name clause does not apply, JetBrains
Mono declaring none.

| Bundle | The font | `OFL.txt` |
|---|---|---|
| `NITranceGate.{vst3,clap,component}` | inlined, as a data URI, in `Contents/Resources/web/assets/style.css` | `Contents/Resources/web/fonts/OFL.txt` |
| `NISpectrogram.{vst3,clap,component}` | inlined, as a data URI, in `Contents/Resources/web/assets/style.css` | `Contents/Resources/web/fonts/OFL.txt` |
| `NIListenIn.{vst3,clap,component}` | inlined, as a data URI, in `Contents/Resources/web/assets/style.css` | `Contents/Resources/web/fonts/OFL.txt` |
| `NISideChain.{vst3,clap,component}` | inlined, as a data URI, in `Contents/Resources/web/assets/style.css` | `Contents/Resources/web/fonts/OFL.txt` |
| the documentation site | emitted by the build as a hashed asset under `/neon-ingvy-audio-plugins/_astro/` | `/neon-ingvy-audio-plugins/fonts/OFL.txt` |

**The OFL requires its text to travel with the font**, so `OFL.txt` sits beside
the kit's font files in `ui-kit/src/fonts`, and wherever the font ships: each
editor copies it from its own `ui/public/fonts/` into `web/fonts/`, so it is in
every plugin bundle with the editor, and `site/scripts/stage-assets.mjs` copies
the kit's into the site's `/fonts/`, failing the build if it is not there. The
OFL is permissive and GPL-compatible.

## The engines — this repository's own

| Crates | Where | Licence |
|---|---|---|
| `tg-core`, `tg-capi`, `tg-move` | `engines/trance-gate` | **GPL-3.0-or-later**, © 2026 Torben Gräber |
| `spectro-core`, `spectro-recv`, `spectro-capi` | `engines/spectro` | **GPL-3.0-or-later**, © 2026 Torben Gräber |
| `bus-core`, `bus-capi` | `engines/audio-bus` | **GPL-3.0-or-later**, © 2026 Torben Gräber |
| `sc-core`, `sc-capi`, `sc-move` | `engines/side-chain` | **GPL-3.0-or-later**, © 2026 Torben Gräber, with a ported part — see below |
| `ground-core`, `ground-capi` | `engines/ground` | **GPL-3.0-or-later**, © 2026 Torben Gräber |
| `shell-core`, `shell-capi` | `engines/shell` | **GPL-3.0-or-later**, © 2026 Torben Gräber |
| `ni-dsp`, `ni-schwung` | `engines/shared` | **GPL-3.0-or-later**, © 2026 Torben Gräber |
| `ni-testkit` | `engines/shared` | **GPL-3.0-or-later**, © 2026 Torben Gräber — a dev-dependency only; it ships in nothing |

These are the workspace's members, each `publish = false`, and
`scripts/check-licenses.mjs` holds this table to the path packages in
`Cargo.lock`. Crates from crates.io are the next section's.

Taking no crate from crates.io was a rule while the project was MIT, so that
this file stayed short.
[docs/adr/0003-established-rust-crates.md](docs/adr/0003-established-rust-crates.md)
reverses it: established crates replace the hand-written FFT, queues and
parsers.

## Rust crates from crates.io — compiled into the engines that depend on them

Each one is under a licence on the allowlist in [`deny.toml`](deny.toml) and
comes from crates.io: `cargo deny check licenses bans sources`
(`ctest -R cargo_deny`) refuses anything else. A crate is compiled into the
static library or shared object of every engine that depends on it, so its
notice travels with each plugin and module that links that engine.

The text between the markers is generated by cargo-about
(`scripts/gen-rust-notices.sh`), and `scripts/check-licenses.mjs` fails when it
lists other crates, or other versions, than `cargo metadata` says ship.

<!-- BEGIN Rust crates: written by scripts/gen-rust-notices.sh. Edit scripts/rust-notices.hbs, never this text. -->

No crate from crates.io is linked into anything that ships: `Cargo.lock` holds
only this repository's own crates, listed in the section above.

<!-- END Rust crates -->

## Ported source, which carries a notice even though no library does

**A port is a derivative work.** Nothing below is linked, vendored or
downloaded — the code was read and rewritten in another language — and that is
exactly the case MIT's notice requirement covers.

| Ported into | From | Licence |
|---|---|---|
| `engines/side-chain/crates/sc-core/src/midi.rs` | [`schwung-ducker`](https://github.com/charlesvestal/schwung-ducker)'s `src/dsp/ducker.c` | **MIT**, © 2026 Charles Vestal |

What was taken: the MIDI trigger semantics — the channel filter, the note
match, Trigger versus Gate, a note-on at velocity zero read as a note-off, and
velocity scaling the depth. It ships in NI Side-Chain's plugin bundle and its
Move module.

What was not: the envelope's structure (this one has a Delay that goes negative
and cycle-relative times, and its stage machine is a different one), the sample
offsets (`ducker.c` applies a note at the top of its block), and every other
trigger source. The file itself says which lines it came from, and carries the
upstream notice beside its own GPL-3.0-or-later header: MIT allows the port in
a GPLv3 work on exactly that condition.

A second thing was taken and has since been removed: the `Pump` curve — linear
going down, a cubic ease-out coming back — was ported into `shape.rs` and later
dropped, along with the direction argument that existed only to serve it.
Nothing of it remains, so it no longer needs a notice.

## Test-only, and linked into nothing that ships

| Component | Licence |
|---|---|
| `doctest` 2.4.12 | **MIT**, © 2016-2023 Viktor Kirilov (portions derived from Catch2, **BSL-1.0**) |
| `plugin_api_v1.h`, `audio_fx_api_v2.h` — Schwung's module API | **MIT**, © 2025-2026 Charles Vestal |

doctest is vendored as a single header at `external/doctest/doctest.h` and
reached only by the targets in `tests/cpp/`.

The two Schwung headers are vendored at `engines/trance-gate/include/`, copied
unmodified from [Schwung](https://github.com/charlesvestal/schwung)'s
`src/host/`. `audio_fx_api_v2.h` is unchanged there since 2026-02 (Schwung
`68dc24b3`). `plugin_api_v1.h` is the revision Schwung carried from
2026-09-07 (`da640c8a`); the current one adds a static assert. They keep
their origin: no header of ours is added to them. Only the Trance Gate's
Move-side C tests include them (`engines/trance-gate/tests/test_gate.c`,
`render_ref.c`, `dump_params.c`), to load the module through the host's own
vtable. The module itself is Rust, and `ni-schwung` declares the same
structures for the host to call.

No plugin, module or bundle links any of these, so they add nothing to any
artefact's notices.

The coverage tooling adds no row: `llvm-cov`, `llvm-profdata` and
`cargo-llvm-cov` are developer tools that run *on* the build rather than inside
it. Likewise the licence tools (`cargo-deny`, `cargo-about`), the CI
validators (`pluginval`, `clap-validator`, `auval`) and the build tools (vite,
astro, CMake, cargo) are run, not shipped — vite's one
exception is its preload polyfill, listed above. So is `@playwright/test`
(**Apache-2.0**, © Microsoft Corporation), the root `devDependency` that drives
the editors' end-to-end tests (`tests/e2e`) in the Google Chrome already
installed: it is pinned in `package-lock.json`, downloads no browser, and no
editor build bundles it.

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

The full text is `OFL.txt`, in `ui-kit/src/fonts` beside the font files and at
the path the table under *Bundled font* gives for each artefact that ships the
font.
