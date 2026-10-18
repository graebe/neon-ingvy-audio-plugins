---
section: schwung
title: On the Ableton Move
---

The same Rust engine as the plugin, wrapped in Schwung's `audio_fx` vtable
instead of the VST3 shell. One core, two shells: a test renders two seconds
through both paths and holds them within a sixteen-bit step and a half.

## Install

```sh
./modules/_shared/package.sh notch   # cross-builds in Docker, makes the tarball
./modules/_shared/install.sh notch   # onto move.local
```

The module id is `ni-notch`. Put it in a chain's audio effect slot.

## The knobs

Everything the plugin has but its analyzer, which is a picture the Move has
no screen for, by the plugin's names:

| encoder | range |
|---|---|
| **Freq** | third-octave steps, 20 Hz to 20 kHz |
| **Q** | 0.3 to 16, in twelve steps |
| **Slope** | 6 to 48 dB/oct |
| **Family** | five approximations |
| **Ripple** | 0.01 to 3 dB |
| **Stopband** | 20 to 120 dB |
| **Channel** | Stereo, Left, Right, Mid, Side |
| **Output** | ±24 dB |

**Freq and Q step.** A knob that turned evenly would spend most of its travel
above a few kHz, so Freq moves in third-octaves (20, 25, 31.5 ... 16k, 20k) and
Q in twelve steps; each starts where the plugin does. Ranges and defaults
are the plugin's: a notch set up on the Move sounds as the same
settings do in Live.

## The panic

The Move's panic (and any **All Sound Off** or **All Notes Off**, CC 120 or
CC 123 on any channel) empties the filter at once: the next block starts from
silence, the settings stay.
