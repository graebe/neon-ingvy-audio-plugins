---
section: live
title: Ableton Live Interface
---

The window is one picture over one panel.

- **Response**, across the top: the filter's curve -- what the signal
  becomes -- over the spectrum of the signal.
- **All Pass**: every control the filter has, and **Bypass**.

## Part 1 — what NI All Pass does

Because the level does not change, the picture draws the **phase** instead:
0° at the bottom of the spectrum, turning through the scale at the right
(±180°, wrapping round). A 2nd-order all-pass turns half a cycle (-180°)
exactly at **Freq** and a whole one by the top.

**Order** is how far the phase turns: each order adds half a cycle (180°) of
turn by the top (1st to 8th order). **Q** sharpens the turn of the steepest section: a
higher Q turns the phase faster close to Freq.

Drag the dot: sideways is **Freq**, up and down is **Q**. Hold **Shift** for fine moves; double-click the dot to
put it back where a new instance starts.

## Part 2 — the controls

| control | range | what it is |
|---|---|---|
| **Freq** | 10 Hz to 22 kHz | where the phase turns |
| **Q** | 0.1 to 40 | how fast it turns there (2nd order and up) |
| **Order** | 1st to 8th | how far it turns |
| **Channel** | five | what part of the stereo signal (below) |
| **Output** | ±24 dB | the gain after the filter |

A control the filter has but does not use at the moment is drawn dimmed, never
hidden, so nothing in the window moves.

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

### Align a kick in and a kick out

On the outside mic: sweep **Freq** between 50 and 200 Hz while listening to
both together until the low end is fullest.

### Softer peaks

**Order** 4th, **Freq** 1 kHz on a vocal bus: the waveform's peaks spread
out and the level reads lower, while it sounds the same.

### Hear the phase

Put NI All Pass on a copy of a track, **Freq** 500 Hz, and mix it with the
original: the band where they cancel moves with Freq -- a phaser by hand.
