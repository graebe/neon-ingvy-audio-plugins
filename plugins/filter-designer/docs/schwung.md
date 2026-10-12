---
section: schwung
title: On the Ableton Move
---

The same Rust engine as the plugin, wrapped in Schwung's `audio_fx` vtable
instead of the VST3 shell. Not a port and not a reimplementation: one core,
two shells, with a test that renders four seconds through both paths and
compares them sample for sample.

## Install

```sh
./modules/_shared/package.sh filter-designer   # cross-builds in Docker, makes the tarball
./modules/_shared/install.sh filter-designer   # onto move.local
```

The module id is `ni-filter-designer`. Put it in a chain's audio effect slot.

## The knobs

The Move has no screen to draw a curve on, so the module is what a set
played live reaches for: the isolator and two cuts, on two pages of eight
knobs.

**Page 1, the isolator** — on from the start.

| knob | what it does |
|---|---|
| **Low**, **Mid**, **High** | each band from -60 dB (gone) to +6 dB: turn one all the way down to kill it |
| **Lower** | the crossover between Low and Mid, 60 Hz to 1.5 kHz |
| **Upper** | the crossover between Mid and High, 1 to 16 kHz |
| **Slope** | how sharp the split is: 24 or 48 dB an octave |
| **Isolator** | the isolator on or off |
| **Output** | the level after everything |

**Page 2, the cuts** — off until you switch them on.

| knob | what it does |
|---|---|
| **LC On**, **LC Freq**, **LC Order**, **LC Res** | a Low Cut (Butterworth): on, where, how steep (6 to 48 dB), and a peak at the corner |
| **HC On**, **HC Freq**, **HC Order**, **HC Res** | the same for a High Cut |

The frequencies are steps, not a sweep: a Move knob turns evenly, and twelve
steps where they matter beat a knob that spends most of its turn above
10 kHz.

## The panic

The Move's panic (CC 120 or 123) empties every filter at once: the next
block starts from silence.
