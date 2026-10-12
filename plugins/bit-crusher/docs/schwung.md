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
./modules/_shared/package.sh bit-crusher   # cross-builds in Docker, makes the tarball
./modules/_shared/install.sh bit-crusher   # scp to move.local
```

The module id is `ni-bit-crusher`. Put it in a chain's audio effect slot.

## The knobs

Two pages. The first, eight knobs — the crusher:

| knob | what it does | range |
|---|---|---|
| **Bits** | how many steps: 2 to the power of Bits | 1 – 16 |
| **Rate** | how often a level is taken | 250 Hz – 48 kHz, in twelve steps |
| **Drive** | gain into the crusher; past full scale the top step clips | ±24 dB |
| **Shape** | from even steps to µ-law, fine near silence | 0 – 100 % |
| **Jitter** | an unsteady hold clock: noisier, and wider | 0 – 100 % |
| **Dither** | noise of up to a step before the steps | 0 – 100 % |
| **Mix** | dry against crushed | 0 – 100 % |
| **Output** | the level after the mix | ±24 dB |

The second page is the **Filter**, the same low-pass as the plugin's, after
the crusher and before Mix:

| knob | what it does | range |
|---|---|---|
| **Filter** | on or off — off by default, as in the plugin | Off / On |
| **Cutoff** | where it starts to cut | 100 Hz – 20 kHz, in twelve steps |
| **Peak** | a resonant rise at the cutoff; never self-oscillating | 0 – +24 dB |
| **Slope** | how steeply it cuts | 12, 24 or 48 dB/oct |

The plugin's manual explains each one, with recipes: they work here the same
way, with the switches off.

**Cutoff is a list**, for the same reason as Rate: 100, 200, 300, 500, 800,
1.2k, 2k, 3k, 5k, 8k, 12k and 20k — even steps of pitch, roughly, for a knob
that turns in even steps.

## What is different here

**Rate is a list.** The plugin's Rate knob turns smoothly over 50 Hz to
96 kHz in equal ratios; a Move knob turns in even steps, which would spend most
of its travel above 10 kHz. So Rate offers twelve rates — 250, 500, 1k, 2k, 4k,
6k, 8k, 12k, 16k, 24k, 32k and 48k — and 48k, the Move's own rate, is no hold
at all.

**No switches.** Mid-Rise, Anti-Alias and the Pre and Post filters stay off: hard
steps, hard holds on the Move's own 44.1 kHz grid — the classic, raw digital
crusher, and the one a hardware box with no screen for a staircase is best
at. Like the plugin, the module runs 32 samples (0.7 ms) late.

**Sixteen bits are the ceiling, as they are in the plugin.** The Move's audio
is 16-bit, so that is also where Bits stops changing anything.

**The panic.** The Move's panic reaches a module as MIDI — All Sound Off or
All Notes Off. Either one resets the crusher: the held sample lets go and the
next block starts from silence.
