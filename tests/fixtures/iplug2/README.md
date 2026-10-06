<!--
SPDX-License-Identifier: GPL-3.0-or-later
Copyright (C) 2026 Torben Gräber
-->
# The iPlug2 VST3 builds' saved state

These files are what the last iPlug2 builds (v2026.10.06.5) hand to a VST3
host. That is what every Live set that uses them contains. The JUCE builds
that replace iPlug2 must open those sets with the same settings, and these
files are the test of that. [FORMAT.md](FORMAT.md) is the byte layout, with
references to the source.

`tests/vst3_capture.mm` wrote them. It asked the shipped bundles through the
calls Live makes. Nothing here was written by hand.

## The files

| File | What it is |
|---|---|
| `ids.json` | Each plugin's VST3 class as its factory reports it. It gives the class ID's bytes (`tuidBytesMacOS`, read back from the factory), the same ID on Windows (`tuidBytesWindows`, computed from the SDK's `INLINE_UID`), the class flags (`kSimpleModeSupported`: single component), `getControllerClassId` (`kNotImplemented`), the factory's classes, and whether the bundle has a `moduleinfo.json` (none does). |
| `<product>/parameters.json` | Every `ParameterInfo` in the host's order, MIDI CCs included: id, title, short title, units, stepCount, default (normalized), unit and flags. iPlug2 lists Bypass (65536) **first**. |
| `<product>/<scenario>.component.bin` | `IComponent::getState`: the exact bytes Live stores. |
| `<product>/<scenario>.controller.bin` | `IEditController::getState`: **empty for every product.** iPlug2 is a single component, and the SDK routes that call to an empty `getEditorState` (FORMAT.md). It is kept so that a test can hand a JUCE build exactly what Live would. |
| `<product>/<scenario>.json` | What the scenario did, the component state decoded per FORMAT.md (header, parameters, strings, bypass), and what a host reads back for each parameter afterwards (`values`: id, normalized, plain, display string), the MIDI CCs excepted. `reload` records a second instance loading the state: same parameters, same chunk, and the bypass it saves after the load. |

## The scenarios

Each scenario starts from a fresh instance at 48 kHz, processing 512-sample
blocks under a transport playing at 120 BPM. A value is set the way a DAW sets
one: the controller is told (`setParamNormalized`), and the processor gets it
in the next block's parameter changes. The plugin's own `performEdit`s are fed
back the same way. A pattern, a Spectrogram session and a Listen-In name are
not parameters, so they are sent through the plugin's real editor, as the
page's own messages. A user has no other way to set them.

| Product | Scenario | Holds |
|---|---|---|
| NITranceGate | `default` | nothing touched: slot 1, 16 steps, 1/16 |
| | `slots` | three slots, each with its own sound and pattern, and **slot 2 current**. Slot 1: 1/8, Amount 80, Attack 5, Decay 30, Sustain 60, Release 25, Exponential, a drawn 16-step pattern with a tie and two accents. Slot 2: 32 steps, Width 50, Join Neighbors, Fade 50 Soft Out, a pattern rolled with seed 2202. Slot 3: 12 steps, 1/16T, Env Time %, S-Curve, Sustain 40, Decay 60, a drawn pattern with a tie and a moved arrival. |
| | `bypassed` | the defaults with the host's Bypass on: the trailing int32 is 1 |
| NISideChain | `default` | nothing touched |
| | `custom` | all fifteen parameters away from their defaults (see `custom.json`) |
| NISpectrogram | `default` | nothing touched |
| | `session` | buses 2 and 5 beside this track, the clash comparing them, the Bass zoom |
| NIListenIn | `default` | bus 1, no name |
| | `labelled` | bus 3, named `Bässe & Kick` (UTF-8) |

## What the capture checks

A capture fails if any of these do not hold:

- the decoder accounts for every byte;
- each scenario's own expectations hold (for example, the Trance Gate's blob
  has slot 1's tie and slot 3's moved arrival);
- the host's reading of each parameter equals the double in the chunk;
- a second instance that loads the state reports the same parameters and
  saves the same chunk.

`ctest -R iplug2_fixtures`, in the full tier, loads the committed states into
the current build. Every parameter must read what was captured. It does not
compare bytes, because a later engine may write the same patch differently.
It runs while the iPlug2 builds exist. Once they are gone, the JUCE builds'
own fixture tests take over.

## Regenerating

Only regenerate on purpose. These files stand for what users' sets hold, and
a later build that writes something else does not change those sets. Add a
new scenario rather than rewriting an old one. The capture opens editors, so
it needs a logged-in macOS session.

```sh
cmake -S . -B build-spike -DCMAKE_BUILD_TYPE=Release -DIPLUG_DEPLOY_PLUGINS=OFF
cmake --build build-spike -j
build-spike/tests/vst3_capture tests/fixtures/iplug2 \
    build-spike/out/NITranceGate.vst3 build-spike/out/NISideChain.vst3 \
    build-spike/out/NISpectrogram.vst3 build-spike/out/NIListenIn.vst3
```

The capture is deterministic: two runs write identical files. After iPlug2 is
removed, these can only be regenerated from a commit that still has it. The
fixtures were captured at 52ac83c, on top of v2026.10.06.5 (488ad46).
