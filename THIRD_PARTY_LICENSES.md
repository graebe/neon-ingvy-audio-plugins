# Third-party licences

Copyright © 2026 Torben Gräber. What follows are the notices this project's
dependencies require.

**The licence is not yet uniform, and this file says why.** The iPlug2 build
(`plugins/trance-gate-iplug`) and everything it links are permissive; the JUCE
build (`plugins/trance-gate`) is not, and while it exists the artefact it
produces is AGPLv3. See [LICENSE](LICENSE) and the README.

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

`vst3-sys`, `vst3-com`, `vst3-com-macros`, `vst3-com-macros-support`
(**GPL-3.0-or-later**) and `nih_plug`, `nih_plug_derive`, `nih_plug_xtask`,
`nih_log` (**ISC**, © 2022-2024 Robbert van der Helm) were dependencies until
the nih-plug wrapper was replaced by the iPlug2 one. They are no longer linked
into anything, so their notices no longer apply — `vst3-sys` was the single
crate making the Rust plugin GPL, reached only through `nih_export_vst3!()`.

## Bundled assets

**JetBrains Mono**, © 2020 The JetBrains Mono Project Authors, under the **SIL
Open Font License 1.1**. The OFL requires its text to travel with the font;
it ships as `plugins/trance-gate/Resources/OFL.txt`.

## The engine

`tg-core`, `tg-capi`, `tg-move` from
[schwung-trance-gate](https://github.com/graebe/schwung-trance-gate),
© 2026 Torben Gräber. They have no dependencies of their own.

They were relicensed from MIT to GPL-3.0-or-later when this build moved to
nih-plug. That reason is gone: nothing copyleft links them any more, and the
same crates ship inside the MIT Piano Practice module on the Move. **Taking
them back to MIT is the remaining step**, and it is a decision for the engine
repository rather than this one.

## JUCE — while `plugins/trance-gate` still builds

The JUCE 8 modules are **AGPLv3**-or-commercial. AGPLv3 is a *stronger*
obligation than this project's GPL-3.0-or-later: any artefact built from the
JUCE target must be conveyed under AGPLv3, not GPLv3. The JUCE target is being
retired for exactly this reason. JUCE's own bundled dependencies (AudioUnit
SDK and Oboe under Apache-2.0, the VST3 SDK under **MIT** © 2025 Steinberg
Media Technologies GmbH, ASIO under Steinberg's proprietary terms) are listed
in `LICENSE.md` in the JUCE checkout.

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
