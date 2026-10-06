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
| Plugin bundles (`NI*.vst3`, `NI*.component`, `NI*.clap`) | the plugin framework, the SDKs, JSON for Modern C++, the Rust standard library, the engines and the crates from crates.io they link, and each editor's web bundle and fonts |
| Schwung module tarballs (`*-module.tar.gz`) | the Rust standard library, the engines and the crates from crates.io they link; the Trance Gate's also carries its own `ui_chain.js` |
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

## Bundled music font

**Bravura** 1.482 (© 2015 Steinberg Media Technologies GmbH, **SIL Open Font
License 1.1**, Reserved Font Name "Bravura"), the reference font of SMuFL. The
native kit draws its notation with it (`ni::ui::GrandStaff`: clefs,
accidentals, noteheads) and embeds `Bravura.otf` whole and unmodified, as
BinaryData of `ni_ui_assets` -- which is what lets it keep its reserved name;
a subset would have to be renamed. The file and its `OFL.txt` are at
`plugins/_shared/ui/fonts/bravura/`, with their provenance in its `README.md`.

**The OFL travels with the font**: a plugin whose editor draws notation copies
`NI_UI_MUSIC_FONT_LICENSE` (that `OFL.txt`) into its bundle's resources. The
first is NI Chord-Detector, whose bundle row arrives with its JUCE build. The
OFL is permissive and GPL-compatible.

## The engines — this repository's own

| Crates | Where | Licence |
|---|---|---|
| `tg-core`, `tg-capi`, `tg-move` | `engines/trance-gate` | **GPL-3.0-or-later**, © 2026 Torben Gräber |
| `spectro-core`, `spectro-recv`, `spectro-capi` | `engines/spectro` | **GPL-3.0-or-later**, © 2026 Torben Gräber |
| `bus-core`, `bus-capi` | `engines/audio-bus` | **GPL-3.0-or-later**, © 2026 Torben Gräber |
| `sc-core`, `sc-capi`, `sc-move` | `engines/side-chain` | **GPL-3.0-or-later**, © 2026 Torben Gräber, with a ported part — see below |
| `cd-core`, `cd-capi` | `engines/chord-detector` | **GPL-3.0-or-later**, © 2026 Torben Gräber |
| `ground-core`, `ground-capi` | `engines/ground` | **GPL-3.0-or-later**, © 2026 Torben Gräber |
| `shell-core`, `shell-capi` | `engines/shell` | **GPL-3.0-or-later**, © 2026 Torben Gräber |
| `ni-dsp`, `ni-schwung` | `engines/shared` | **GPL-3.0-or-later**, © 2026 Torben Gräber |
| `ni-testkit` | `engines/shared` | **GPL-3.0-or-later**, © 2026 Torben Gräber — a dev-dependency only; it ships in nothing |
| `music-core` | `engines/shared` | **GPL-3.0-or-later**, © 2026 Torben Gräber — moved here from [neo-riemann](https://codeberg.org/graebe/neo-riemann), where it was MIT under the same owner |

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

**One crate is under MPL-2.0**: `triple_buffer`, which hands each plugin's
readouts from its audio thread to its other threads (`engines/shell`). The MPL
is a copyleft on its own files only, and its section 3.3 lets them be part of
a larger work under the GPL. What it asks of a binary is that whoever receives
one can get the source of those files: they are the crate exactly as published,
unmodified, at the version listed below — <https://crates.io/crates/triple_buffer>,
from <https://github.com/HadrienG2/triple-buffer>.

<!-- BEGIN Rust crates: written by scripts/gen-rust-notices.sh. Edit scripts/rust-notices.hbs, never this text. -->

Every crate from crates.io that an engine depends on, for any platform, and
the licence text it ships under. A platform's crates are listed even where
nothing here is built for it; deny.toml says why. Generated by cargo-about
from `Cargo.lock`; scripts/gen-rust-notices.sh rewrites it.

| Crate | Version | Licence |
|---|---|---|
| `arrayvec` | 0.7.8 | MIT OR Apache-2.0 |
| `atomic_float` | 1.1.0 | Apache-2.0 OR MIT OR Unlicense |
| `basedrop` | 0.1.3 | MIT OR Apache-2.0 |
| `crossbeam-utils` | 0.8.23 | MIT OR Apache-2.0 |
| `fastrand` | 2.5.0 | Apache-2.0 OR MIT |
| `itoa` | 1.0.18 | MIT OR Apache-2.0 |
| `lexical-core` | 1.0.6 | MIT OR Apache-2.0 |
| `lexical-parse-float` | 1.0.6 | MIT OR Apache-2.0 |
| `lexical-parse-integer` | 1.0.6 | MIT OR Apache-2.0 |
| `lexical-util` | 1.0.7 | MIT OR Apache-2.0 |
| `libc` | 0.2.190 | MIT OR Apache-2.0 |
| `memchr` | 2.8.3 | Unlicense OR MIT |
| `num-complex` | 0.4.6 | MIT OR Apache-2.0 |
| `num-integer` | 0.1.47 | MIT OR Apache-2.0 |
| `num-traits` | 0.2.19 | MIT OR Apache-2.0 |
| `primal-check` | 0.3.4 | MIT OR Apache-2.0 |
| `realfft` | 3.5.0 | MIT |
| `rtrb` | 0.4.0 | MIT OR Apache-2.0 |
| `rustfft` | 6.4.1 | MIT OR Apache-2.0 |
| `serde` | 1.0.229 | MIT OR Apache-2.0 |
| `serde_core` | 1.0.229 | MIT OR Apache-2.0 |
| `serde_json` | 1.0.151 | MIT OR Apache-2.0 |
| `strength_reduce` | 0.2.4 | MIT OR Apache-2.0 |
| `transpose` | 0.2.3 | MIT OR Apache-2.0 |
| `triple_buffer` | 9.0.0 | MPL-2.0 |
| `windows-link` | 0.2.1 | MIT OR Apache-2.0 |
| `windows-sys` | 0.61.2 | MIT OR Apache-2.0 |
| `wmidi` | 4.0.11 | MIT |
| `zmij` | 1.0.23 | MIT |

### MIT License: `windows-link` 0.2.1, `windows-sys` 0.61.2

```text
    MIT License

    Copyright (c) Microsoft Corporation.

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE

```

### MIT License: `primal-check` 0.3.4

```text
Copyright (c) 2014 Huon Wilson

Permission is hereby granted, free of charge, to any
person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the
Software without restriction, including without
limitation the rights to use, copy, modify, merge,
publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software
is furnished to do so, subject to the following
conditions:

The above copyright notice and this permission notice
shall be included in all copies or substantial portions
of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF
ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT
SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
```

### MIT License: `num-complex` 0.4.6, `num-integer` 0.1.47, `num-traits` 0.2.19

```text
Copyright (c) 2014 The Rust Project Developers

Permission is hereby granted, free of charge, to any
person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the
Software without restriction, including without
limitation the rights to use, copy, modify, merge,
publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software
is furnished to do so, subject to the following
conditions:

The above copyright notice and this permission notice
shall be included in all copies or substantial portions
of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF
ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT
SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

```

### MIT License: `rustfft` 6.4.1, `strength_reduce` 0.2.4

```text
Copyright (c) 2015 The RustFFT Developers

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

```

### MIT License: `transpose` 0.2.3

```text
Copyright (c) 2022 The transpose Developers

Permission is hereby granted, free of charge, to any
person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the
Software without restriction, including without
limitation the rights to use, copy, modify, merge,
publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software
is furnished to do so, subject to the following
conditions:

The above copyright notice and this permission notice
shall be included in all copies or substantial portions
of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF
ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT
SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

```

### MIT License: `atomic_float` 1.1.0

```text
Copyright (c) 2024 Thom Chiovoloni

Permission is hereby granted, free of charge, to any
person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the
Software without restriction, including without
limitation the rights to use, copy, modify, merge,
publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software
is furnished to do so, subject to the following
conditions:

The above copyright notice and this permission notice
shall be included in all copies or substantial portions
of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF
ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT
SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

```

### MIT License: `libc` 0.2.190

```text
Copyright (c) The Rust Project Developers

Permission is hereby granted, free of charge, to any
person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the
Software without restriction, including without
limitation the rights to use, copy, modify, merge,
publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software
is furnished to do so, subject to the following
conditions:

The above copyright notice and this permission notice
shall be included in all copies or substantial portions
of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF
ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT
SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

```

### MIT License: `arrayvec` 0.7.8

```text
Copyright (c) Ulrik Sverdrup "bluss" 2015-2023

Permission is hereby granted, free of charge, to any
person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the
Software without restriction, including without
limitation the rights to use, copy, modify, merge,
publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software
is furnished to do so, subject to the following
conditions:

The above copyright notice and this permission notice
shall be included in all copies or substantial portions
of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF
ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT
SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

```

### MIT License: `realfft` 3.5.0

`realfft` 3.5.0 ships no licence file. Its manifest declares MIT and names `HEnquist <henrik.enquist@gmail.com>` as its author; its source is at <https://github.com/HEnquist/realfft>.
The text below is therefore the licence as SPDX publishes it, whose copyright
line is a placeholder for the authorship stated above.

```text
MIT License

Copyright (c) <year> <copyright holders>

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the
following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial
portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT
LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO
EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
USE OR OTHER DEALINGS IN THE SOFTWARE.

```

### MIT License: `wmidi` 4.0.11

```text
Permission is hereby granted, free of charge, to any
person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the
Software without restriction, including without
limitation the rights to use, copy, modify, merge,
publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software
is furnished to do so, subject to the following
conditions:

The above copyright notice and this permission notice
shall be included in all copies or substantial portions
of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF
ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT
SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
```

### MIT License: `fastrand` 2.5.0, `itoa` 1.0.18, `lexical-core` 1.0.6, `lexical-parse-float` 1.0.6, `lexical-parse-integer` 1.0.6, `lexical-util` 1.0.7, `rtrb` 0.4.0, `serde` 1.0.229, `serde_core` 1.0.229, `serde_json` 1.0.151, `zmij` 1.0.23

```text
Permission is hereby granted, free of charge, to any
person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the
Software without restriction, including without
limitation the rights to use, copy, modify, merge,
publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software
is furnished to do so, subject to the following
conditions:

The above copyright notice and this permission notice
shall be included in all copies or substantial portions
of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF
ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT
SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

```

### MIT License: `basedrop` 0.1.3

```text
Permission is hereby granted, free of charge, to any
person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the
Software without restriction, including without
limitation the rights to use, copy, modify, merge,
publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software
is furnished to do so, subject to the following
conditions:

The above copyright notice and this permission notice
shall be included in all copies or substantial portions
of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF
ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT
SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

```

### MIT License: `memchr` 2.8.3

```text
The MIT License (MIT)

Copyright (c) 2015 Andrew Gallant

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

```

### MIT License: `crossbeam-utils` 0.8.23

```text
The MIT License (MIT)

Copyright (c) 2019 The Crossbeam Project Developers

Permission is hereby granted, free of charge, to any
person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the
Software without restriction, including without
limitation the rights to use, copy, modify, merge,
publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software
is furnished to do so, subject to the following
conditions:

The above copyright notice and this permission notice
shall be included in all copies or substantial portions
of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF
ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT
SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

```

### Mozilla Public License 2.0: `triple_buffer` 9.0.0

```text
Mozilla Public License Version 2.0
==================================

1. Definitions
--------------

1.1. "Contributor"
    means each individual or legal entity that creates, contributes to
    the creation of, or owns Covered Software.

1.2. "Contributor Version"
    means the combination of the Contributions of others (if any) used
    by a Contributor and that particular Contributor's Contribution.

1.3. "Contribution"
    means Covered Software of a particular Contributor.

1.4. "Covered Software"
    means Source Code Form to which the initial Contributor has attached
    the notice in Exhibit A, the Executable Form of such Source Code
    Form, and Modifications of such Source Code Form, in each case
    including portions thereof.

1.5. "Incompatible With Secondary Licenses"
    means

    (a) that the initial Contributor has attached the notice described
        in Exhibit B to the Covered Software; or

    (b) that the Covered Software was made available under the terms of
        version 1.1 or earlier of the License, but not also under the
        terms of a Secondary License.

1.6. "Executable Form"
    means any form of the work other than Source Code Form.

1.7. "Larger Work"
    means a work that combines Covered Software with other material, in
    a separate file or files, that is not Covered Software.

1.8. "License"
    means this document.

1.9. "Licensable"
    means having the right to grant, to the maximum extent possible,
    whether at the time of the initial grant or subsequently, any and
    all of the rights conveyed by this License.

1.10. "Modifications"
    means any of the following:

    (a) any file in Source Code Form that results from an addition to,
        deletion from, or modification of the contents of Covered
        Software; or

    (b) any new file in Source Code Form that contains any Covered
        Software.

1.11. "Patent Claims" of a Contributor
    means any patent claim(s), including without limitation, method,
    process, and apparatus claims, in any patent Licensable by such
    Contributor that would be infringed, but for the grant of the
    License, by the making, using, selling, offering for sale, having
    made, import, or transfer of either its Contributions or its
    Contributor Version.

1.12. "Secondary License"
    means either the GNU General Public License, Version 2.0, the GNU
    Lesser General Public License, Version 2.1, the GNU Affero General
    Public License, Version 3.0, or any later versions of those
    licenses.

1.13. "Source Code Form"
    means the form of the work preferred for making modifications.

1.14. "You" (or "Your")
    means an individual or a legal entity exercising rights under this
    License. For legal entities, "You" includes any entity that
    controls, is controlled by, or is under common control with You. For
    purposes of this definition, "control" means (a) the power, direct
    or indirect, to cause the direction or management of such entity,
    whether by contract or otherwise, or (b) ownership of more than
    fifty percent (50%) of the outstanding shares or beneficial
    ownership of such entity.

2. License Grants and Conditions
--------------------------------

2.1. Grants

Each Contributor hereby grants You a world-wide, royalty-free,
non-exclusive license:

(a) under intellectual property rights (other than patent or trademark)
    Licensable by such Contributor to use, reproduce, make available,
    modify, display, perform, distribute, and otherwise exploit its
    Contributions, either on an unmodified basis, with Modifications, or
    as part of a Larger Work; and

(b) under Patent Claims of such Contributor to make, use, sell, offer
    for sale, have made, import, and otherwise transfer either its
    Contributions or its Contributor Version.

2.2. Effective Date

The licenses granted in Section 2.1 with respect to any Contribution
become effective for each Contribution on the date the Contributor first
distributes such Contribution.

2.3. Limitations on Grant Scope

The licenses granted in this Section 2 are the only rights granted under
this License. No additional rights or licenses will be implied from the
distribution or licensing of Covered Software under this License.
Notwithstanding Section 2.1(b) above, no patent license is granted by a
Contributor:

(a) for any code that a Contributor has removed from Covered Software;
    or

(b) for infringements caused by: (i) Your and any other third party's
    modifications of Covered Software, or (ii) the combination of its
    Contributions with other software (except as part of its Contributor
    Version); or

(c) under Patent Claims infringed by Covered Software in the absence of
    its Contributions.

This License does not grant any rights in the trademarks, service marks,
or logos of any Contributor (except as may be necessary to comply with
the notice requirements in Section 3.4).

2.4. Subsequent Licenses

No Contributor makes additional grants as a result of Your choice to
distribute the Covered Software under a subsequent version of this
License (see Section 10.2) or under the terms of a Secondary License (if
permitted under the terms of Section 3.3).

2.5. Representation

Each Contributor represents that the Contributor believes its
Contributions are its original creation(s) or it has sufficient rights
to grant the rights to its Contributions conveyed by this License.

2.6. Fair Use

This License is not intended to limit any rights You have under
applicable copyright doctrines of fair use, fair dealing, or other
equivalents.

2.7. Conditions

Sections 3.1, 3.2, 3.3, and 3.4 are conditions of the licenses granted
in Section 2.1.

3. Responsibilities
-------------------

3.1. Distribution of Source Form

All distribution of Covered Software in Source Code Form, including any
Modifications that You create or to which You contribute, must be under
the terms of this License. You must inform recipients that the Source
Code Form of the Covered Software is governed by the terms of this
License, and how they can obtain a copy of this License. You may not
attempt to alter or restrict the recipients' rights in the Source Code
Form.

3.2. Distribution of Executable Form

If You distribute Covered Software in Executable Form then:

(a) such Covered Software must also be made available in Source Code
    Form, as described in Section 3.1, and You must inform recipients of
    the Executable Form how they can obtain a copy of such Source Code
    Form by reasonable means in a timely manner, at a charge no more
    than the cost of distribution to the recipient; and

(b) You may distribute such Executable Form under the terms of this
    License, or sublicense it under different terms, provided that the
    license for the Executable Form does not attempt to limit or alter
    the recipients' rights in the Source Code Form under this License.

3.3. Distribution of a Larger Work

You may create and distribute a Larger Work under terms of Your choice,
provided that You also comply with the requirements of this License for
the Covered Software. If the Larger Work is a combination of Covered
Software with a work governed by one or more Secondary Licenses, and the
Covered Software is not Incompatible With Secondary Licenses, this
License permits You to additionally distribute such Covered Software
under the terms of such Secondary License(s), so that the recipient of
the Larger Work may, at their option, further distribute the Covered
Software under the terms of either this License or such Secondary
License(s).

3.4. Notices

You may not remove or alter the substance of any license notices
(including copyright notices, patent notices, disclaimers of warranty,
or limitations of liability) contained within the Source Code Form of
the Covered Software, except that You may alter any license notices to
the extent required to remedy known factual inaccuracies.

3.5. Application of Additional Terms

You may choose to offer, and to charge a fee for, warranty, support,
indemnity or liability obligations to one or more recipients of Covered
Software. However, You may do so only on Your own behalf, and not on
behalf of any Contributor. You must make it absolutely clear that any
such warranty, support, indemnity, or liability obligation is offered by
You alone, and You hereby agree to indemnify every Contributor for any
liability incurred by such Contributor as a result of warranty, support,
indemnity or liability terms You offer. You may include additional
disclaimers of warranty and limitations of liability specific to any
jurisdiction.

4. Inability to Comply Due to Statute or Regulation
---------------------------------------------------

If it is impossible for You to comply with any of the terms of this
License with respect to some or all of the Covered Software due to
statute, judicial order, or regulation then You must: (a) comply with
the terms of this License to the maximum extent possible; and (b)
describe the limitations and the code they affect. Such description must
be placed in a text file included with all distributions of the Covered
Software under this License. Except to the extent prohibited by statute
or regulation, such description must be sufficiently detailed for a
recipient of ordinary skill to be able to understand it.

5. Termination
--------------

5.1. The rights granted under this License will terminate automatically
if You fail to comply with any of its terms. However, if You become
compliant, then the rights granted under this License from a particular
Contributor are reinstated (a) provisionally, unless and until such
Contributor explicitly and finally terminates Your grants, and (b) on an
ongoing basis, if such Contributor fails to notify You of the
non-compliance by some reasonable means prior to 60 days after You have
come back into compliance. Moreover, Your grants from a particular
Contributor are reinstated on an ongoing basis if such Contributor
notifies You of the non-compliance by some reasonable means, this is the
first time You have received notice of non-compliance with this License
from such Contributor, and You become compliant prior to 30 days after
Your receipt of the notice.

5.2. If You initiate litigation against any entity by asserting a patent
infringement claim (excluding declaratory judgment actions,
counter-claims, and cross-claims) alleging that a Contributor Version
directly or indirectly infringes any patent, then the rights granted to
You by any and all Contributors for the Covered Software under Section
2.1 of this License shall terminate.

5.3. In the event of termination under Sections 5.1 or 5.2 above, all
end user license agreements (excluding distributors and resellers) which
have been validly granted by You or Your distributors under this License
prior to termination shall survive termination.

************************************************************************
*                                                                      *
*  6. Disclaimer of Warranty                                           *
*  -------------------------                                           *
*                                                                      *
*  Covered Software is provided under this License on an "as is"       *
*  basis, without warranty of any kind, either expressed, implied, or  *
*  statutory, including, without limitation, warranties that the       *
*  Covered Software is free of defects, merchantable, fit for a        *
*  particular purpose or non-infringing. The entire risk as to the     *
*  quality and performance of the Covered Software is with You.        *
*  Should any Covered Software prove defective in any respect, You     *
*  (not any Contributor) assume the cost of any necessary servicing,   *
*  repair, or correction. This disclaimer of warranty constitutes an   *
*  essential part of this License. No use of any Covered Software is   *
*  authorized under this License except under this disclaimer.         *
*                                                                      *
************************************************************************

************************************************************************
*                                                                      *
*  7. Limitation of Liability                                          *
*  --------------------------                                          *
*                                                                      *
*  Under no circumstances and under no legal theory, whether tort      *
*  (including negligence), contract, or otherwise, shall any           *
*  Contributor, or anyone who distributes Covered Software as          *
*  permitted above, be liable to You for any direct, indirect,         *
*  special, incidental, or consequential damages of any character      *
*  including, without limitation, damages for lost profits, loss of    *
*  goodwill, work stoppage, computer failure or malfunction, or any    *
*  and all other commercial damages or losses, even if such party      *
*  shall have been informed of the possibility of such damages. This   *
*  limitation of liability shall not apply to liability for death or   *
*  personal injury resulting from such party's negligence to the       *
*  extent applicable law prohibits such limitation. Some               *
*  jurisdictions do not allow the exclusion or limitation of           *
*  incidental or consequential damages, so this exclusion and          *
*  limitation may not apply to You.                                    *
*                                                                      *
************************************************************************

8. Litigation
-------------

Any litigation relating to this License may be brought only in the
courts of a jurisdiction where the defendant maintains its principal
place of business and such litigation shall be governed by laws of that
jurisdiction, without reference to its conflict-of-law provisions.
Nothing in this Section shall prevent a party's ability to bring
cross-claims or counter-claims.

9. Miscellaneous
----------------

This License represents the complete agreement concerning the subject
matter hereof. If any provision of this License is held to be
unenforceable, such provision shall be reformed only to the extent
necessary to make it enforceable. Any law or regulation which provides
that the language of a contract shall be construed against the drafter
shall not be used to construe this License against a Contributor.

10. Versions of the License
---------------------------

10.1. New Versions

Mozilla Foundation is the license steward. Except as provided in Section
10.3, no one other than the license steward has the right to modify or
publish new versions of this License. Each version will be given a
distinguishing version number.

10.2. Effect of New Versions

You may distribute the Covered Software under the terms of the version
of the License under which You originally received the Covered Software,
or under the terms of any subsequent version published by the license
steward.

10.3. Modified Versions

If you create software not governed by this License, and you want to
create a new license for such software, you may create and use a
modified version of this License if you rename the license and remove
any references to the name of the license steward (except to note that
such modified license differs from this License).

10.4. Distributing Source Code Form that is Incompatible With Secondary
Licenses

If You choose to distribute Source Code Form that is Incompatible With
Secondary Licenses under the terms of this version of the License, the
notice described in Exhibit B of this License must be attached.

Exhibit A - Source Code Form License Notice
-------------------------------------------

  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at http://mozilla.org/MPL/2.0/.

If it is not possible or desirable to put the notice in a particular
file, then You may include the notice in a location (such as a LICENSE
file in a relevant directory) where a recipient would be likely to look
for such a notice.

You may add additional accurate notices of copyright ownership.

Exhibit B - "Incompatible With Secondary Licenses" Notice
---------------------------------------------------------

  This Source Code Form is "Incompatible With Secondary Licenses", as
  defined by the Mozilla Public License, v. 2.0.
```

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
