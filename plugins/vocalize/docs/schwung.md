---
section: schwung
title: On the Ableton Move
---

The same Rust engine as the plugin, wrapped in Schwung's `audio_fx` vtable
instead of the VST3 shell. Not a port and not a reimplementation — one core,
two shells, with a test that renders four seconds through both paths and
compares them sample for sample.

## Install

```sh
./modules/_shared/package.sh vocalize   # cross-builds in Docker, makes the tarball
./modules/_shared/install.sh vocalize   # scp to move.local
```

The module id is `ni-vocalize`. Put it in a chain's audio effect slot.

## The knobs

One page of seven:

| knob | what it does | range |
|---|---|---|
| **Vocal** | how far the sound has become the vowel | 0 – 100 % |
| **Vowel** | 0 A, 1 E, 2 I, 3 O, 4 U, and the way between | 0 – 4 |
| **Size** | the voice: up bigger and lower, down smaller and higher | ±100 % |
| **Resonance** | the formants broad and breathy, or sharp and whistling | 0 – 100 % |
| **Excite** | harmonics for pure sounds, so the vowel has something to shape | 0 – 100 % |
| **Mix** | dry against vowel | 0 – 100 % |
| **Output** | the level after the mix | ±24 dB |

The plugin's manual explains each one, with recipes: they work here the same
way, in the plugin's Time mode.

## What is different here

**The Time engine only.** The Move plays live, and the Time engine has no
latency; the module is built without the Spectral engine, which also keeps
the FFT out of it. A Mode saved in a preset is read and changes nothing.

**Vowel is a number.** The knob turns from 0 to 4 and the screen shows the
number: 0 is A, 1 E, 2 I, 3 O and 4 U, and 1.5 is halfway from E to I.

**The panic.** The Move's panic reaches a module as MIDI — All Sound Off or
All Notes Off. Either one resets Vocalize: the history and the resonators let
go, and the next block starts from silence.
