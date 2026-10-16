---
section: schwung
title: On the Ableton Move
---

The same Rust engine as the plugin, wrapped in Schwung's `audio_fx` vtable
instead of the VST3 shell. One core, two shells: a test renders four seconds
through both paths and compares them sample for sample.

## Install

```sh
./modules/_shared/package.sh filter-designer   # cross-builds in Docker, makes the tarball
./modules/_shared/install.sh filter-designer   # onto move.local
```

The module id is `ni-filter-designer`. Put it in a chain's audio effect slot.

## The parameters

The Move has no display for a response curve, so the module offers the
isolator, a high-pass and a low-pass on two pages of eight encoders.

**Page 1: the isolator** (switched on at start).

| encoder | parameter |
|---|---|
| **Low**, **Mid**, **High** | each band's level, -60 dB (muted: the kill) to +6 dB |
| **Low-Mid** | the low-mid crossover frequency, 60 Hz to 1.5 kHz |
| **Mid-High** | the mid-high crossover frequency, 1 to 16 kHz |
| **Slope** | the Linkwitz-Riley crossover order: LR4 24 dB/oct or LR8 48 dB/oct |
| **Isolator** | the isolator in or out of the signal path |
| **Output** | the output gain |

**Page 2: high-pass and low-pass** (Butterworth, bypassed at start).

| encoder | parameter |
|---|---|
| **HP On**, **HP Freq**, **HP Slope**, **HP Q** | the high-pass: in or out, -3 dB cutoff (20 Hz to 1 kHz), roll-off (6 to 48 dB/oct), Q of the highest-Q pole pair |
| **LP On**, **LP Freq**, **LP Slope**, **LP Q** | the low-pass, cutoff 1 to 18 kHz |

The frequencies are discrete steps: a Move encoder is linear, and twelve
logarithmically spaced values per encoder cover the useful range.

## The panic

CC 120 (All Sound Off) or CC 123 (All Notes Off) resets every filter's state;
the next block starts from silence.
