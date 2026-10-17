---
section: live
title: Ableton Live Interface
---

The window is one plot over a row of filter slots and two panels.

- **Response**, across the top: the magnitude response of every filter and
  of the whole chain, over the spectrum of the signal.
- **The row**: sixteen numbered filter slots and **ISO**, the isolator.
- **Inspector**: the parameters of the selected filter or of the isolator.
- **Output**: what applies to the whole chain, and **Bypass**.

## Part 1 — the magnitude response

A linear filter changes the level and the phase of each frequency
component. Its **magnitude response** |H(f)| is the level change in dB as a
function of frequency; the plot shows it on a logarithmic frequency axis
from 10 Hz to 22 kHz, 0 dB in the middle. A flat line on 0 dB is unity gain.
The **phase response** is not drawn: every filter here is **minimum phase**,
so the phase follows from the magnitude, and the plugin adds no latency.

A filter with a high Q **rings**: once the input stops, its output decays
at its centre frequency. A Q 40 notch at 20 Hz takes about 4.4 s to fall
60 dB. The plugin reports this as its **tail**: how long the filters ring
after the input stops, until they are 60 dB down, at most 30 s. A host
that renders with the tail does not cut that ringing off.

### Adding a filter

Double-click in the plot. A handle appears at the pointer's frequency and
gain: a **bell** (peaking) filter, which boosts or cuts a band centred on
**Freq** by **Gain** dB. Its **Q** sets the bandwidth: centre frequency over
the distance between the two -3 dB points. Q 0.71 is about two octaves wide,
Q 4 about a third of an octave.

| gesture | parameter |
|---|---|
| drag horizontally | Freq |
| drag vertically | Gain (Q for a filter without gain) |
| **Alt** + drag vertically | Q |
| drag one of the two ticks beside the handle | Q, by its -3 dB band edge |
| mouse wheel | Q |
| **Shift** while pressing | five times finer |
| double-click the handle | Gain (or Q) to its default |
| **Delete** | sets the slot's Type to Empty |

Every drag is one gesture per parameter: one undo step in Live, one
automation write. A double-click below 30 Hz adds a **high-pass**, above
15 kHz a **low-pass**.

### The filter types

| Type | transfer function | Freq is | uses |
|---|---|---|---|
| **Bell** | 2nd-order peaking EQ | the centre frequency | Gain, Q |
| **Low Shelf**, **High Shelf** | 1st to 4th order shelf | the frequency of half the gain | Gain, Q (order ≥ 2), Order |
| **High-Pass**, **Low-Pass** | order 1 to 8, from an analog approximation | the -3 dB cutoff (-6 dB for Linkwitz-Riley) | Family, Order, Q, Ripple, Stopband |
| **Band-Pass** | the low-pass prototype transformed to a band, 2N poles | the geometric centre | Family, Order, Q (bandwidth) |
| **Notch** (band-stop) | the low-pass prototype transformed to a band-stop | the centre, a transmission zero | Family, Order, Q (bandwidth) |
| **All-Pass** | order 1 to 8, Butterworth poles; unity magnitude | where the phase shift reaches order × 90° | Order, Q |
| **Tilt** | 1st-order tilt shelf, -Gain/2 at DC and +Gain/2 at Nyquist | the pivot, 0 dB | Gain |
| **Empty** | no filter | — | — |

Controls that the selected type does not use are disabled, so the layout
never changes.

## Part 2 — approximations and order

### Order and roll-off

A high-pass or low-pass of order N attenuates by 6·N dB per octave beyond its
cutoff: 12 dB/oct at order 2, 48 dB/oct at order 8. **Order** selects N.
A band-pass or notch of order N has 2N poles: N on each side of the band.
A shelf goes up to order 4 (24 dB/oct). A Linkwitz-Riley high-pass or
low-pass is a Butterworth filter squared, so it exists only at even orders
(LR2, LR4, LR6, LR8). Choosing an order a filter cannot have, or switching
to Linkwitz-Riley at an odd order, selects the next order it has, so the
field always shows the filter that plays.

### Family: the approximation

An ideal brick-wall filter cannot be built; each approximation trades the
flatness of the passband, the steepness of the transition band and the phase
response differently. **Family** selects it:

| Family | passband | transition | phase / group delay | parameters |
|---|---|---|---|---|
| **Butterworth** | maximally flat | moderate | moderate overshoot | — |
| **Bessel** | gently rolled off | the widest | maximally flat group delay: the least ringing on transients | — |
| **Chebyshev I** | equiripple, **Ripple** dB | steeper than Butterworth | more overshoot | Ripple |
| **Chebyshev II** (inverse) | maximally flat | steep | | **Stopband**: the minimum stopband attenuation; transmission zeros in the stopband |
| **Elliptic** (Cauer) | equiripple | the steepest for a given order | the most ringing | Ripple, Stopband |
| **Linkwitz-Riley** | Butterworth squared | as Butterworth of twice the order | -6 dB at Freq: at 24 and 48 dB/oct (LR4, LR8) a high-pass and a low-pass at the same Freq sum to an all-pass; at 12 and 36 dB/oct (LR2, LR6) they are in antiphase and cancel at Freq | — |

Every family is normalised so that Freq is its -3 dB point (Linkwitz-Riley:
-6 dB): switching the family keeps the handle on the curve. An even-order
Chebyshev I or elliptic filter starts at -Ripple dB at DC, as these
approximations are defined; its passband peaks at 0 dB.

**Example.** Select a **Low-Pass** at 2 kHz, 24 dB/oct. Switch **Family**
through all six and compare the curve around 2 kHz: Butterworth flat up to
the corner, Bessel rolling off an octave earlier, Chebyshev I rippling in
the passband, Chebyshev II with notches in the stopband, elliptic with the
narrowest transition. All of this can be done during playback:

- a change of **Family** moves the filter into the new one over 20 ms, the
  way a knob moves it;
- a change of **Type**, **Order**, **Channel** or **On** crossfades over 20 ms
  to the new filter, which has already been running on the music unheard
  until it settled. A narrow filter low in the spectrum rings for a while, so
  that can take up to half a second: it then comes in at its level, instead
  of swelling up from silence.

### Q on a high-pass or low-pass

On a high-pass or low-pass of order 2 or more, **Q** raises the Q of the
pole pair with the highest Q by Q / 0.71. At 0.71 the filter is the family's
own response; above it a resonant peak forms at the cutoff. At order 2,
Butterworth, the gain at the cutoff is exactly 20·log10(Q) dB: Q 4 is
+12 dB.

### Band-pass and notch: Q is the bandwidth

Q is the centre frequency over the bandwidth between the two -3 dB band
edges. A **Notch** has a transmission zero at its centre frequency: the
attenuation there is complete at every order, except for an even-order
Chebyshev II or elliptic notch, which bottoms out at its stopband
attenuation.

## Part 3 — the isolator

Click **ISO**. The isolator splits the signal into three bands with two
**Linkwitz-Riley crossovers**:

- **Low**: below the **Low-Mid** crossover frequency;
- **Mid**: between Low-Mid and **Mid-High**;
- **High**: above Mid-High.

Each band has a level from -inf to +6 dB and a **Kill**, which mutes the
band with a 5 ms gain ramp. **Slope** selects LR4 (24 dB/oct) or LR8
(48 dB/oct). The low band passes through the all-pass of the mid-high
crossover, so with all three levels at 0 dB the isolator sums to an
all-pass: a flat magnitude response with the crossovers' phase shift. That is
why it starts switched off; **On**, at the panel's title, switches it in.
The isolator precedes the sixteen filters.

## Part 4 — the rest of the window

### The plot

- grey line: 0 dB;
- dark lines: each heard filter's own response, and the isolator's bands;
- light line: the selected filter's own response;
- violet line: the response of the whole chain, in the selected filter's
  lane;
- handles: one per filter, hollow when the filter is bypassed.

**The analyzer.** Behind the curves, the spectrum of the signal **before**
the filters (pre, faded) and **after** them (post, highlighted), mono,
measured with an 8192-point FFT, Hann window, banded onto the plot's
frequency axis (the maximum bin per band), falling back at 300 ms. The
caption's right end selects **PRE + POST**, **POST**, **PRE** or off, and
the magnitude range: ±6, ±12, ±18 or ±30 dB. The analyzer runs only while
the window is open and shows it.

An **amber** line marks the highest frequency the sample rate allows for a
band edge (0.49·fs) when a band-pass or notch had to be narrowed to fit.

### The row

Click a slot to select it; **Alt**-click switches the filter's **On** (the
filter stays on the plot, hollow). The slot shows the filter type's symbol,
violet while the filter is in the signal path.

### Channel

**Channel** selects the signal a filter processes: **Stereo** (left and
right, the same filter), **Left**, **Right**, **Mid** ((L+R)/2) or **Side**
((L−R)/2). Mid/side is encoded and decoded around each mid or side filter
exactly. The plot's total is drawn for the selected filter's lane; the
caption names it when it is not stereo.

### Output

- **Output**: the output gain, ±24 dB.
- **Scale**: multiplies the gain of every bell, shelf and tilt; 50 % halves
  every boost and cut, -100 % inverts them.
- **Adaptive**: proportional Q for bells: Q_eff = Q·10^(|Gain|/40), so a bell
  narrows as its gain grows.
- **Matched**: every high-pass and low-pass matched to its analog prototype
  instead of the bilinear transform. Near Nyquist a bilinear low-pass falls
  faster than its analog prototype, steepening into the transform's zero at
  fs/2, and a resonant peak there narrows. A matched one keeps the analog
  slope and peak, with Freq still exactly at -3 dB. Example: a 24 dB/oct
  low-pass at 14 kHz, at 48 kHz, is -28 dB at 19 kHz bilinear and -10 dB
  matched (analog: -11 dB). Band-pass and notch filters are unchanged.
- **Bypass**: the plugin's bypass, the same as Live's device switch. The
  output crossfades to the input over 10 ms; the filters keep processing the
  input, so they resume from the current signal without a transient.

## Examples

**Vocal clean-up.** High-Pass, Butterworth, 18 dB/oct at 90 Hz. Bell at
300 Hz, -3 dB, Q 1.5. High Shelf at 10 kHz, +2 dB, order 2.

**Mains hum.** Notch at 50 Hz (60 Hz in North America), order 1, Q 10. Add
notches at the harmonics, 100 Hz and 150 Hz.

**Band-limited voice.** Band-Pass, Butterworth, 24 dB/oct, centred at
1.2 kHz, Q 1.2.

**Two-way crossover.** Two filters at 2.5 kHz: a High-Pass and a Low-Pass,
both Linkwitz-Riley 24 dB/oct, one on Left and one on Right. The two outputs
sum to a flat magnitude response.

**DJ isolator drop.** ISO, On. Kill Low on the last bar of a build-up,
release it on the downbeat.
