---
section: live
title: Ableton Live Interface
---

The window is one picture over one panel.

- **Response**, across the top: the filter's curve -- what the signal
  becomes -- over the spectrum of the signal.
- **Low Shelf**: every control the filter has, and **Bypass**.

## Part 1 — what NI Low Shelf does

**Gain** is how far the shelf lifts or lowers the lows (±30 dB); **Freq** is
the middle of the transition, where the gain is half. **Slope** is how steep
the transition is: 6 to 24 dB/oct.

**Q** shapes the corner: 0.71 is the smooth shelf; above it the shelf
overshoots -- a small dip above the boost, a bump above a cut -- the
"Pultec" shape many engineers reach for.

Drag the dot: sideways is **Freq**, up and down is **Gain**; hold **Alt** as you press for **Q**. Hold **Shift** for fine moves; double-click the dot to
put it back where a new instance starts.

## Part 2 — the controls

| control | range | what it is |
|---|---|---|
| **Freq** | 10 Hz to 22 kHz | the transition's middle |
| **Gain** | ±30 dB | the shelf's level |
| **Q** | 0.1 to 40 | the corner; above 0.71 it overshoots |
| **Slope** | 6 to 24 dB/oct | the transition's steepness |
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

### Weight

**Freq** 120 Hz, **Gain** +3 dB, **Slope** 12 dB/oct under a mix.

### Mud out of the sides

**Channel** Side, **Freq** 200 Hz, **Gain** -6 dB: the lows stay in the
middle, the width above stays.

### The overshoot

**Gain** +4 dB, **Q** 1.4: the boost and a dip just above it, tightening the
low mids as it lifts the lows.
