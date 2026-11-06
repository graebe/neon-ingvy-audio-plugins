---
title: Dissonance
order: 9
slug: dissonance
---

Some intervals sound smooth and others rough, and much of the difference is
physical. Two sine tones close in frequency beat, and the inner ear cannot
hear them apart. Plomp and Levelt measured how rough two sines sound against
the distance between them, and found that the distance that matters scales
with the critical band [1]. A musical note is a sum of sines, its partials, so
notes together are as rough as all their partials are, pair by pair. Sethares
put that into a formula [2, 3], and `dissonance` works it out.

```rust
use music_core::Timbre;

let strings = Timbre::harmonic(6);
let c4 = 261.63;

let fifth = strings.dissonance(&[c4, c4 * 1.5]);
let tritone = strings.dissonance(&[c4, c4 * 2f64.sqrt()]);
assert!(fifth < tritone);
```

Everything here needs the `std` feature, for `exp`. Nothing allocates: a curve
or a landscape goes into a slice the caller owns.

## Two sines

`pair(f1, a1, f2, a2)` is the roughness of two sines:

```text
d = min(a1, a2) · (e^(−b1·s·Δf) − e^(−b2·s·Δf)),   s = d* / (s1·f + s2)
```

Here Δf is the distance between the two in hertz, and f is the lower
frequency. The constants are those printed in [3], appendix E, eqs. E.1–E.4:
b1 = 3.5, b2 = 5.75, d* = 0.24, s1 = 0.021 and s2 = 19. Sethares' published
code [4] rounds them slightly differently (3.51, 0.0207, 18.96). We take the
printed values, which a reader can check.

The curve is 0 when the two sines coincide. It rises to its peak at
s·Δf = ln(b2/b1)/(b2 − b1) ≈ 0.22, about a quarter of a critical band apart, as
Plomp and Levelt found ([1], p. 548). Then it falls away. `roughest(f)` gives
that peak in hertz. It grows with the register, because the critical band is
wider higher up.

**Loudness.** A pair counts as loud as its quieter sine, because that is how
deep the beating goes ([3], appendix G, p. 343). The 1993 paper multiplied the
two amplitudes instead; the book calls the minimum its refinement ([3],
p. 330). Amplitudes go in as they are, with no conversion to sones.

## Notes, curves and landscapes

A `Timbre` is a list of partials, each a ratio over the fundamental and an
amplitude. `Timbre::dissonance(&fundamentals)` sums `pair` over every pair of
partials in the sound: within a note as well as across notes. That follows
[2] eq. 6 and [3] eq. E.7, where D(α) = D_F + D_αF + ΣΣ d(f_i, αf_j). A note's
own roughness is not a constant offset, because the critical band moves with
the register. Each note's own roughness counts once, even in a chord.

- `curve(f0, from, step, out)` holds one note at `f0` and moves a second from
  `from` cents up, `step` cents at a time, filling `out`.
- `landscape(f0, step, n, out)` does the same for three notes on an n × n
  grid. `x` and `y` are the two upper notes' distances above `f0`, as in
  Sethares' dissonance surfaces ([3] figs. 6.20–6.21): the major triad is
  (400, 700) and again (700, 400). Swapping the upper two changes nothing, so
  the grid is symmetric about its diagonal, where they coincide.
- `valleys(curve, prominence, out)` finds a curve's local minima, at least
  `prominence` of its range deep. A curve's ends are never a valley, so sample
  a little below the unison and beyond the last interval you want.

## Timbres

| Preset | Partials | Source |
|---|---|---|
| `harmonic(n)` | 1, 2, 3 … | Sethares' harmonic timbre [2, 3] |
| `stretched(n, A)` | k^log₂A, so the second partial lies at A | [2] eq. 7, figs. 7a–d |
| `odd(n)` | 1, 3, 5 … at 1/k | a square wave's series [5] |
| `bar(n)` | 1, 2.7565, 5.4039, 8.9330, 13.3443, 18.6380 | a free bar, from cos βL · cosh βL = 1 [2] eq. 8, [6] |
| `membrane(n)` | 1, 1.5933, 2.1355, 2.2954, 2.6531 … | the Bessel zeros j_mn over j_01 [7] |
| `bell(n)` | 0.5, 1, 1.2, 1.5, 2, 2.5, 2.61, 3, 4 | an ideal minor-third bell (Lehr), [3] table 6.2 |
| `saron(n)` | 1, 2.76, 4.72, 5.92 | a Javanese saron, [3] §10.2 |

A harmonic partial is 0.88 as loud as the one below, as in [2] fig. 2 and [3]
fig. 6.1. The odd harmonics fall as 1/k, as a square wave's do. The other
presets' sources give the frequencies but not the loudness, so they fall by
0.88 as well. That is our choice, not a measurement.

## What the tests hold it to

- **Six harmonics:** valleys at 6/5, 5/4, 4/3, 3/2, 5/3 and 2/1, within
  5 cents ([3] fig. 6.1).
- **Seven harmonics:** valleys at all nine minima of [2] table I (1, 1.2, 1.25,
  1.33, 1.4, 1.5, 1.67, 1.75 and 2). The min rule adds a shallow 7/6, which
  [3] table 6.1 lists for nine harmonics.
- **Stretched to A = 2.1:** the smooth octave moves to 2.1 and leaves 2/1
  ([2] fig. 7c).
- **Triads:** the just major and minor triads lie in wells of the landscape
  ([3] figs. 6.20–6.21, table 6.4).
- **Register:** the same minor third is rougher at C2 than at C5 ([1], p. 556;
  [3] p. 47).

## Sources

1. R. Plomp and W. J. M. Levelt, "Tonal consonance and critical bandwidth",
   *J. Acoust. Soc. Am.* 38 (1965) 548–560.
2. W. A. Sethares, "Local consonance and the relationship between timbre and
   scale", *J. Acoust. Soc. Am.* 94(3) (1993) 1218–1228.
3. W. A. Sethares, *Tuning, Timbre, Spectrum, Scale*, 2nd ed., Springer
   (2005). Page numbers are the book's own.
4. W. A. Sethares, "Computer programs" (`dissmeasure`),
   <https://sethares.engr.wisc.edu/comprog.html>.
5. E. W. Weisstein, "Fourier Series — Square Wave", MathWorld,
   <https://mathworld.wolfram.com/FourierSeriesSquareWave.html>.
6. D. A. Russell, "Flexural vibrations of a free-free bar", Penn State
   Acoustics, <https://www.acs.psu.edu/drussell/Demos/Flexural-Bar/flexural.html>.
7. D. A. Russell, "Vibrational modes of a circular membrane", Penn State
   Acoustics, <https://www.acs.psu.edu/drussell/Demos/MembraneCircle/Circle.html>.
