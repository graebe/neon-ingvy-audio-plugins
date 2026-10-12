---
section: live
title: Ableton Live Interface
---

The window is one picture over a row and two panels.

- **Response**, across the top, is what the filters do to your sound: every
  filter's curve, and the sum of them all in bright violet. Behind it, the
  music itself.
- Under it, **the row**: sixteen numbered cells, one per filter slot, and
  **ISO**, the isolator.
- Under that, the **Inspector** shows the filter you selected, and
  **Output** what applies to all of them.

## Part 1 — a filter is a curve

A filter lets some frequencies through louder and others quieter. Draw how
much louder or quieter at each frequency and you have its **response
curve**: frequency across, from the deepest bass at 10 Hz on the left to the
top of hearing at 22 kHz on the right, and level up and down, 0 dB in the
middle. A flat line on 0 dB is a filter that does nothing.

### Your first filter

Double-click anywhere in the picture. A dot appears where you clicked: a
**Bell**, a bump (or, below the middle, a dip) centred where you clicked.
Play something and drag the dot:

- **sideways** moves where it works: its **Freq**;
- **up and down** makes it louder or quieter there: its **Gain**;
- with **Alt** held, up and down makes it **narrower or wider**: its **Q**.

The two short ticks either side of the dot are where the bell is half its
height (-3 dB of it). Drag one of them for the width, too. **Shift** while
you press makes any drag five times finer. **Double-click** the dot to put
its gain back to 0 dB, and **Delete** removes the filter.

> **Every drag is one move to Live.** Undo takes back a whole drag, and
> automation records it as one gesture.

### Where it adds, and where it cuts

Double-click near the left edge, below 30 Hz, and you get a **Low Cut**
instead: everything below its frequency goes, the rumble you cannot hear but
your speakers can. Near the right edge, above 15 kHz, you get a **High Cut**.
Change any filter's type in the Inspector's **Type**:

| type | what it does | when |
|---|---|---|
| **Bell** | lifts or lowers a band around Freq | the honk at 800 Hz, the air at 12 kHz |
| **Low Shelf**, **High Shelf** | everything below (or above) Freq up or down | more bass, a darker top |
| **Low Cut**, **High Cut** | everything below (or above) Freq out | rumble, hiss, a telephone |
| **Band Pass** | only the band around Freq | a radio voice, a riser |
| **Notch** | a narrow band around Freq out, completely | a hum at 50 Hz, a ringing drum |
| **All Pass** | nothing to the level; turns the phase | aligning two mics, a phaser |
| **Tilt** | one end up, the other down, about Freq | brighter or darker in one move |
| **Empty** | no filter | removes it, keeping its settings for undo |

## Part 2 — how a cut turns the corner

### Order: how steep

A cut does not stop at its frequency like a wall. It slopes: an octave past
it, the sound is so many dB quieter. **Order** sets how steep: every step is
6 dB an octave more, from **6 dB** (gentle, natural) to **48 dB** (a wall).
Freq is where the cut is already **3 dB down** — about half the power gone.

### Family: what happens at the corner

Two cuts of the same order can turn the corner in very different ways. That
is what the **Family** is for:

| family | the corner | the trade |
|---|---|---|
| **Butterworth** | as flat as it can be up to the corner, then the slope | the reference; nothing wobbles |
| **Bessel** | soft and early | the cleanest transients: a kick stays a kick |
| **Chebyshev I** | sharp, after a passband that ripples by **Ripple** dB | steeper corner, a little colour before it |
| **Chebyshev II** | flat passband, sharp corner, then notches in the stop band, never above **Stopband** dB | steep and clean, but the stop band comes back up between its notches |
| **Elliptic** | the sharpest corner any filter of its order can have | ripples on both sides (Ripple and Stopband) |
| **Linkwitz-Riley** | -6 dB at Freq | a Low Cut and a High Cut at the same Freq add back up to flat: a crossover |

Try it: a **High Cut** at 2 kHz, **24 dB**. Switch Family and watch the
corner. Butterworth rounds it, Bessel starts bending an octave early,
Elliptic falls off a cliff and leaves small ripples behind. Every change of
family or order fades over 20 ms, so you can do it while the music plays.

### Resonance: a peak at the corner

On a cut the **Q** knob is called **Resonance**. At 0.71 there is none.
Turn it up and the corner rises into a peak before the slope: the squelch of
a synth filter. At order 12 dB, a resonance of 4 is a peak of exactly 12 dB.

### Band Pass and Notch: Q is the width

On a Band Pass or a Notch, **Q** is how narrow the band is: its frequency
divided by its width between the two -3 dB edges. A Notch takes its centre
out completely, at any order; a higher **Order** makes its sides steeper.

## Part 3 — the isolator

Click **ISO** in the row. The isolator splits the sound into three bands —
**Low** below **Lower**, **Mid** between, **High** above **Upper** — and
lets you turn each one up to +6 dB or all the way out. **Kill** takes a band
out at once, without a click: what a DJ does to drop the bass before the
break. **Slope** is how sharp the split is, 24 or 48 dB an octave.

All three at 0 dB put the sound back together flat. The split shifts the
phase a little around the crossovers, which is why the isolator starts
**off**; switch it on with **On** at the top right of its panel, or Alt-click
the ISO cell. It sits in front of the sixteen filters.

## Part 4 — the rest of the window

### The picture

- The **grey** line is 0 dB: your sound untouched.
- Each filter alone is a **thin dark** line; the selected one is **white**.
- The **bright violet** line is everything together: what you hear.
- Behind it, the **analyzer**: your sound's spectrum. The caption's right end
  says what it shows — click it for **Post** (after the filters, the
  default), **Pre** (before), **Pre + post**, or off.
- Next to it, **±18 dB** is the picture's range; click it for ±6, ±12 or
  ±30.
- A **dot** per filter; hollow when the filter is switched off.
- An **amber** line on the right appears when the sample rate is too low for
  a band you asked for: its top edge is held just under Nyquist, and it
  narrows.

### The row

Click a cell to select its filter; **Alt-click** switches it off and on —
its dot stays, hollow, so you can find it again. A cell shows its filter's
type as a small symbol, bright while it plays.

### Channel: one side, or the middle

**Channel** says what a filter works on: **Stereo** (both sides), **Left**,
**Right**, **Mid** (what both sides share: the vocal, the kick, the bass) or
**Side** (what differs: the width, the room). The picture's caption names
the lane it shows when you select a filter that is not Stereo.

### Output

**Output** is the level after everything. **Scale** multiplies every
filter's gain at once — 50 % makes the whole curve half as deep, -100 %
turns it upside down. **Adaptive** makes a bell narrower the more it boosts
or cuts, as many analog desks do.

## Recipes

**Clean up a vocal.** Low Cut, Butterworth, 18 dB at 90 Hz. A Bell at
300 Hz, -3 dB, Q 1.5 for the mud. A High Shelf at 10 kHz, +2 dB, for air.

**Kill a hum.** Notch at 50 Hz (60 Hz in America), Q 10. If it hums on, add
Notches at 100 and 150 Hz.

**A telephone.** Band Pass, Butterworth, 24 dB, at 1.2 kHz, Q 1.2.

**A DJ drop.** ISO, On. Kill Low on the last bar of the build, off on the
one.

**A crossover.** Two filters at 2.5 kHz, a Low Cut and a High Cut, both
Linkwitz-Riley 24 dB, one on Left and one on Right: two bands that add back
to flat.
