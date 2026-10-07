---
title: NI Spectrogram
tagline: A rolling STFT analyzer — 10 Hz to 20 kHz, 256 log bands, thirteen seconds.
order: 2
hosts: [live]
formats: [VST3]
engine: engines/spectro
crates: [spectro-core, spectro-capi]
tests: [spectro_core, spectro_columns, spectro_ramp, spectro_state, sg_processor, sg_host]
notOnMove: >-
  The Spectrogram draws a picture and the Move has no screen to draw it on.
  That is also why `engines/spectro` has no `tg-move` counterpart — a crate
  belongs to exactly one product, and this product has one shell.
still: media/spectrogram/live.png
harness: harness/spectrogram/
---

# NI Spectrogram

A rolling spectrogram: log frequency from **10 Hz to 20 kHz** on the vertical,
256 bands — about two per semitone — time scrolling right to left, **thirteen
seconds** of history. Audio passes through **bit for bit**: it is an analyzer,
and its whole output is the picture. A universal macOS VST3 with a native
editor.

**No parameters**, and that is a statement rather than an omission: nothing about
it changes what comes out — Pause included, which is a property of the picture
and not of the audio. What you set up in it — which channels you are viewing,
what you compare, the clash marking and the zoom — is saved with your Live set
instead, and is there again when you close and reopen the window or reload the
project.

## 10 Hz is a window length, not a setting

An FFT's bins are `sample_rate / fft_size` apart, and an axis cannot start below
its first bin — so "start at 10 Hz" fixes the window at 8192 points at 48 kHz
(bins 5.9 Hz apart), and 16384 at 96 kHz. The engine picks it from the rate
rather than carrying a constant; `spectro_pick_fft_size` is that rule, and the
hop beside it is tied to a **column rate** instead of to the window, so the
picture holds the same thirteen seconds whatever the session runs at.

This was worth learning the hard way: the first version asked for 20 Hz with a
1024-point window, and the axis was silently clamped up to **47 Hz** — a
spectrogram starting an octave high still looks like a spectrogram.
`ctest -R spectro_columns` now fails if the axis does not start where it was
asked to.

The cost is time resolution: a 171 ms window smears a transient across a sixth
of a second. That is what a long window *is*, not a defect — the way out is a
multi-resolution analysis, not a shorter window. And below about 35 Hz there are
only a handful of bins, so the bottom two octaves read as bands of repeated
value: that is what the analysis actually knows.

## Where the analysis runs

On **a thread of its own**, not on Live's audio thread. The audio thread only
copies each block into a ring and goes on; a worker beside it does the
transforms, every few milliseconds, at a priority below the interface and
above background work. So a Spectrogram — even one reading three Listen-In buses
— costs the audio thread almost nothing, and the transforms never hold it up.

The analysis keeps running while the window is closed, so a window you open
mid-song already holds the last thirteen seconds. Nothing allocates once the
analysis is configured, and a counting allocator in `engines/spectro` fails the
build if that stops being true.

## The colour is the design system's, extended in one place

The system's rule is that its saturated violet is never a fill — it is the halo
around a near-white core — which a field of light cannot obey literally. So the
rule is honoured spatially instead: quiet is the ground, loud is the near-white
core, and the violet is the falloff between them. Look at one bright partial and
you are looking at exactly the system's white core in a violet halo, drawn in
pixels rather than in a box-shadow.

The five stops are `--spec-0..4` in `ui-kit/src/tokens.css` and nowhere else, and
`ctest -R spectro_ramp` fails if the ramp ever dips in luminance — a ramp that
dips is a picture that lies about level.
