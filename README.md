# vst-library

Audio plugins and a Schwung module, built from shared Rust engines and one
Solid UI kit. A monorepo: everything that ships from here is in here.

| product | ships as | engine |
|---|---|---|
| [Trance Gate](#trance-gate) | VST3 · AU · CLAP · a Schwung module for the Move | `engines/trance-gate` |
| [Spectrogram](#spectrogram) | VST3 · AU · CLAP | `engines/spectro` |

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
design/files/                the Ultraviolet design system, vendored
versions.json                one version per product
```

## Build

```sh
git submodule update --init --recursive   # iPlug2. The engines are subtrees.
npm ci                                    # the kit and both editors
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build                       # the macOS plugins, universal
ctest --test-dir build                    # 16 tests
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
| `spectro_wire`, `spectro_columns_js` | the wire format the editor decodes, both sides pinned to one table the plugin's own C++ generates |
| `tg_wire` | the four pieces of plugin arithmetic where being wrong is silent — the scope quantiser, the message split, the editor height, the transport advance |

### Coverage

```sh
./scripts/coverage.sh
```

One command, three languages. `build/coverage/` gets `coverage.json` for a
machine, `summary.txt` and `html/index.html` for a person, and `lcov.info` for
an editor's gutter. It builds into `build-coverage/` and never into `build/`:
the instrumented build is `-O0`, one architecture and has no `NDEBUG`, so
nothing from it can be shipped by mistake.

**One engine for all three languages.** Rust compiles through LLVM, so the same
`-fprofile-instr-generate` instrumentation and the same `llvm-cov` reader serve
the C, the C++ and both cargo suites; node's own `--experimental-test-coverage`
covers the editors without adding a dependency to a tree that is kept small for
the licence audit. It needs `cargo install cargo-llvm-cov` and
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
— and the exemptions. An exemption must name a unit that still exists and carry
a reason rather than a note, and a test enforces both: a stale excuse is how a
floor quietly stops meaning anything. Floors apply to units **derived from
paths**, so a plugin arriving in this repository is measured on arrival rather
than being silently absent from the denominator.

It is report-only for now — the plugin shells are exempt because what is left in
them after `Wire.cpp` was lifted out is calls into iPlug2 that only `tg_au` can
exercise. `"enforcing": true` makes a shortfall fail.

## Trance Gate

A tempo-locked step gate: rhythmic chopping locked to song position, per-step
ADSR, ties, per-step amount, 8 pattern slots. VST3 / AU / Standalone,
universal binary.

**It is the same engine as the Move module**, and that is asserted rather than
claimed: `tests/render_plugin.c` renders through the plugin's own audio path —
float, split channels, a DAW-shaped transport — and the result is
byte-for-byte identical to the module's reference render.

### Patch interchange

The plugin's saved state **is** the Move patch, verbatim. `Copy patch` puts it
on the clipboard; `Paste patch` reads one back. The same string moves a
pattern between the hardware and the DAW in either direction.

### Editing

Click a step to toggle it, shift-click for a tie, drag up/down on a step to set
its amount. The ring mirrors the Move display, playhead included.

### No automation in v1

Pattern, slots and macros live in the saved state and are edited here, the same
model the Move module uses. Exposing steps as host parameters means 32 × 8 =
520 of them, or 64 that get silently rewritten on every slot change — a
decision worth making with the thing in front of you rather than on paper.

## Spectrogram

A rolling spectrogram: log frequency from **10 Hz to 20 kHz** on the vertical,
256 bands — about two per semitone — time scrolling right to left, **thirteen
seconds** of history. Audio passes through **bit for bit**: it is an analyzer,
and its whole output is the picture. VST3 / AU / CLAP, universal binary.

**Pause holds the view, not the analysis.** The columns keep arriving and keep
filling the history behind the frozen picture, so letting go shows a view that
is already current — with the paused seconds *in* it, scrolled past, rather than
cut out of it. The plugin is never told; it is a repaint gate in the editor.

**A range dropdown zooms** — Full 10 Hz – 20 kHz, Sub 10 – 200 Hz, Bass
40 – 800 Hz, Mid 200 Hz – 4 kHz, High 2 – 20 kHz, overlapping on purpose. All
256 bands spread over whatever is chosen. It does **not** add bins: the window
is what decides those. What it does is give the ones that exist the whole
height — in Sub, ~33 bins crowded into the bottom 40 pixels become ~33 bins at 8
pixels each, which is what makes 50 Hz and 60 Hz two visibly different rows.
Higher up there are several bins to a band already and the zoom is detail in the
ordinary sense.

Changing it while audio runs takes no lock and allocates nothing:
`spectro_set_range` stores a request, the audio thread rebuilds its own band
table at the next frame, and columns measured against the old range are dropped
rather than drawn under the new scale.

**No parameters**, and that is a statement rather than an omission: nothing about
it changes what comes out — Pause included, which is a property of the picture
and not of the audio. Range and Speed will be ordinary host parameters when they
arrive.

### 10 Hz is a window length, not a setting

An FFT's bins are `sample_rate / fft_size` apart, and an axis cannot start below
its first bin — so "start at 10 Hz" fixes the window at 8192 points at 48 kHz
(bins 5.9 Hz apart), and 16384 at 96 kHz. The engine picks it from the rate
rather than carrying a constant; `spectro_pick_fft_size` is that rule, and the
hop beside it is tied to a **column rate** instead of to the window, so the
picture holds the same thirteen seconds whatever the session runs at.

This was worth learning the hard way: the first version asked for 20 Hz with a
1024-point window, and the axis was silently clamped up to **47 Hz** — a
spectrogram starting an octave high still looks like a spectrogram. `ctest -R
spectro_columns` now fails if the axis does not start where it was asked to.

The cost is time resolution: a 171 ms window smears a transient across a sixth
of a second. That is what a long window *is*, not a defect — the way out is a
multi-resolution analysis, not a shorter window. And below about 35 Hz there are
only a handful of bins, so the bottom two octaves read as bands of repeated
value: that is what the analysis actually knows.

The colour is the Ultraviolet design system's, extended in one documented place.
The system's rule is that its saturated violet is never a fill — it is the halo
around a near-white core — which a field of light cannot obey literally. So the
rule is honoured spatially instead: quiet is the ground, loud is the near-white
core, and the violet is the falloff between them. Look at one bright partial and
you are looking at exactly the system's white core in a violet halo, drawn in
pixels rather than in a box-shadow. The five stops are `--spec-0..4` in
`ui/src/uv.css` and nowhere else, and `ctest -R spectro_ramp` fails if the ramp
ever dips in luminance — a ramp that dips is a picture that lies about level.

### Where the FFT runs

On the **audio thread**, as each hop completes, with finished columns going into
a lock-free ring the editor drains at 60 Hz. Transforming on the message thread
instead would make the picture's time axis stretch and squeeze with the host's UI
load; here the columns are produced by the audio clock, and the only thing UI
jitter can do is make several arrive at once. Nothing allocates after
`spectro_configure`, and a counting allocator in `engines/spectro` fails the
build if that stops being true.

## Licence

**MIT**, © 2026 Torben Gräber — every part of it, with nothing copyleft in the
chain.

| | |
|---|---|
| this repository | **MIT** |
| [iPlug2](https://github.com/iPlug2/iPlug2) | **zlib**, with WDL/NanoVG/NanoSVG (Zlib) and MetalNanoVG/RTAudio (MIT) |
| VST3 SDK | **MIT**, © 2026 Steinberg Media Technologies GmbH |
| CLAP | **MIT** |
| [the Trance Gate engine](https://github.com/graebe/schwung-trance-gate) | **MIT**, and it has no external crates at all |
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

