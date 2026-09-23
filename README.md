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

The plugin is **GPLv3** — JUCE and the VST3 SDK are GPLv3-or-commercial, and
this satisfies both. The shared engine
([schwung-trance-gate](https://github.com/graebe/schwung-trance-gate)) stays
**MIT**: the GPL flows to the plugin binary, not back into the engine.

The bundled typeface is **JetBrains Mono**, © 2020 The JetBrains Mono Project
Authors, under the **SIL Open Font License 1.1** — the licence text ships
beside the font files in `plugins/trance-gate/Resources/OFL.txt`, which is
what the OFL requires of anything that redistributes them.
