---
section: live
title: Ableton Live Interface
---

The window is one picture over one panel.

- **Response**, across the top: the filter's curve -- what the signal
  becomes -- over the spectrum of the signal.
- **Band Pass**: every control the filter has, and **Bypass**.

## Part 1 — what NI Band Pass does

**Freq** is the centre of the band, where the filter passes at 0 dB. **Q** is
the centre over the bandwidth between the two -3 dB edges: Q 1 is a band about
1.4 octaves wide, Q 4 a third of an octave. The two edges show on the picture
as ticks; drag one to set Q.

**Slope** sets how fast each side falls away beyond the edges: 6 dB per
octave per order on each side.

Drag the dot: sideways is **Freq**, up and down is **Q**; or drag one of the two edge ticks to the width you want. Hold **Shift** for fine moves; double-click the dot to
put it back where a new instance starts.

## Part 2 — the controls

| control | range | what it is |
|---|---|---|
| **Freq** | 10 Hz to 22 kHz | the centre |
| **Q** | 0.1 to 40 | the centre over the -3 dB bandwidth |
| **Slope** | 6 to 48 dB/oct | each side's roll-off |
| **Family** | five | the approximation (below) |
| **Ripple** | 0.01 to 3 dB | passband ripple: Chebyshev I, elliptic |
| **Stopband** | 20 to 120 dB | stopband depth: Chebyshev II, elliptic |
| **Channel** | five | what part of the stereo signal (below) |
| **Output** | ±24 dB | the gain after the filter |

A control the filter has but does not use at the moment is drawn dimmed, never
hidden, so nothing in the window moves.

### Family: the approximation

The family is the analog prototype the filter is designed from. Each trades
the sharpness of the corner against the flatness of the passband and the
behaviour in time:

| family | passband | the corner | in time |
|---|---|---|---|
| **Butterworth** | maximally flat | gentle | a little ringing at high orders |
| **Bessel** | flat, gently drooping | the softest | no overshoot: the best transients |
| **Chebyshev I** | ripples by **Ripple** dB | steeper | rings more |
| **Chebyshev II** | flat | steep, with notches in the stopband no shallower than **Stopband** dB | rings more |
| **Elliptic** | ripples by **Ripple** dB | the steepest at any order | rings most |

**Ripple** and **Stopband** light up only for the families that use them.

A Linkwitz-Riley band-pass would play as a Butterworth one, so it is not
offered here.

## Part 3 — the rest of the window

### Channel

Which part of a stereo signal the filter works on:

| Channel | filters | leaves |
|---|---|---|
| **Stereo** | left and right alike | nothing |
| **Left**, **Right** | one side | the other |
| **Mid** | (L + R) / 2, what a mono sum hears | the side |
| **Side** | (L - R) / 2, the width | the mid |

The picture and the analyzer follow the channel: a **Side** filter is drawn,
and its spectrum read, on the side alone (the caption names the lane). On a
mono track there is no side: **Right** and **Side** do nothing.

### Output and Bypass

**Output** (±24 dB) is a gain after the filter; the curve includes it.
**Bypass** is Live's own device switch too: it crossfades over 10 ms, and for
two seconds the filter keeps running on the music underneath, so switching it
back on is seamless. After that it rests and costs nothing.

### The picture

10 Hz to 22 kHz across, in equal steps of pitch. The click targets in the
caption's right end set the **analyzer** (pre + post, post, pre, off) and the
**range** (±6, ±12, ±18, ±30 dB). The analyzer shows the spectrum before the
filter faded behind, and after it highlighted in front, on its own scale
(-6 to -84 dBFS); it runs only while the window is open. One amber line marks
where the sample rate limits the filter (near Nyquist).

The plugin adds no latency. Live receives the filter's tail -- how long it
rings after the input stops -- when it sets the device up, and automation
lands once per audio block: both are limits of the plugin framework, as
NI Filter Designer's manual explains.

## Examples

### Telephone voice

**Freq** 1.5 kHz, **Q** 0.8, **Slope** 24 dB/oct. Then nudge **Output** up
to make up the level the band lost.

### Find a snare's crack

**Freq** 4 kHz, **Q** 3, then sweep **Freq** while the loop plays: the band
you hear alone is what a bell would boost.

### A narrow, ringing band

Chebyshev I, **Ripple** 1 dB, **Q** 8: a resonant, vocal tone from noise or
a pad.
