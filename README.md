# vst-library

Plugin builds around engines kept separate from the shells that host them.

An engine with a **second consumer is a submodule, not a copy**. The Trance
Gate's DSP is the same Rust crate Schwung compiles into the Move module, pinned
by commit — a second copy would drift, and the symptom would be "it sounds
different in Live", which is the hardest kind of bug to chase. An engine with
only one consumer so far lives in `engines/` and moves out the day it gains a
second: the Spectrogram's analyzer is there, because the Move has no screen to
draw a spectrogram on.

| plugin | engine | what it is |
|---|---|---|
| [Trance Gate](#trance-gate) | `external/schwung-trance-gate` (submodule) | a tempo-locked step gate |
| [Spectrogram](#spectrogram) | `engines/spectro` (in-repo) | a rolling analyzer |

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

## Build

```bash
git submodule update --init --recursive
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
ctest --test-dir build
```

iPlug2's SDKs are downloaded rather than tracked; a fresh clone needs
`external/iPlug2/Dependencies/IPlug/download-vst3-sdk.sh` and
`download-clap-sdks.sh` before the first configure, or CMake stops on a
non-existent include path in `iPlug2::VST3`.

Artefacts land in `build/out/` and are copied to `~/Library/Audio/Plug-Ins/`.

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

