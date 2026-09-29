# Third-party licences

Copyright © 2026 Torben Gräber. What follows are the notices this project's
dependencies require.

**The licence is uniform: MIT throughout.** Every dependency below is
permissive, and nothing in the chain is copyleft. See [LICENSE](LICENSE) and
the README for how that came to be true.

## The plugin framework — permissive

| Component | Licence |
|---|---|
| [iPlug2](https://github.com/iPlug2/iPlug2) | **zlib**, © the iPlug 2 Developers; based on WDL-OL/iPlug by Oli Larkin and iPlug v1 by John Schwartz / Cockos |
| VST3 SDK (vendored in iPlug2) | **MIT**, © 2026 Steinberg Media Technologies GmbH |
| CLAP | **MIT** |

The VST3 SDK is the thing most often assumed to force copyleft and no longer
does: Steinberg withdrew the GPLv3-or-proprietary dual licence and the copy
iPlug2 carries is plain MIT. That is what makes an MIT VST3 possible at all.

## Removed, and recorded so nobody re-adds them looking for a notice

**JUCE 8** — **AGPLv3**-or-commercial, and AGPLv3 is a *stronger* obligation
than GPLv3 rather than an equal one: while that target shipped, the artefact
had to be conveyed under AGPLv3. It was the last copyleft dependency here and
the only reason this repository was not MIT. Removing it meant removing the
2,586-line editor it drew, which is the real cost and is recorded in the
commit before the removal.


`vst3-sys`, `vst3-com`, `vst3-com-macros`, `vst3-com-macros-support`
(**GPL-3.0-or-later**) and `nih_plug`, `nih_plug_derive`, `nih_plug_xtask`,
`nih_log` (**ISC**, © 2022-2024 Robbert van der Helm) were dependencies until
the nih-plug wrapper was replaced by the iPlug2 one. They are no longer linked
into anything, so their notices no longer apply — `vst3-sys` was the single
crate making the Rust plugin GPL, reached only through `nih_export_vst3!()`.

## Test-only, and linked into nothing that ships

| Component | Licence |
|---|---|
| [doctest](https://github.com/doctest/doctest) 2.4.12 | **MIT**, © 2016-2023 Viktor Kirilov |

Vendored as a single header at `external/doctest/doctest.h` and reached only by
the targets in `tests/cpp/`. No plugin, module or bundle links it, so it adds
nothing to any artefact's notices — it is recorded here because a dependency
that is not written down is one somebody has to rediscover.

**It is MIT, and that was the deciding factor rather than a coincidence.**
Catch2 (BSL-1.0) and GoogleTest (BSD-3-Clause) are both permissive and either
would have worked; both would also have put the first exception into the
sentence at the top of this file. doctest is additionally one header with no
build step, which is the same argument that keeps the editors on vite and solid
and nothing else.

The coverage tooling adds no row at all: `llvm-cov`, `llvm-profdata` and
`cargo-llvm-cov` are developer tools that run *on* the build rather than inside
it. `lcov`/`genhtml` were not used, and their being **GPL-2.0** is why — there
is no obligation attached to running them, but `llvm-cov` renders HTML already
and a licence table with no exceptions in it is worth keeping.

## Bundled assets

**JetBrains Mono** (© 2020 The JetBrains Mono Project Authors, **SIL Open Font
License 1.1**), `Regular` and `Medium`. It is the design system's one typeface
and everything that draws the design system bundles it:

| bundle | path inside it |
|---|---|
| `TranceGate.{vst3,clap,component}` | `Contents/Resources/web/fonts/` |
| `Spectrogram.{vst3,clap,component}` | `Contents/Resources/web/fonts/` |
| the documentation site | `/neon-ingvy-audio-plugins/fonts/` |

**The OFL requires its text to travel beside the font**, so `OFL.txt` sits in
each `fonts/` directory -- globbed into every plugin bundle by each plugin's
CMakeLists, and copied into the site's by `site/scripts/stage-assets.mjs`, which
fails the build if it is not there. A font copied without it is the one licence
mistake this repository can make by forgetting a file rather than by choosing a
dependency.

A site that serves the font is a bundle like any other. It is listed here
because it is easy not to think of it as one.
It is permissive and GPL-compatible.

This section previously read "none at present": the font had gone with the JUCE
editor and came back with the WebView one without the note following it.

## The engines

`tg-core`, `tg-capi`, `tg-move`, `spectro-core`, `spectro-capi` and
`bus-core`, `bus-capi`, in `engines/`, © 2026 Torben Gräber. They have no
dependencies of their own.

They were relicensed from MIT to GPL-3.0-or-later when this build moved to
nih-plug, and back to **MIT** once the premise behind that turned out to be
false. Released as v1.0.0 with the notice inside the module tarball, which is
what MIT asks of a copy that reaches a device without a repository near it.

`spectro-core`, `spectro-capi` from `engines/spectro` in this repository,
© 2026 Torben Gräber, **MIT**. They have no dependencies of their own either,
and that is deliberate: the FFT is ninety lines here rather than a crate,
because a crate that needs no attribution is cheaper than one that does. See
the comment at the top of `cmake/SpectroEngine.cmake` for why this engine is
in-repo while the Trance Gate's is a submodule.

`bus-core`, `bus-capi` from `engines/audio-bus`, © 2026 Torben Gräber,
**MIT**. No dependencies either, and in this case that meant declaring the six
POSIX calls the transport needs -- `shm_open`, `ftruncate`, `mmap`, `fstat`,
`kill`, `getpid` -- rather than depending on `libc`. That crate is
MIT/Apache-2.0 and would have added nothing to this file, so the choice was
about weight rather than licence: it is a large thing to borrow `mmap` from,
and a declaration that is wrong fails at the first call rather than silently.

This is also the only crate here that is shared BETWEEN products rather than
belonging to one, so it is worth saying where it ends up: inside every plugin
that links it, statically, with no runtime component and nothing installed
outside the bundle.

## Every other dependency

All permissive, all requiring only that their copyright notices be preserved —
which this file does by listing them.

| Crate | Licence |
|---|---|
| `addr2line` | Apache-2.0 OR MIT |
| `adler2` | 0BSD OR MIT OR Apache-2.0 |
| `anyhow` | MIT OR Apache-2.0 |
| `anymap3` | BlueOak-1.0.0 OR MIT OR Apache-2.0 |
| `atomic_float` | Apache-2.0 OR MIT OR Zlib |
| `atomic_refcell` | Apache-2.0 OR MIT |
| `atty` | MIT |
| `backtrace` | MIT OR Apache-2.0 |
| `bitflags` | MIT OR Apache-2.0 |
| `camino` | MIT OR Apache-2.0 |
| `cargo-platform` | MIT OR Apache-2.0 |
| `cargo_metadata` | MIT |
| `cfg-if` | MIT OR Apache-2.0 |
| `clap-sys` | MIT/Apache-2.0 |
| `core-foundation-sys` | MIT OR Apache-2.0 |
| `core-foundation` | MIT OR Apache-2.0 |
| `crossbeam-channel` | MIT OR Apache-2.0 |
| `crossbeam-deque` | MIT OR Apache-2.0 |
| `crossbeam-epoch` | MIT OR Apache-2.0 |
| `crossbeam-queue` | MIT OR Apache-2.0 |
| `crossbeam-utils` | MIT OR Apache-2.0 |
| `crossbeam` | MIT OR Apache-2.0 |
| `deranged` | MIT OR Apache-2.0 |
| `equivalent` | Apache-2.0 OR MIT |
| `gimli` | MIT OR Apache-2.0 |
| `goblin` | MIT |
| `hashbrown` | MIT OR Apache-2.0 |
| `hermit-abi` | MIT OR Apache-2.0 |
| `indexmap` | Apache-2.0 OR MIT |
| `itoa` | MIT OR Apache-2.0 |
| `libc` | MIT OR Apache-2.0 |
| `lock_api` | MIT OR Apache-2.0 |
| `log` | MIT OR Apache-2.0 |
| `malloc_buf` | MIT |
| `memchr` | Unlicense OR MIT |
| `midi-consts` | MIT OR Apache-2.0 |
| `miniz_oxide` | MIT OR Zlib OR Apache-2.0 |
| `num-conv` | MIT OR Apache-2.0 |
| `num_threads` | MIT OR Apache-2.0 |
| `objc` | MIT |
| `object` | Apache-2.0 OR MIT |
| `once_cell` | MIT OR Apache-2.0 |
| `parking_lot_core` | MIT OR Apache-2.0 |
| `parking_lot` | MIT OR Apache-2.0 |
| `plain` | MIT/Apache-2.0 |
| `powerfmt` | MIT OR Apache-2.0 |
| `proc-macro2` | MIT OR Apache-2.0 |
| `quote` | MIT OR Apache-2.0 |
| `raw-window-handle` | MIT OR Apache-2.0 OR Zlib |
| `redox_syscall` | MIT |
| `reflink` | MIT/Apache-2.0 |
| `rustc-demangle` | MIT/Apache-2.0 |
| `scopeguard` | MIT OR Apache-2.0 |
| `scroll_derive` | MIT |
| `scroll` | MIT |
| `semver` | MIT OR Apache-2.0 |
| `serde_core` | MIT OR Apache-2.0 |
| `serde_derive` | MIT OR Apache-2.0 |
| `serde_json` | MIT OR Apache-2.0 |
| `serde_spanned` | MIT OR Apache-2.0 |
| `serde` | MIT OR Apache-2.0 |
| `smallvec` | MIT OR Apache-2.0 |
| `syn` | MIT OR Apache-2.0 |
| `termcolor` | Unlicense OR MIT |
| `thiserror-impl` | MIT OR Apache-2.0 |
| `thiserror` | MIT OR Apache-2.0 |
| `time-core` | MIT OR Apache-2.0 |
| `time-macros` | MIT OR Apache-2.0 |
| `time` | MIT OR Apache-2.0 |
| `toml_datetime` | MIT OR Apache-2.0 |
| `toml_edit` | MIT OR Apache-2.0 |
| `toml` | MIT OR Apache-2.0 |
| `unicode-ident` | (MIT OR Apache-2.0) AND Unicode-3.0 |
| `widestring` | MIT OR Apache-2.0 |
| `winapi-i686-pc-windows-gnu` | MIT/Apache-2.0 |
| `winapi-util` | Unlicense OR MIT |
| `winapi-x86_64-pc-windows-gnu` | MIT/Apache-2.0 |
| `winapi` | MIT/Apache-2.0 |
| `windows-link` | MIT OR Apache-2.0 |
| `windows-sys` | MIT OR Apache-2.0 |
| `windows-targets` | MIT OR Apache-2.0 |
| `windows_aarch64_gnullvm` | MIT OR Apache-2.0 |
| `windows_aarch64_msvc` | MIT OR Apache-2.0 |
| `windows_i686_gnu` | MIT OR Apache-2.0 |
| `windows_i686_msvc` | MIT OR Apache-2.0 |
| `windows_x86_64_gnullvm` | MIT OR Apache-2.0 |
| `windows_x86_64_gnu` | MIT OR Apache-2.0 |
| `windows_x86_64_msvc` | MIT OR Apache-2.0 |
| `windows` | MIT OR Apache-2.0 |
| `winnow` | MIT |
| `zmij` | MIT |

Full licence texts for these crates are in the vendored sources under
`~/.cargo/registry/src/`, and each crate's canonical text is on its
[crates.io](https://crates.io) page.
