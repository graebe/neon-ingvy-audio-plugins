---
section: live
title: Ableton Live Interface
---

The window is one picture over one panel.

- **Response**, across the top: the filter's curve -- what the signal
  becomes -- over the spectrum of the signal.
- **Low Pass**: every control the filter has, and **Bypass**.

## Part 1 — what NI Low Pass does

The filter passes the band below **Freq** untouched and falls away above it.
**Freq** is the -3 dB point (-6 dB for Linkwitz-Riley); **Slope** is how fast
it falls: 6 dB per octave per order, from 6 to 48 dB/oct.

**Q** lifts the corner into a resonant peak: it scales the Q of the filter's
sharpest pole pair, so 0.71 is the family's own response and 4 a pronounced
bump just below Freq -- the classic synth filter. A 6 dB/oct slope has no pole
pair, so Q is off there.

Drag the dot on the curve: sideways is **Freq**, up and down is **Q**. Hold **Shift** for fine moves; double-click the dot to
put it back where a new instance starts.

## Part 2 — the controls

| control | range | what it is |
|---|---|---|
| **Freq** | 10 Hz to 22 kHz | the -3 dB cutoff |
| **Q** | 0.1 to 40 | the corner's resonance (12 dB/oct and steeper) |
| **Slope** | 6 to 48 dB/oct | the roll-off |
| **Family** | six | the approximation (below) |
| **Ripple** | 0.01 to 3 dB | passband ripple: Chebyshev I, elliptic |
| **Stopband** | 20 to 120 dB | stopband depth: Chebyshev II, elliptic |
| **Matched** | off, on | the analog curve up to Nyquist |
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
| **Linkwitz-Riley** | flat; -6 dB at Freq | Butterworth squared | sums flat with its opposite cut: the crossover family; even slopes only |

**Ripple** and **Stopband** light up only for the families that use them.

### Matched

A digital filter designed by the bilinear transform squeezes its response
near the top of the band: a 16 kHz low-pass at 44.1 kHz falls much faster
than its analog original. **Matched** designs the filter to the analog
magnitude instead, so a high cutoff sounds as open as it reads.

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

### Take the edge off

**Freq** 12 kHz, **Slope** 12 dB/oct, **Matched** on. A gentle roll-off
for a bright cymbal or a harsh synth.

### Muffled, through a wall

**Freq** 400 Hz, **Slope** 24 dB/oct, Bessel. No ringing: the transients
stay soft and round.

### A filter sweep

**Slope** 24 dB/oct, **Q** 3, automate **Freq** from 200 Hz to 8 kHz over a
build-up.
