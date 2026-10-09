---
title: NI Bit-Crusher
tagline: A signal made discrete — fewer levels, taken less often — and you can watch the steps.
order: 6
hosts: [live, move]
formats: [VST3]
engine: engines/bit-crusher
crates: [bc-core, bc-capi, bc-move]
tests: [bc_core_rs, bc_core, bc_render_ab, bc_processor, bc_rt, bc_host]
---

# NI Bit-Crusher

A bit crusher takes a smooth, continuous signal and makes it **discrete**,
twice over:

- **in amplitude** — it keeps fewer levels. A CD keeps 65 536 of them
  (16 bits); this keeps as few as two or three, so a smooth wave becomes a
  staircase.
- **in time** — it keeps the level less often. Instead of 44 100 or 48 000
  times a second, it takes one and *holds* it, a few thousand times a second
  or fewer, so the staircase gets flat tops.

Both are what made early samplers, game consoles and telephones sound the way
they did. A VST3 for macOS (Linux and Windows follow) with a native editor,
plus a Schwung module for the Ableton Move.

## What it does

**Bits** sets how many levels there are: 2 to the power of Bits. It is a
continuous control — 7.5 bits is a real setting — so sweeping it is a smooth
ride down the staircase rather than a jump from 256 steps to 128.

**Rate** sets how often a level is taken. It does not have to divide your
project's sample rate: 12 kHz at 44.1 kHz holds for three samples and four in
the proportion that averages 3.675, and the clock never drifts.

Around those two, everything a crusher can usefully do:

| control | what it does |
|---|---|
| **Drive** | gain into the crusher — pushes a quiet signal up the staircase; past full scale the top step is a hard clip |
| **Shape** | bends the steps: from even (0 %) to µ-law (100 %), fine near silence and coarse near full scale, so quiet detail survives |
| **Dither** | triangular noise of up to a step, added before the steps: it trades the gritty, signal-following distortion for an even hiss |
| **Mid-Rise** | moves the grid half a step, so there is no level at zero and silence becomes a buzz |
| **Anti-Alias** | smooths each step's edge (first-order ADAA), so the staircase folds back less |
| **Jitter** | lets the hold's clock wander — noisier, and wider in stereo |
| **Pre** | a low-pass at half the Rate *before* the hold: takes out what would fold back |
| **Post** and **Octave** | a low-pass *after* the hold, at half the Rate moved by Octave: smooths the hold's steps |
| **Mix**, **Output** | dry against crushed, and the level after |

Every control glides, the switches included, so automating any of them does
not click. There is no latency.

## The engine

`engines/bit-crusher` is Rust, shared by both shells:

| crate | what it is |
|---|---|
| `bc-core` | the DSP: the staircase and its antiderivative, the hold, the filters (`biquad`), the parameter table and its text |
| `bc-capi` | the C ABI the plugin links (`bc_core.h`, `bc_shell.h`, written by cbindgen) |
| `bc-move` | the Schwung `audio_fx` vtable |

The parameter table is the one source of truth: the plugin declares its
parameters from it, the Move module's `chain_params` is tested against it, and
the text every readout shows is the engine's.

## Tested

| test | what it holds |
|---|---|
| `bc_core_rs` | the engine's own claims: the grid's levels and error, the antiderivative, the hold's clock, the filters, the glides, the panic, no allocation on any audio path, and that Anti-Alias takes out most of the aliasing (measured with an FFT) |
| `bc_core` | the generated C headers, from C |
| `bc_render_ab` | the plugin's float path and the Move's int16 path render the same patch within half a sixteen-bit step, and the render's hash is pinned |
| `bc_processor`, `bc_rt`, `bc_host` | the processor on the real engine, its audio callback under an allocation guard, and the built VST3 as a DAW hosts it — the engine's golden render, bit for bit |

Licence: GPL-3.0-or-later, © 2026 Torben Gräber.
