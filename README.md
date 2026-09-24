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

Copyright © 2026 Torben Gräber.

**The iPlug2 build is permissive; the JUCE one is not, and that is the only
thing left.** `plugins/trance-gate-iplug` links iPlug2 (**zlib**), the VST3
SDK iPlug2 vendors (**MIT**, © 2026 Steinberg — the GPL-or-commercial dual
licence was withdrawn) and the shared engine. Nothing in that chain is
copyleft.

`plugins/trance-gate` links **JUCE 8**, which is **AGPLv3**-or-commercial, and
that is a *stronger* obligation than GPL rather than an equal one: while that
target ships in a build, the artefact must be conveyed under AGPLv3.

So the remaining work to make this repository MIT is not a licensing decision,
it is a product one — **retiring the JUCE target means retiring its 2,586-line
editor**, and the iPlug2 build currently draws nothing. See
[THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).

### What is already gone

The Rust plugin (`crates/tg-plugin`) and its `xtask` bundler have been removed.
nih-plug itself is ISC, but `nih_export_vst3!()` pulled in
[`vst3-sys`](https://github.com/RustAudio/vst3-sys), which is
GPL-3.0-or-later — the single crate that made that build copyleft. iPlug2
reaches VST3 through Steinberg's own MIT SDK instead, so the format now costs
nothing. The engine crates were relicensed from MIT to GPL when this build
moved to nih-plug; nothing copyleft links them any more, so taking them back
to MIT is the last loose end.
