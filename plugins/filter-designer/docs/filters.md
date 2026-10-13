---
section: filters
title: Filter Reference
---

This is the mathematics behind every filter in NI Filter Designer: what each
one is, which controls it listens to, its formula as an analog filter and as
the digital filter that actually plays, and what the formula tells you about
the sound. The [Live manual](live.md) shows *how to use* the filters; this
page shows *what they are*.

Every number in the examples was measured on the engine's own code
(`ni-filter`) at 48 kHz, so it is what you will see in the picture.

## Part 1: how to read the formulas

### Notation

| symbol | meaning |
|---|---|
| `fs` | the sample rate, in Hz |
| `f0` | the filter's **Freq**, in Hz |
| `ω0 = 2π·f0` | Freq as an analog angular frequency |
| `s` | the analog (Laplace) variable; on the frequency axis `s = j·2π·f` |
| `z` | the digital variable; on the frequency axis `z = e^(j·2π·f/fs)` |
| `G` | a gain in dB, already times **Scale** |
| `A = 10^(G/40)` | the square root of the linear gain |
| `N` | the **Order** |
| `Q` | the **Q** control |
| `H` | a transfer function; `|H|` is its magnitude, `20·log10|H|` its level in dB |

A **continuous** formula, `H(s)`, describes an analog filter. That is the
filter's design: its shape, written in the language filter theory uses. A
**discrete** formula, `H(z)`, describes the digital filter that runs on the
samples. The two meet on the frequency axis: put `s = j·2π·f` into one and
`z = e^(j·2π·f/fs)` into the other, and the magnitude is the curve you see.

### From analog to digital: the bilinear transform

Most filters here go from `H(s)` to `H(z)` by the **bilinear transform**. It
replaces `s` with

```text
        z − 1
  u = ─────────          on the frequency axis:   u = j·tan(π·f/fs)
        z + 1
```

This maps the whole analog frequency axis, 0 to ∞, onto the digital one, 0
to fs/2. The price is that frequencies are *warped*: analog frequency `Ω`
lands at digital frequency `f` where

```text
  Ω = tan(π·f/fs)
```

Low down, `Ω ≈ π·f/fs` and nothing changes. Near fs/2 the axis is squeezed,
so a low-pass reaches silence exactly at fs/2. To put a filter's important
frequency exactly where you asked for it, that frequency is **prewarped**:
`Ωc = tan(π·f0/fs)` takes the place of `ω0`.

The designer prewarps **once per filter**, never once per section. Only then
is the digital cascade exactly the bilinear transform of its analog design:

```text
  H_digital(e^(j·2π·f/fs)) = H_analog(j·tan(π·f/fs))
```

So the Butterworth, Chebyshev and elliptic shapes keep their defining
properties (flatness, equal ripples, notch positions) up to fs/2.

### The one building block: a state-variable section

Every filter is a chain (a **cascade**) of sections in series, plus one gain.
A section is a **TPT state-variable filter** (Zavalishin, *The Art of VA
Filter Design*) with three outputs, high-pass, band-pass and low-pass,
mixed by three weights `m_hp`, `m_bp`, `m_lp`.

**Continuous**, a second-order section with frequency `ω0` and damping
`k = 1/Q`:

```text
          m_hp·s² + m_bp·ω0·s + m_lp·ω0²
  H(s) = ────────────────────────────────
              s² + k·ω0·s + ω0²
```

**Discrete:** `s → u` and `ω0 → g = tan(π·f0/fs)`. Written out as a biquad:

```text
          b0 + b1·z⁻¹ + b2·z⁻²        b0 = m_hp + m_bp·g + m_lp·g²
  H(z) = ──────────────────────       b1 = 2·(m_lp·g² − m_hp)
          a0 + a1·z⁻¹ + a2·z⁻²        b2 = m_hp − m_bp·g + m_lp·g²
                                      a0 = 1 + k·g + g²
                                      a1 = 2·(g² − 1)
                                      a2 = 1 − k·g + g²
```

**What runs**, per sample, with the integrator states `s1` and `s2`:

```text
  hp = (x − (k + g)·s1 − s2) / (1 + g·(g + k))
  bp = g·hp + s1          s1 ← bp + g·hp
  lp = g·bp + s2          s2 ← lp + g·bp
  y  = m_hp·hp + m_bp·bp + m_lp·lp
```

A **first-order section** (for odd orders) is the TPT one-pole:

```text
          m_hp·s + m_lp·ω0
  H(s) = ──────────────────
              s + ω0

          (m_hp + m_lp·g) + (m_lp·g − m_hp)·z⁻¹
  H(z) = ───────────────────────────────────────
                 (1 + g) + (g − 1)·z⁻¹

  v = (x − s1)·g/(1 + g)     lp = v + s1     s1 ← lp + v
  hp = x − lp                y = m_hp·hp + m_lp·lp
```

A whole filter is then

```text
  H(z) = c · H_1(z) · H_2(z) · … · H_n(z)        (at most 8 sections)
```

Why this one structure:

- **Any shape.** The three outputs together make every second-order
  numerator, so one structure is a low-pass, a high-pass, a band-pass, a
  notch, a bell, a shelf or an all-pass by its mix alone.
- **Smooth automation.** It stays stable while `g` and `k` move, for any
  `g > 0` and `k > 0`. A textbook biquad (direct form) can blow up or click
  when its coefficients change quickly.
- **Exact notches.** Zeros on the analog frequency axis (`m_bp = 0`) land
  exactly on the digital unit circle, so a notch is infinitely deep.

### Matched filters: the Bell, the shelves and the tilt

The bilinear transform squeezes everything near fs/2. For a cut that is
correct, but a bell at 15 kHz would come out narrower than you drew it. So the
equaliser shapes are **matched** instead (Vicanek, *Matched Second Order
Digital Filters*, 2016): the digital filter meets the analog magnitude
exactly at DC, at fs/2 and at the match frequency `fm` (Freq).

**Poles** are the analog ones, impulse-invariant. For an analog pole pair at
`fp` with quality `Qp`:

```text
  w  = 2π·fp/fs  (at most 0.98·π)        ζ = 1/(2·Qp)
  a1 = −2·e^(−ζ·w)·cos(w·√(1 − ζ²))      (cosh(w·√(ζ² − 1)) when ζ > 1)
  a2 = e^(−2·ζ·w)
```

**Zeros** are solved so that `|H|²` meets the analog `|Ha|²` at the three
points:

```text
  A0 = (1 + a1 + a2)²      A1 = (1 − a1 + a2)²      A2 = −4·a2
  φ1 = sin²(π·fm/fs)       φ0 = 1 − φ1               φ2 = 4·φ0·φ1

  B0 = |Ha(0)|²·A0         B1 = |Ha(fs/2)|²·A1
  B2 = (|Ha(fm)|²·(A0·φ0 + A1·φ1 + A2·φ2) − B0·φ0 − B1·φ1) / φ2

  W  = (√B0 + √B1)/2
  b0 = (W + √(W² + B2))/2      b1 = (√B0 − √B1)/2      b2 = W − b0
```

The biquad `(b0 + b1·z⁻¹ + b2·z⁻²)/(1 + a1·z⁻¹ + a2·z⁻²)` then becomes a
state-variable section:

```text
  d = 1 − a1 + a2
  g = √((1 + a1 + a2)/d)          k = 2·(1 − a2)/(d·g)
  m_hp = (b0 − b1 + b2)/d         m_bp = 2·(b0 − b2)/(d·g)
  m_lp = (b0 + b1 + b2)/(d·g²)
```

If no real numerator meets the three points (`W² + B2 < 0`), the section falls
back to the prewarped bilinear transform. A **first-order** matched section
puts its pole at `p = e^(−2π·fp/fs)`, so `g = (1 − p)/(1 + p)`, and matches
DC and fs/2 only: `m_lp = |Ha(0)|`, `m_hp = |Ha(fs/2)|`.

### What Freq and Q mean

- **Low-Pass and High-Pass:** Freq is where the level is **−3 dB**, in every
  family. Linkwitz-Riley is the exception: **−6 dB**.
- **Band-Pass, Notch and Bell:** Q is the centre over the width between the
  two band edges. The edges `f1` and `f2` sit symmetrically around Freq on a
  log axis:

```text
  f1 = f0·(√(1 + 1/(4Q²)) − 1/(2Q))       f1·f2 = f0²
  f2 = f0·(√(1 + 1/(4Q²)) + 1/(2Q))       f2 − f1 = f0/Q

  width in octaves = 2·asinh(1/(2Q)) / ln 2
```

  Q 0.71 is about 1.9 octaves wide, Q 1.41 one octave, Q 4.3 a third of an
  octave. For a band-pass or a notch the edges are its −3 dB points. For a
  Bell they are where it reaches **half its gain** in dB.
- **Shelves and All-Pass:** Q shapes the steepest section (order 2 and up).
- **Clamps:** every frequency and band edge stays between 1 Hz and
  0.49·fs. Freq itself goes from 10 Hz to 22 kHz.

## Part 2: the families

A **family**, the **approximation** in filter theory, is the shape of the
corner where a cut turns from passing to stopping. An ideal brick wall cannot
be built, so each family trades passband flatness, steepness and phase in
its own way. Each family is an analog **prototype**: a low-pass of order `N`
with its corner at 1 rad/s. Part 3 then moves the prototype to Freq and
turns it into a high-pass, a band-pass or a notch.

Every prototype is a product of second-order factors and at most one
first-order factor:

```text
               ω_i²·(s² + z_i²)               σ
  H(s) = c · Π ───────────────────────── · ───────
             i  z_i²·(s² + (ω_i/Q_i)·s + ω_i²)  s + σ
```

Each factor is 1 at DC. `c` is the passband level at DC. A factor without
zeros drops its `(s² + z_i²)/z_i²` term. Then the prototype is **normalised**:
its frequency axis is scaled by a constant `w3` so that it is exactly −3 dB
at 1 rad/s. `w3` depends only on Family, Order, Ripple and Stopband, never
on Freq. So switching the family leaves the curve under the handle, and only
the corner's shape changes.

**At order 1 all families are the same filter**, a single pole. Ripple,
stopband and squaring need at least two poles. (Linkwitz-Riley's order 1
plays as order 2.)

The examples below are a Low-Pass at 1 kHz, order 4 (24 dB/oct), measured an
octave above, at 2 kHz:

| family | at 2 kHz | group delay at 100 Hz → peak |
|---|---|---|
| Bessel | −13.5 dB | 0.34 → 0.34 ms |
| Butterworth | −24.3 dB | 0.42 → 0.62 ms |
| Linkwitz-Riley | −24.7 dB | 0.45 → 0.54 ms |
| Chebyshev II (60 dB) | −27.0 dB | 0.40 → 0.63 ms |
| Chebyshev I (1 dB) | −36.1 dB | 0.47 → 1.37 ms |
| Elliptic (1 dB, 60 dB) | −44.9 dB | 0.44 → 1.47 ms |

**Group delay** is how long each frequency is held back. Where it is
constant, the waveform keeps its shape. A peak at the corner is what you hear
as ringing on transients. The table shows the trade every filter makes:
the steeper the corner, the more it rings.

### Butterworth

**Description.** Maximally flat. Nothing in the passband moves until the
corner, then the slope. It is the reference every other family is compared
with.

**Parameters.** Order 1–8. Ripple and Stopband are not used.

**Continuous.**

```text
                  1
  |H(jΩ)|² = ──────────
              1 + Ω^(2N)

  poles: s = −sin θ_k ± j·cos θ_k,      θ_k = (2k + 1)·π / (2N)
         k = 0 … ⌊N/2⌋ − 1,  and s = −1 for odd N

           ⌊N/2⌋−1        1                    1
  H(s) =     Π      ───────────────────── · ─────  (odd N)
            k=0      s² + 2·sin θ_k·s + 1     s + 1
```

Every pole lies on the unit circle (`ω_i = 1`, `Q_k = 1/(2·sin θ_k)`). It
is −3 dB at 1 by construction, so `w3 = 1`.

| order | section Q |
|---|---|
| 2 | 0.707 |
| 3 | 1.000 (+ one pole) |
| 4 | 1.307, 0.541 |
| 8 | 2.563, 0.900, 0.601, 0.510 |

**Discrete** (a Low-Pass at Freq, `Ωc = tan(π·f0/fs)`): every section has
`g = Ωc`, `k = 2·sin θ_k` and the mix `(0, 0, 1)`. An odd order adds the
one-pole `g = Ωc`, mix `(0, 1)`.

**Explanation.** The first `2N − 1` derivatives of `|H|²` are zero at DC.
That is what *maximally flat* means: no other filter of the same order stays
closer to 0 dB below its corner. Far above the corner the level falls by
`20·log10(Ω^N)`, which is **6·N dB per octave**. Order 4 at 1 kHz is
−3.01 dB at 1 kHz, −24.3 dB at 2 kHz and −48.9 dB at 4 kHz. The price is a
modest group-delay peak at the corner, so a low-pass of high order rings
slightly.

### Bessel

**Description.** Maximally flat *delay*. Every frequency in the passband is
delayed by almost the same time, so a waveform keeps its shape. It has the
softest corner of all.

**Parameters.** Order 1–8. Ripple and Stopband are not used.

**Continuous.**

```text
            θ_N(0)               N          (2N − k)!
  H(s) = ───────────    θ_N(s) = Σ  a_k·s^k,   a_k = ───────────────────
          θ_N(w3·s)             k=0                2^(N−k)·k!·(N − k)!

  θ_2(s) = s² + 3s + 3
  θ_4(s) = s⁴ + 10s³ + 45s² + 105s + 105
```

`θ_N` is the **reverse Bessel polynomial** (Thomson, 1949). Its roots are the
poles. Nothing parameterises a Bessel filter but its order, so the roots and
`w3` (which puts −3 dB at 1) are a constant table for orders 1 to 8; a test
holds every entry to the polynomial.

**Discrete.** For each pole `p_i` of the normalised prototype, a section with
`g = |p_i|·Ωc`, `k = 2·(−Re p_i)/|p_i|`, mix `(0, 0, 1)` (Low-Pass).

**Explanation.** `θ_N` is chosen so that the phase is as close to a straight
line as `N` poles allow. Linear phase means constant delay. Order 4 at 1 kHz
delays 0.336 ms at 100 Hz and 0.331 ms at 1 kHz. A Butterworth delays 0.42 ms
and then 0.59 ms. So a kick through a Bessel low-pass stays a kick. The cost is
the corner: it starts to bend an octave early, and a Bessel of order 4 is
only −13.5 dB an octave above Freq, where a Butterworth is −24.3 dB. Higher
orders barely sharpen it (order 8: −13.8 dB). They only make the delay
flatter.

### Chebyshev I

**Description.** A steeper corner, bought with a passband that ripples by
**Ripple** dB.

**Parameters.** Order 1–8, **Ripple** 0.01–3 dB (default 1 dB).

**Continuous.**

```text
                    1
  |H(jΩ)|² = ────────────────        ε = √(10^(R/10) − 1)
              1 + ε²·T_N(Ω)²

  T_N(Ω) = cos(N·acos Ω)      for |Ω| ≤ 1
         = cosh(N·acosh Ω)    for |Ω| > 1

  poles: s = −sinh(a)·sin θ_k ± j·cosh(a)·cos θ_k,    a = asinh(1/ε) / N
         and s = −sinh(a) for odd N

  c = 1 (odd N),   c = 1/√(1 + ε²) (even N)
```

`T_N` is the Chebyshev polynomial. It swings between −1 and +1 inside
`|Ω| ≤ 1`, so `|H|²` swings between 1 and `1/(1 + ε²)`, which is R dB down.
Beyond 1 it grows like `2^(N−1)·Ω^N`. The ripple band ends at 1. `w3` then
moves the −3 dB point (a little beyond it) to 1. Even orders start their
passband at the bottom of the ripple, which is why `c` lowers the whole curve
by R.

**Discrete.** Sections from the poles as for Bessel: `g = |p|·Ωc/w3`,
`k = 2·(−Re p)/|p|`.

**Explanation.** Allowing the passband to wobble moves the poles closer to the
frequency axis (higher Q), which sharpens the corner. At 1 dB ripple and order 4
the level an octave up is −36.1 dB, 12 dB more than Butterworth. The passband
peaks at 0 dB and dips to exactly −1.00 dB. More ripple gives a sharper
corner and more colour. The high-Q poles also give the biggest group-delay
peak after the elliptic (1.37 ms at order 4), so it rings. Ripple is
capped at 3 dB so the −3 dB point stays unique.

### Chebyshev II (inverse Chebyshev)

**Description.** A flat passband like Butterworth, a sharper corner, and a
stopband that bounces between notches but never rises above **−Stopband** dB.

**Parameters.** Order 1–8, **Stopband** 20–120 dB (default 60 dB).

**Continuous.**

```text
               ε²·T_N(1/Ω)²                         1
  |H(jΩ)|² = ──────────────────        ε = ────────────────────
              1 + ε²·T_N(1/Ω)²              √(10^(A/10) − 1)

  poles: the reciprocals 1/q_k of the Chebyshev I poles q_k with this ε
  zeros: s = ±j / cos θ_k
  odd N: one pole at s = −1/sinh(a)
```

Where `T_N(1/Ω)² = 1`, `|H|²` is `ε²/(1 + ε²) = 10^(−A/10)`: the stopband
peaks are exactly A dB down. Where `T_N(1/Ω) = 0`, `|H|` is zero: those are
the notches at `1/cos θ_k`. Raw, the stopband starts at 1. `w3` moves the −3 dB
point there.

**Discrete.** Each factor becomes one section with zeros: for a Low-Pass,
`g = ω_i·Ωc/w3`, `k = 1/Q_i`, mix `((ω_i/z_i)², 0, 1)`. The zeros land on
the unit circle, exactly.

**Explanation.** It is Chebyshev I turned inside out. The ripple moves from
the passband, where you hear it, into the stopband, where it is at least A dB
down. The passband is monotonic. The corner is a little sharper than
Butterworth: −27.0 dB an octave up at order 4 and 60 dB. The delay is almost as
calm (peak 0.63 ms). Two things to know:

- Between its notches the stopband comes back up to −A dB. It does not keep
  falling like the all-pole families. Choose A by how far down "gone" has to
  be.
- An **even** order stays at −A dB all the way to fs/2. An odd order falls to
  silence there.

### Elliptic (Cauer)

**Description.** The sharpest corner any filter of its order can have, with
equal ripples in both bands: **Ripple** in the passband, **−Stopband** in the
stopband.

**Parameters.** Order 2–8 (order 1 is the shared one-pole), **Ripple**
0.01–3 dB, **Stopband** 20–120 dB.

**Continuous.**

```text
                     1                εp = √(10^(R/10) − 1)
  |H(jΩ)|² = ─────────────────       εs = √(10^(A/10) − 1)
              1 + εp²·R_N(Ω)²         k1 = εp / εs
```

`R_N` is the elliptic rational function. It stays within ±1 for
`|Ω| ≤ 1` (the passband ripple) and above `1/k1` for `|Ω| ≥ 1/k` (the
stopband floor). It is built from Jacobi elliptic functions of modulus `k`,
the **selectivity**. `k` follows from the **degree equation**:

```text
  k' = k1'^N · Π sn⁴(u_i·K1', k1')
               i
  k' = √(1 − k²),  k1' = √(1 − k1²),  u_i = (2i − 1)/N,  i = 1 … ⌊N/2⌋

  zeros:  s = ±j / (k·cd(u_i·K, k))
  poles:  s = j·cd((u_i − j·v0)·K, k),  v0 from sn(j·N·v0·K1, k1) = j/εp
  odd N:  s = j·sn(j·v0·K, k)

  c = 1 (odd N),   c = 10^(−R/20) (even N)
```

`K` and `K1'` are the complete elliptic integrals of `k` and `k1'`. The
Jacobi functions `sn` and `cd` are computed in closed form by Landen's
descending transformation (Orfanidis, *Lecture Notes on Elliptic Filter
Design*, 2006). Seven steps reach double precision. As designed, the
passband edge is at 1 and the stopband edge at `1/k`. `w3` then moves the
−3 dB point to 1.

**Discrete.** As Chebyshev II: one section per factor, zeros exactly on the
unit circle.

**Explanation.** Butterworth puts all its effort into flatness, Chebyshev I
into the passband and Chebyshev II into the stopband. The elliptic spends
its poles and zeros on both, and that gives the narrowest transition there
is. At order 4, 1 dB and 60 dB, it is −44.9 dB an octave above Freq, and
nothing in the stopband rises above −60 dB. It has the worst phase of the
six (group-delay peak 1.47 ms), so it rings the most. Use it where a
frequency has to go and the transient does not matter: an anti-alias
filter, a sharp band split for sound design. Like Chebyshev II, an even order
stays at −Stopband dB up to fs/2.

### Linkwitz-Riley

**Description.** Two Butterworth filters in series. A Low-Pass and a
High-Pass at the same Freq sum to a flat magnitude (in phase at LR4 and LR8;
LR2 and LR6 need one of them inverted), which makes it the crossover
family.

**Parameters.** Order: it exists only at **even** orders, LR2, LR4
(24 dB/oct), LR6 and LR8 (48 dB/oct). The window selects the next even
order when an odd one is chosen; an odd order set by automation plays the
next even one too. Ripple and Stopband are not used.

**Continuous.**

```text
  H_LR,N(s) = H_BW,N/2(s)²

                    1
  |H(jΩ)|² = ──────────────          |H(j1)| = 1/2  →  −6.02 dB
              (1 + Ω^N)²
```

It is **not** renormalised: Freq is the **−6 dB** point. Each Low-Pass and
High-Pass is then down 6 dB at Freq, so their amplitudes add to 1 there. For
LR4 (`D(s) = s² + √2·s + 1`):

```text
                  1        s⁴       s⁴ + 1     s² − √2·s + 1
  LP + HP  =  ────── + ────── = ──────── = ───────────────     |·| = 1
                 D²       D²        D²       s² + √2·s + 1
```

The sum is an **all-pass**: flat in level, with a phase turn of 360°. LR8
sums to the all-pass of Butterworth order 4 in the same way.

**Discrete.** The Butterworth sections of order N/2, each twice. When N/2 is
odd, the two real poles at −1 become one section with Q = 1/2.

**Explanation.** For a crossover you want two bands that hand over without a
bump or a dip. Butterworth bands are −3 dB each at the crossover. Their powers
add up to 1, but their amplitudes do not, and amplitudes are what add when
two bands play together (an order-2 pair even cancels at the crossover).
Linkwitz-Riley bands are −6 dB and in phase there, so their amplitudes add
up to 0 dB at every frequency.

> **Only LR4 and LR8 sum flat as they are.** LR2 and LR6 sum flat only with
> one band's polarity inverted. Added as they are, they cancel completely at
> Freq (measured: −∞ dB at 1 kHz). The Designer has no polarity switch, so
> for a crossover choose **24 dB/oct** or **48 dB/oct**. The isolator offers only
> those two.

On a **Band-Pass** or **Notch**, Linkwitz-Riley plays as Butterworth of the
same order. The squared response is meant for crossovers, not bands.

## Part 3: the filter types

### Low-Pass and High-Pass

**Description.** A **Low-Pass** keeps everything below Freq and takes away
what is above. A **High-Pass** does the opposite: it takes away the lows,
such as rumble and handling noise. They are the cuts.

**Parameters.**

| control | range | does |
|---|---|---|
| Family | the six above | the shape of the corner |
| Order | 1–8, 6–48 dB/oct | the steepness |
| Freq | 10 Hz – 22 kHz | the −3 dB point (−6 dB for Linkwitz-Riley) |
| Q | 0.1 – 40 (0.71 = none) | resonance, order 2 and up |
| Ripple, Stopband | as the family | Chebyshev I/II, Elliptic |

**Continuous.** With `H_N` the family's normalised prototype:

```text
  H_LP(s) = H_N(s/ω0)          H_HP(s) = H_N(ω0/s)
```

**Discrete.**

```text
  H_LP(z) = H_N(u/Ωc)          H_HP(z) = H_N(Ωc/u)
  u = (z − 1)/(z + 1),   Ωc = tan(π·f0/fs)
```

Each prototype factor `(ω_i, Q_i, z_i)` becomes one section, and the real pole
`σ` one first-order section:

```text
  factor (ω_i, Q_i, z_i):   g            k       mix (m_hp, m_bp, m_lp)
    Low-Pass              ω_i·Ωc/w3    1/Q_i   ((ω_i/z_i)², 0, 1)
    High-Pass             w3·Ωc/ω_i    1/Q_i   (1, 0, (ω_i/z_i)²)
  without zeros, the mix is (0, 0, 1) and (1, 0, 0)

  real pole σ (first order): g            mix (m_hp, m_lp)
    Low-Pass              σ·Ωc/w3      (0, 1)
    High-Pass             w3·Ωc/σ      (1, 0)
```

The cascade's gain is the prototype's `c`.

**Resonance.** At order 2 and up, Q raises the Q of the factor that already
has the highest Q:

```text
  Q_top' = min(40,  Q_top · Q / 0.7071)
```

At Q 0.71 the family is untouched. On a Butterworth of order 2 the level at
Freq is then exactly `20·log10(Q)` dB, as on every classic resonant filter.
Order 1 has no resonance.

**Explanation.** The slope above the corner is 6 dB per octave per order. On a
Butterworth Low-Pass at 1 kHz, the level an octave up is −7.0 dB at order 1,
−18.2 dB at order 3, −30.3 dB at order 5 and −42.4 dB at order 7. A High-Pass is
the mirror image on the warped axis: order 4 at 1 kHz is −24.1 dB at 500 Hz.
Resonance adds a peak just before the corner, the squelch of a synth filter.
Order 2, Q 4 peaks at +12.1 dB at 984 Hz and is +12.0 dB at Freq. Q 10 gives
+20 dB.

**Example.** *Rumble.* High-Pass, Butterworth, 18 dB/oct at 40 Hz: flat above
about 80 Hz, −18 dB an octave below Freq, and no colour where the bass lives.
Switch to Bessel to keep a kick's attack intact, or to Elliptic if a 20 Hz
rumble must go at any cost.

### Band-Pass

**Description.** Keeps a band around Freq and takes away both sides: the
telephone, the radio voice, the riser.

**Parameters.**

| control | range | does |
|---|---|---|
| Family | all six (Linkwitz-Riley plays as Butterworth) | the shape of both corners |
| Order | 1–8 | N poles per side, 2N in all; each side falls 6·N dB/oct |
| Freq | 10 Hz – 22 kHz | the centre |
| Q | 0.1 – 40 | the width: Freq over the distance between the −3 dB edges |
| Ripple, Stopband | as the family | |

**Continuous.** The band edges `f1` and `f2` come from Freq and Q (Part 1),
with `ω1 = 2π·f1` and `ω2 = 2π·f2`:

```text
  H(s) = H_N( (s² + ω0²) / (B·s) )        ω0² = ω1·ω2,   B = ω2 − ω1
```

**Discrete.** **Each edge** is prewarped, so both −3 dB points are exact at
any rate:

```text
  H(z) = H_N( (u² + Ω1·Ω2) / ((Ω2 − Ω1)·u) )
  Ω1 = tan(π·f1/fs),   Ω2 = tan(π·f2/fs)
```

The substitution turns every prototype pole `p` into two. They are the roots
of `u² − (p·B/w3)·u + Ω0²`, and each becomes a section with `g = |u_i|` and
`k = −2·Re u_i / g`. An all-pole factor gives two band-pass sections. A
factor with zeros gives two sections with zeros on the unit circle, one on
each side. The real pole `σ` gives one second-order band-pass:
`g = Ω0`, `k = σ·B/(w3·Ω0)`, mix `(0, k, 0)`.

**Explanation.** The band-pass is a low-pass prototype folded around Freq: the
prototype's 0 to 1 rad/s becomes the band from `f1` to `f2`. Everything about
the family carries over. A Chebyshev band ripples across its top, and an
elliptic band has notches on both sides. The edges are exact. The point
midway between them on the warped axis sits a little above Freq near fs/2
(15 kHz, Q 2 at 48 kHz is centred at 15.9 kHz). If `f2` would pass 0.49·fs,
it is held there and `f1` stays, so the band narrows. The amber line in the
picture says so.

**Example.** *A telephone.* Band-Pass, Butterworth, order 4 (8 poles), 1.2 kHz,
Q 1.2: the edges are 800 Hz and 1800 Hz, both at −3.01 dB. The centre is
0 dB. 300 Hz is −52 dB and 4.8 kHz −53 dB.

### Notch

**Description.** Takes away a band around Freq and keeps both sides: a mains
hum, a ringing drum, a resonance in a room.

**Parameters.** As Band-Pass. Q is the width between the −3 dB edges.

**Continuous.**

```text
  H(s) = H_N( B·s / (s² + ω0²) )
```

**Discrete.**

```text
  H(z) = H_N( (Ω2 − Ω1)·u / (u² + Ω1·Ω2) )
```

For an all-pole factor, both sections get the numerator `u² + Ω0²`, a pair
of zeros exactly on the unit circle at the centre. The real pole `σ` gives one
section with `g = Ω0`, `k = w3·B/(σ·Ω0)`, mix `(1, 0, 1)`.

**Explanation.** A notch is the band-pass inside out. The prototype's stopband
is folded onto the centre and its passband onto both sides. How deep the
centre goes depends on what the prototype does at infinity:

- **Butterworth, Bessel, Chebyshev I, Linkwitz-Riley:** the centre is an
  exact null, at every order. A higher order only makes the sides steeper and
  the bottom wider.
- **Chebyshev II and Elliptic, odd order:** a null at the centre too.
- **Chebyshev II and Elliptic, even order:** the centre sits at **−Stopband
  dB**, with two exact nulls just beside it. At order 2, 60 dB, Q 10 the centre
  is −60 dB. Raise Stopband, or choose an odd order, if the very centre must
  go.

**Example.** *A hum.* Notch, Butterworth, 50 Hz, Q 10: the edges are 47.6 Hz
and 52.6 Hz, so a seventh of an octave goes and the bass around it stays. Add
notches at 100 and 150 Hz for the harmonics.

### Bell

**Description.** Lifts or lowers a band around Freq. It is the parametric
equaliser's everyday tool.

**Parameters.** Freq, **Gain** −30 to +30 dB (times Scale), **Q** 0.1–40. With
**Adaptive Q** on, Q narrows as the gain grows.

**Continuous.** With `A = 10^(G/40)`:

```text
          s² + (A/Q)·ω0·s + ω0²
  H(s) = ───────────────────────          |H(jω0)| = A² = 10^(G/20)
          s² + ω0/(A·Q)·s + ω0²

                (1 − x²)² + (x·A/Q)²
  |H(j2πf)|² = ──────────────────────────       x = f/f0
                (1 − x²)² + (x/(A·Q))²
```

**Discrete.** One matched section (Part 1): poles at `fp = f0`, `Qp = A·Q`,
matched at DC (0 dB), at fs/2, and at Freq (exactly G).

**Adaptive Q.**

```text
  Q_eff = Q · 10^(|G|/40)       ×1.41 at 6 dB, ×2 at 12 dB, ×4 at 24 dB
```

**Explanation.** At the edges `f1` and `f2`, `(1 − x²)² = x²/Q²`, and the
formula gives `|H| = A` exactly: **half the gain in dB**. Those are the two
ticks beside the handle. Boost and cut are mirror images: in the analog design
the bell of −G is exactly `1/H` of the bell of +G, so a cut undoes a boost of
the same Q. The matched digital pair keeps this within 0.005 dB below a
quarter of the rate. Up near fs/2 it is within a fraction of a dB (0.7 dB at
15 kHz, 48 kHz).
Because the section is matched, a bell high up keeps its analog shape: 15 kHz,
+6 dB, Q 1 at 44.1 kHz is +6.00 dB at 15 kHz, and its upper skirt is not
squeezed against fs/2.

**Example.** 1 kHz, +6 dB, Q 1: +6.00 dB at 1 kHz, +3.00 dB at 618 Hz and
1618 Hz (the edges, each a golden ratio from Freq), 0.00 dB at 20 Hz.

### Low Shelf and High Shelf

**Description.** A **Low Shelf** raises or lowers everything below Freq, and a
**High Shelf** everything above it. Think bass and treble.

**Parameters.** Freq (where the shelf has half its gain), **Gain** −30 to
+30 dB, **Order** 1–4, **Q** (order 2 and up) on the steepest section.

**Continuous.** A shelf of order N is a cascade (Holters and Zölzer,
*Parametric Higher-Order Shelving Filters*, 2006). Each second-order section
carries `2G/N` dB and each first-order section `G/N` dB, so they add up to G.
The sections take Butterworth's angles, `Q_k = 1/(2·sin θ_k)`,
`θ_k = (2k + 1)·π/(2N)`. Q scales the first, steepest one by `Q/0.7071`.

Second-order section, `A = 10^((2G/N)/40)`:

```text
                A·(s² + (√A/Q_k)·ω0·s + A·ω0²)
  low:  H(s) = ────────────────────────────────    DC: A²  Freq: A  ∞: 1
                 A·s² + (√A/Q_k)·ω0·s + ω0²

                A·(A·s² + (√A/Q_k)·ω0·s + ω0²)
  high: H(s) = ────────────────────────────────    DC: 1   Freq: A  ∞: A²
                 s² + (√A/Q_k)·ω0·s + A·ω0²
```

First-order section (odd N), `g = 10^((G/N)/20)`:

```text
                 s + √g·ω0
  low:  H(s) = ─────────────        DC: g   Freq: √g   ∞: 1
                 s + ω0/√g

                g·s + √g·ω0
  high: H(s) = ─────────────        DC: 1   Freq: √g   ∞: g
                 s + √g·ω0
```

**Discrete.** Every section is matched (Part 1). The second-order poles sit at
`f0/√A` (low) or `f0·√A` (high) with `Q_k`, matched at DC, fs/2 and Freq. The
first-order pole is impulse-invariant, matched at DC and fs/2.

**Explanation.** Every section reaches half its gain at Freq, so the whole
shelf does. A higher order makes the step steeper:

| Low Shelf, 100 Hz, +6 dB | 50 Hz | 100 Hz | 200 Hz |
|---|---|---|---|
| order 1 | +4.76 | +3.00 | +1.24 |
| order 2 | +5.62 | +3.00 | +0.38 |
| order 4 | +5.97 | +3.00 | +0.03 |

Q above 0.71 makes the step overshoot, with a bump on the boosted side and a dip
on the other. That is the shape of the classic passive program equalisers.
Order 2, Q 2 gives +8.9 dB at 73 Hz and −2.9 dB at 138 Hz. A first-order shelf
is matched only at DC and fs/2. High up it is a few tenths of a dB off at Freq
(a first-order High Shelf at 8 kHz, +6 dB, is +3.26 dB at 8 kHz).

**Example.** *Air.* High Shelf, 10 kHz, +2 dB, order 2: +1.0 dB at 10 kHz,
+1.8 dB at 16 kHz, +1.9 dB at 20 kHz, and only +0.1 dB at 5 kHz.

### Tilt

**Description.** Turns the whole spectrum about Freq: the lows down and the
highs up, or the other way round. It makes a sound brighter or darker in one
move.

**Parameters.** Freq (the pivot), **Gain** (the total turn: −G/2 at DC, +G/2
at the top).

**Continuous.** With `g = 10^(G/20)`:

```text
          √g·s + ω0         DC:   1/√g   (−G/2 dB)
  H(s) = ───────────       Freq: 1      (0 dB)
          s + √g·ω0         ∞:    √g     (+G/2 dB)
```

**Discrete.** One first-order matched section: pole at `f0·√g`,
impulse-invariant, `m_lp = 1/√g`, `m_hp = |Ha(fs/2)|`.

**Explanation.** A tilt is a first-order high shelf of G dB, lowered by G/2 so
it pivots on 0 dB at Freq. Its zero sits at `f0/√g` and its pole at `f0·√g`,
and the turn happens between the two, gently, at no more than 6 dB per
octave. That is how a tilt equaliser keeps the balance natural.

**Example.** 1 kHz, +6 dB: −3.00 dB at 20 Hz, −2.94 dB at 100 Hz, 0.00 dB at
1 kHz, +2.94 dB at 10 kHz, +2.99 dB at 20 kHz.

### All-Pass

**Description.** Changes no level at all, only the phase: it delays some
frequencies more than others. You use it to align two microphones, for a
phaser, or as a crossover's missing half.

**Parameters.** Freq, **Order** 1–8, **Q** (order 2 and up) on the steepest
section.

**Continuous.** On Butterworth's poles, with `Q_k = 1/(2·sin θ_k)` and the
first `Q_k` times `Q/0.7071`:

```text
           s² − (ω0/Q_k)·s + ω0²         s − ω0
  H(s) = Π ─────────────────────── · ────────    (odd N)
         k s² + (ω0/Q_k)·s + ω0²        s + ω0
```

**Discrete.** The bilinear transform, prewarped at Freq. It keeps an all-pass
an all-pass exactly. Each section has `g = Ωc`, `k = 1/Q_k` and the mix
`(1, −k, 1)`, which is `y = x − 2k·bp`. The first-order section is
`y = hp − lp`.

**Explanation.** Every zero mirrors a pole across the frequency axis, so
`|H| = 1` at every frequency. Only the phase turns:

- A second-order section turns from 0° at DC through −180° at Freq to −360°
  at the top.
- The first-order section of an odd order turns by 180° in all. It starts at
  180°: an odd order **inverts the lows** (`H(0) = −1`).

At Freq the phase has turned by N·90° relative to DC. A second-order section
delays Freq by

```text
  τ(f0) = 2·Q_k / (π·f0)          and DC by   τ(0) = 1 / (π·Q_k·f0)
```

so Q trades a wide, gentle delay for a narrow, tall one at Freq. Order 2,
1 kHz: 0.45 ms at Q 0.71, and 1.28 ms at Freq with Q 2 (0.16 ms at DC).

**Example.** *Two microphones on a snare.* Put an All-Pass on the one that
sounds thin against the other. Sweep Freq until the sum gets fuller. The
level of each stays the same; only their phase relation changes.

## Part 4: the isolator

**Description.** Three bands, like a DJ mixer's EQ: **Low** below the first
crossover, **Mid** between, **High** above the second. Each has a level and a
kill. It sits in front of the sixteen filters.

**Parameters.**

| control | range | default |
|---|---|---|
| Isolator (On) | Off, On | Off |
| Low, Mid, High | −60 to +6 dB (−60 is silence) | 0 dB |
| Kill Low, Mid, High | Off, On | Off |
| Crossover Low-Mid | 30 – 2000 Hz | 250 Hz |
| Crossover Mid-High | 500 – 16000 Hz (never below Low-Mid) | 2500 Hz |
| Crossover Slope | LR4 24 dB/oct, LR8 48 dB/oct | LR4 |

**Continuous.** With Linkwitz-Riley low- and high-passes at the two crossovers
`f_lo` and `f_hi`, and `AP` the all-pass of Butterworth order N/2 at `f_hi`
(which is exactly `LP + HP` of that crossover):

```text
  Low(s)  = LP_LR(s; f_lo) · AP(s; f_hi)
  Mid(s)  = HP_LR(s; f_lo) · LP_LR(s; f_hi)
  High(s) = HP_LR(s; f_lo) · HP_LR(s; f_hi)

  H(s) = g_L·Low(s) + g_M·Mid(s) + g_H·High(s)
  g = 10^(level/20),  0 when killed or at −60 dB
```

**Discrete.** Every block is the Linkwitz-Riley or All-Pass design of Part 3,
prewarped at its own crossover. The bilinear transform keeps sums, so the
identity below holds digitally too, to rounding.

**Explanation.** With all three bands at 0 dB:

```text
  Low + Mid + High = LP_lo·AP_hi + HP_lo·(LP_hi + HP_hi)
                   = LP_lo·AP_hi + HP_lo·AP_hi
                   = AP_hi · (LP_lo + HP_lo)
                   = AP_hi · AP_lo                        |H| = 1
```

That is why the Low band runs through the upper crossover's all-pass. It has
the same phase turn the Mid and High bands get from splitting at `f_hi`, so
all three line up and sum flat (measured: within 10⁻¹⁴ dB). Without it, Low
and Mid would disagree in phase where they overlap, and the sum would dip
around the lower crossover: −0.1 dB at the default crossovers, more as they
move closer.
Flat is not the same as untouched, though. The sum is an all-pass, so the
phase still turns around both crossovers. That is why the isolator starts
**off**.

Kills and levels glide every sample over 5 ms, so a kill never clicks.
Switching the isolator on or off fades over 20 ms. A change of slope
crossfades between the old and the new design.

**Example.** *A DJ drop.* Isolator on, Crossover Low-Mid at 200 Hz. Kill Low on
the last bar of the build: the kick and the bass go in 5 ms, with no click.
Release it on the one.

## Part 5: what applies to every filter

- **Scale** multiplies the gain of every Bell, shelf and tilt:
  `G = Gain·Scale/100`. 50 % halves the curve, and −100 % turns it upside down.
  Cuts, bands, notches and all-passes have no gain to scale.
- **Adaptive Q** (Bell only): `Q_eff = Q·10^(|G|/40)`, after Scale.
- **Channel** picks what a filter works on. Mid and Side are exact round
  trips:

```text
  M = (L + R)/2     S = (L − R)/2          L = M + S     R = M − S
```

- **Output** is the level after everything, `10^(Output/20)`.
- **The order of the chain:** the isolator, then the filters in slot order 1
  to 16, then Output. The order of the filters only matters when Left or
  Right filters are mixed with Mid or Side ones: those do not commute.
  Everything else does.
- **Changes are smooth.** Freq glides in octaves, Q in log and Gain in dB,
  each over 5 ms. A change of Type, Family, Order, Channel or bypass
  crossfades between two designs over 20 ms.

## Further reading

- Vadim Zavalishin, *The Art of VA Filter Design*, Native Instruments, 2018:
  the TPT state-variable filter.
- Martin Vicanek, *Matched Second Order Digital Filters*, 2016: the matched
  Bell, shelves and tilt.
- Martin Holters and Udo Zölzer, *Parametric Higher-Order Shelving Filters*,
  EUSIPCO 2006: the shelves of order 3 and 4.
- Sophocles J. Orfanidis, *Lecture Notes on Elliptic Filter Design*, 2006:
  the elliptic prototype.
- W. E. Thomson, *Delay Networks Having Maximally Flat Frequency
  Characteristics*, Proc. IEE, 1949: the Bessel filter.
- Siegfried H. Linkwitz, *Active Crossover Networks for Noncoincident
  Drivers*, JAES, 1976: the Linkwitz-Riley crossover.
- [ADR 0004](../../../docs/adr/0004-iir-filter-design.md): why the
  Designer builds its filters this way.
