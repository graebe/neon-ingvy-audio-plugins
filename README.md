# vst-library

Plugin builds around engines that live elsewhere.

The engines are **submodules, not copies**. The Trance Gate's DSP is the same
C file Schwung compiles into the Move module, pinned by commit — a second copy
would drift, and the symptom would be "it sounds different in Live", which is
the hardest kind of bug to chase.

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

## Build

```bash
git submodule update --init --recursive
cmake -B build -DCMAKE_BUILD_TYPE=Release   # fetches JUCE 8.0.15
cmake --build build -j8
ctest --test-dir build
```

Artefacts land in `build/plugins/trance-gate/TranceGate_artefacts/Release/`
and are copied to `~/Library/Audio/Plug-Ins/`.

## Licence

**MIT**, © 2026 Torben Gräber — every part of it, with nothing copyleft in the
chain.

| | |
|---|---|
| this repository | **MIT** |
| [iPlug2](https://github.com/iPlug2/iPlug2) | **zlib**, with WDL/NanoVG/NanoSVG (Zlib) and MetalNanoVG/RTAudio (MIT) |
| VST3 SDK | **MIT**, © 2026 Steinberg Media Technologies GmbH |
| CLAP | **MIT** |
| [the engine](https://github.com/graebe/schwung-trance-gate) | **MIT**, and it has no external crates at all |

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

