// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
From a shape and its settings to a [`Cascade`].

THE FAMILIES ([`family`]), on the bilinear transform's warped axis
`u = j·tan(ω/2)`, with ONE prewarp for the whole filter:

```text
  low-pass   p = u / Ωc                    Ωc = tan(π·fc/fs)
  high-pass  p = Ωc / u
  band-pass  p = (u² + Ω0²) / (B·u)        Ω0² = Ω1·Ω2, B = Ω2 - Ω1,
  band stop  p = B·u / (u² + Ω0²)          Ω1, Ω2 the -3 dB edges, each warped
```

`p` the NORMALISED prototype's variable; the prototype as designed runs at
`w3·p` (proto.rs). Each factor then lands as one section (low and high
cut) or two (band-pass and stop, whose quadratic per pole has two roots), and
every section's numerator follows from the factor's: all-pole, zeros on the
jω axis, or both band edges' zeros at the centre -- a notch's, exact.

Freq is the -3 dB point of every family (-6 dB for Linkwitz-Riley); a band
pass's and a notch's Q is its centre over the width between its -3 dB
edges. RESONANCE on a cut raises the Q of the factor with the highest Q by
Q/0.7071 (at order 2, a Butterworth cut with Q, as every EQ has); a band and
a notch take no resonance, their Q is their width.

THE EQUALISER'S SHAPES are matched (Vicanek, 2016): the analog poles,
impulse-invariant, and a numerator whose magnitude meets the analog one at
DC, at Nyquist and at Freq, so a shelf high in the spectrum keeps its shape
where the bilinear transform would squeeze it. A design that cannot be
matched -- no real numerator meets the three -- falls back to the
prewarped bilinear transform, which always can.
*/

use crate::cascade::{Cascade, Section};
use crate::proto::{Family, Prototype};
use core::f64::consts::{FRAC_1_SQRT_2, PI};
use num_complex::Complex64;

/// The lowest a band's edge goes, in Hz.
pub const MIN_HZ: f64 = 1.0;

/// The highest a frequency or an edge goes, as a fraction of the rate.
pub const MAX_HZ_FRACTION: f64 = 0.49;

/// The highest Q a section is given.
const MAX_Q: f64 = 40.0;

/// What a family design is.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Band {
    /// A high-pass: attenuates below the cutoff.
    HighPass,
    /// A low-pass: attenuates above the cutoff.
    LowPass,
    BandPass,
    /// A band stop.
    Notch,
}

fn top(fs: f64) -> f64 {
    MAX_HZ_FRACTION * fs
}

fn warp(hz: f64, fs: f64) -> f64 {
    (PI * hz.clamp(MIN_HZ, top(fs)) / fs).tan()
}

/// A section from one pole in the upper half plane of the warped axis.
fn pole_section(u: Complex64, hp: f64, bp_over_g: f64, lp_over_g2: f64) -> Section {
    let g = u.norm();
    Section::second(g, -2.0 * u.re / g, hp, bp_over_g / g, lp_over_g2 / (g * g))
}

/// The roots of u² - b·u + c, the one with the larger magnitude first.
fn roots(b: Complex64, c: f64) -> (Complex64, Complex64) {
    let d = (b * b - 4.0 * c).sqrt();
    let (r1, r2) = ((b + d) * 0.5, (b - d) * 0.5);
    if r1.norm() >= r2.norm() {
        (r1, r2)
    } else {
        (r2, r1)
    }
}

/// THE FAMILY DESIGN: `proto` as `band` at `hz`, with `q` the resonance (a
/// cut) or the width (a band, a notch), at the rate `fs`. Also says whether
/// the rate clamped an edge: a band whose upper edge would pass
/// 0.49·fs keeps its lower one and narrows.
pub fn family(proto: &Prototype, band: Band, hz: f64, q: f64, fs: f64) -> (Cascade, bool) {
    let q = if q.is_finite() { q.clamp(0.1, MAX_Q) } else { FRAC_1_SQRT_2 };
    match band {
        Band::HighPass | Band::LowPass => (cut(proto, band == Band::HighPass, hz, q, fs), hz > top(fs)),
        Band::BandPass | Band::Notch => {
            /* Linkwitz-Riley's square is a crossover's: a band plays it as
             * Butterworth. */
            let bw;
            let proto = if proto.family() == Family::LinkwitzRiley {
                bw = Prototype::new(Family::Butterworth, proto.order(), 1.0, 60.0);
                &bw
            } else {
                proto
            };
            band_design(proto, band == Band::BandPass, hz, q, fs)
        }
    }
}

fn cut(proto: &Prototype, high_pass: bool, hz: f64, q: f64, fs: f64) -> Cascade {
    let oc = warp(hz, fs);
    let w3 = proto.w3();
    /* Resonance: the factor with the highest Q, raised by Q / 0.7071. */
    let peak = proto
        .pairs()
        .iter()
        .enumerate()
        .max_by(|a, b| a.1.q.total_cmp(&b.1.q))
        .map(|(i, _)| i)
        .filter(|_| proto.order() >= 2 && (q - FRAC_1_SQRT_2).abs() > 1e-9);
    let mut c = Cascade::identity();
    for (i, pair) in proto.pairs().iter().enumerate() {
        let pq = if Some(i) == peak { (pair.q * q / FRAC_1_SQRT_2).min(MAX_Q) } else { pair.q };
        let zero = pair.zero.map_or(0.0, |wz| (pair.w0 / wz).powi(2));
        c.push(if high_pass {
            Section::second(w3 * oc / pair.w0, 1.0 / pq, 1.0, 0.0, zero)
        } else {
            Section::second(pair.w0 * oc / w3, 1.0 / pq, zero, 0.0, 1.0)
        });
    }
    if let Some(s) = proto.real() {
        c.push(if high_pass { Section::first(w3 * oc / s, 1.0, 0.0) } else { Section::first(s * oc / w3, 0.0, 1.0) });
    }
    c.set_gain(proto.dc());
    c
}

fn band_design(proto: &Prototype, pass: bool, hz: f64, q: f64, fs: f64) -> (Cascade, bool) {
    let h = 0.5 / q;
    let r = (1.0 + h * h).sqrt();
    let hz = hz.clamp(MIN_HZ, top(fs));
    let f2 = hz * (r + h);
    let clamped = f2 > top(fs);
    let f2 = f2.min(top(fs));
    let f1 = (hz * (r - h)).clamp(MIN_HZ, f2 * 0.999);
    let (o1, o2) = (warp(f1, fs), warp(f2, fs));
    let o0sq = o1 * o2;
    let width = o2 - o1;
    let w3 = proto.w3();
    let mut c = Cascade::identity();
    for pair in proto.pairs() {
        let p = pair.pole();
        if pass {
            /* p·w3·... : u² - (p·B/w3)·u + Ω0² for each pole. */
            let cb = width / w3;
            let (u1, u2) = roots(p * cb, o0sq);
            match pair.zero {
                None => {
                    let b = p.norm() * cb;
                    c.push(pole_section(u1, 0.0, b, 0.0));
                    c.push(pole_section(u2, 0.0, b, 0.0));
                }
                Some(wz) => {
                    let s = p.norm() / wz;
                    let a = wz * cb;
                    let (z1, z2) = {
                        let d = (a * a + 4.0 * o0sq).sqrt();
                        let (x, y) = (0.5 * (a + d), 0.5 * (d - a));
                        (x.max(y), x.min(y))
                    };
                    c.push(pole_section(u1, s, 0.0, s * z1 * z1));
                    c.push(pole_section(u2, s, 0.0, s * z2 * z2));
                }
            }
        } else {
            let d = w3 * width;
            let (u1, u2) = roots(Complex64::new(d, 0.0) / p, o0sq);
            match pair.zero {
                None => {
                    c.push(pole_section(u1, 1.0, 0.0, o0sq));
                    c.push(pole_section(u2, 1.0, 0.0, o0sq));
                }
                Some(wz) => {
                    let beta = d / wz;
                    let e = (beta * beta + 4.0 * o0sq).sqrt();
                    let (x, y) = (0.5 * (e - beta), 0.5 * (e + beta));
                    let (z1, z2) = (x.max(y), x.min(y));
                    c.push(pole_section(u1, 1.0, 0.0, z1 * z1));
                    c.push(pole_section(u2, 1.0, 0.0, z2 * z2));
                }
            }
        }
    }
    if let Some(s) = proto.real() {
        let o0 = o0sq.sqrt();
        c.push(if pass {
            let k = s * width / (w3 * o0);
            Section::second(o0, k, 0.0, k, 0.0)
        } else {
            Section::second(o0, w3 * width / (s * o0), 1.0, 0.0, 1.0)
        });
    }
    c.set_gain(proto.dc());
    (c, clamped)
}

/* ------------------------------------------------------------- matched -- */

/// An analog magnitude squared, at a frequency in Hz.
type Mag2<'a> = &'a dyn Fn(f64) -> f64;

/// A digital biquad (b0 + b1/z + b2/z²) / (1 + a1/z + a2/z²) as a section.
fn from_biquad(b: [f64; 3], a1: f64, a2: f64) -> Option<Section> {
    let d = 1.0 - a1 + a2;
    let g2 = (1.0 + a1 + a2) / d;
    if !(d > 0.0 && g2 > 0.0) {
        return None;
    }
    let g = g2.sqrt();
    let k = 2.0 * (1.0 - a2) / (d * g);
    let n2 = (b[0] - b[1] + b[2]) / d;
    let n1 = 2.0 * (b[0] - b[2]) / d;
    let n0 = (b[0] + b[1] + b[2]) / d;
    let s = Section::second(g, k, n2, n1 / g, n0 / g2);
    [g, k, n2, n1, n0].iter().all(|v| v.is_finite()).then_some(s)
}

/// A second-order section matched to `mag2`: the analog poles at `pole_hz`
/// and `pole_q`, impulse-invariant, and the numerator that meets `mag2` at
/// DC, at Nyquist and at `match_hz` (Vicanek, 2016), its value at Nyquist of
/// the sign `nyquist` (±1): both meet the three points, and between them
/// one or the other follows `mag2`. `None` when no real numerator does.
fn matched2(fs: f64, pole_hz: f64, pole_q: f64, match_hz: f64, mag2: Mag2, nyquist: f64) -> Option<Section> {
    let w = (2.0 * PI * pole_hz / fs).min(0.98 * PI);
    let zeta = 0.5 / pole_q;
    let r = (-zeta * w).exp();
    let a1 = if zeta <= 1.0 {
        -2.0 * r * (w * (1.0 - zeta * zeta).sqrt()).cos()
    } else {
        -2.0 * r * (w * (zeta * zeta - 1.0).sqrt()).cosh()
    };
    let a2 = r * r;
    let (big_a0, big_a1, big_a2) = ((1.0 + a1 + a2).powi(2), (1.0 - a1 + a2).powi(2), -4.0 * a2);
    let wm = 2.0 * PI * match_hz.min(top(fs)) / fs;
    let phi1 = (0.5 * wm).sin().powi(2);
    let phi0 = 1.0 - phi1;
    let phi2 = 4.0 * phi0 * phi1;
    let b0 = mag2(0.0) * big_a0;
    let b1 = mag2(0.5 * fs) * big_a1;
    let b2 = (mag2(match_hz) * (big_a0 * phi0 + big_a1 * phi1 + big_a2 * phi2) - b0 * phi0 - b1 * phi1) / phi2;
    let (s0, s1) = (b0.sqrt(), nyquist * b1.sqrt());
    let wsum = 0.5 * (s0 + s1);
    let disc = wsum * wsum + b2;
    if !(disc >= 0.0 && phi2 > 1e-12) {
        return None;
    }
    let n0 = 0.5 * (wsum + disc.sqrt());
    let n2 = wsum - n0;
    let n1 = 0.5 * (s0 - s1);
    from_biquad([n0, n1, n2], a1, a2)
}

/// Where [`closest2`] weighs a design: a log grid from 20 Hz to the top,
/// each point's place on the warped axis and the analog |H|² there.
const GRID: usize = 24;

/// How far `s` strays from the analog shape at its worst on `grid`: the
/// larger of |H|²/mag2 and its inverse (1 where it meets it).
fn misfit(s: &Section, grid: &[(f64, f64); GRID]) -> f64 {
    grid.iter()
        .map(|&(u, m2)| {
            let r = s.transfer(Complex64::new(0.0, u)).norm_sqr() / m2;
            if r.is_finite() && r > 0.0 { r.max(1.0 / r) } else { f64::INFINITY }
        })
        .fold(1.0, f64::max)
}

/// A matched design that strays at most this (0.25 dB, as a power ratio) is
/// taken without weighing the others.
const CLOSE_ENOUGH: f64 = 1.059_253_725_177_289;

/// The second-order section closest to `mag2` at its worst, from 20 Hz to
/// the top: matched with either sign at Nyquist, or `bilinear`. Matched
/// follows the analog shape past the bilinear transform's warping; but with
/// its pole near or past Nyquist it may stray far between the points it
/// meets -- a matched high shelf at 18 kHz, +18 dB, order 4 was 13.6 dB off
/// at 8.5 kHz at 44.1 kHz -- and then the bilinear one is closer.
fn closest2(fs: f64, pole_hz: f64, pole_q: f64, match_hz: f64, mag2: Mag2, bilinear: Section) -> Section {
    let first = matched2(fs, pole_hz, pole_q, match_hz, mag2, 1.0);
    /* Low in the band the matched design is the closer one: weighed only
     * from an eighth of the rate up, which keeps a glide's design cheap. */
    if pole_hz.max(match_hz) < 0.125 * fs {
        return first.unwrap_or(bilinear);
    }
    let (lo, hi) = (20.0f64.min(top(fs)), top(fs));
    let step = (hi / lo).powf(1.0 / (GRID - 1) as f64);
    let mut grid = [(0.0, 0.0); GRID];
    let mut f = lo;
    for g in &mut grid {
        *g = ((PI * f / fs).tan(), mag2(f));
        f *= step;
    }
    let mut best: Option<(f64, Section)> = first.map(|m| (misfit(&m, &grid), m));
    if let Some((e, m)) = best {
        if e <= CLOSE_ENOUGH {
            return m;
        }
    }
    if let Some(m) = matched2(fs, pole_hz, pole_q, match_hz, mag2, -1.0) {
        let e = misfit(&m, &grid);
        if best.is_none_or(|(b, _)| e < b) {
            best = Some((e, m));
        }
    }
    match best {
        Some((e, m)) if e <= misfit(&bilinear, &grid) => m,
        _ => bilinear,
    }
}

/// The prewarped bilinear transform of `(n2·s² + n1·s + n0) / (s² + d1·s +
/// d0)`, s in units of 2π·`hz`: what a design falls back to.
fn bilinear2(fs: f64, hz: f64, n: [f64; 3], d1: f64, d0: f64) -> Section {
    let oc = warp(hz, fs);
    let g = oc * d0.sqrt();
    Section::second(g, d1 * oc / g, n[0], n[1] * oc / g, n[2] * oc * oc / (g * g))
}

/// A first-order section matched at DC and Nyquist: the analog pole at
/// `pole_hz`, impulse-invariant; `h0` and `hpi` the magnitudes there.
fn matched1(fs: f64, pole_hz: f64, h0: f64, hpi: f64) -> Section {
    let p = (-2.0 * PI * pole_hz / fs).exp();
    Section::first((1.0 - p) / (1.0 + p), hpi, h0)
}

/// THE BELL: `gain_db` at `hz`, its width `q`.
pub fn bell(hz: f64, gain_db: f64, q: f64, fs: f64) -> Cascade {
    let mut c = Cascade::identity();
    if gain_db == 0.0 || !gain_db.is_finite() {
        return c;
    }
    let hz = hz.clamp(MIN_HZ, top(fs));
    let q = q.clamp(0.1, MAX_Q);
    let a = 10f64.powf(gain_db / 40.0);
    let mag2 = move |f: f64| {
        let w = f / hz;
        let re = (1.0 - w * w).powi(2);
        (re + (w * a / q).powi(2)) / (re + (w / (a * q)).powi(2))
    };
    c.push(matched2(fs, hz, a * q, hz, &mag2, 1.0).unwrap_or_else(|| bilinear2(fs, hz, [1.0, a / q, 1.0], 1.0 / (a * q), 1.0)));
    c
}

/// A shelf of `order` 1..4: `gain_db` below `hz` (low) or above it (high),
/// half of it at `hz`. Order 2 and up are second-order shelves on
/// Butterworth's angles (Holters and Zölzer), the steepest with `q`'s
/// resonance; an odd order adds a first-order shelf.
fn shelf(low: bool, hz: f64, gain_db: f64, q: f64, order: usize, fs: f64) -> Cascade {
    let mut c = Cascade::identity();
    if gain_db == 0.0 || !gain_db.is_finite() {
        return c;
    }
    let hz = hz.clamp(MIN_HZ, top(fs));
    let q = q.clamp(0.1, MAX_Q);
    let order = order.clamp(1, 4);
    let halves = order / 2;
    for k in 0..halves {
        let mut sq = 1.0 / (2.0 * (PI * (2 * k + 1) as f64 / (2 * order) as f64).sin());
        if k == 0 {
            sq = (sq * q / FRAC_1_SQRT_2).min(MAX_Q);
        }
        let a = 10f64.powf(gain_db * 2.0 / order as f64 / 40.0);
        let ra = a.sqrt();
        let mag2 = move |f: f64| {
            let w = f / hz;
            let (num, den) = if low { (a - w * w, 1.0 - a * w * w) } else { (1.0 - a * w * w, a - w * w) };
            let mid = (ra * w / sq).powi(2);
            a * a * (num * num + mid) / (den * den + mid)
        };
        let pole_hz = if low { hz / ra } else { hz * ra };
        let fallback = if low {
            bilinear2(fs, hz, [1.0, ra / sq, a], 1.0 / (ra * sq), 1.0 / a)
        } else {
            bilinear2(fs, hz, [a * a, a * ra / sq, a], ra / sq, a)
        };
        c.push(closest2(fs, pole_hz, sq, hz, &mag2, fallback));
    }
    if order % 2 == 1 {
        let g = 10f64.powf(gain_db / order as f64 / 20.0);
        let w = 0.5 * fs / hz;
        c.push(if low {
            let a = 1.0 / g.sqrt();
            matched1(fs, hz * a, g, ((w * w + g * g * a * a) / (w * w + a * a)).sqrt())
        } else {
            let a = g.sqrt();
            matched1(fs, hz * a, 1.0, ((g * g * w * w + a * a) / (w * w + a * a)).sqrt())
        });
    }
    c
}

pub fn low_shelf(hz: f64, gain_db: f64, q: f64, order: usize, fs: f64) -> Cascade {
    shelf(true, hz, gain_db, q, order, fs)
}

pub fn high_shelf(hz: f64, gain_db: f64, q: f64, order: usize, fs: f64) -> Cascade {
    shelf(false, hz, gain_db, q, order, fs)
}

/// THE TILT: a first-order turn about `hz`, -gain/2 at DC and +gain/2 at
/// the top, 0 dB at `hz`.
pub fn tilt(hz: f64, gain_db: f64, fs: f64) -> Cascade {
    let mut c = Cascade::identity();
    if gain_db == 0.0 || !gain_db.is_finite() {
        return c;
    }
    let hz = hz.clamp(MIN_HZ, top(fs));
    let g = 10f64.powf(gain_db / 20.0);
    let rg = g.sqrt();
    let w = 0.5 * fs / hz;
    c.push(matched1(fs, hz * rg, 1.0 / rg, ((g * w * w + 1.0) / (w * w + g)).sqrt()));
    c
}

/// THE ALL-PASS of `order` 1..8 on Butterworth's poles at `hz`, the
/// steepest factor's Q raised by `q` / 0.7071: flat, its phase turning
/// through `order`·90° around `hz`.
pub fn all_pass(hz: f64, q: f64, order: usize, fs: f64) -> Cascade {
    let oc = warp(hz, fs);
    let order = order.clamp(1, 8);
    let q = q.clamp(0.1, MAX_Q);
    let mut c = Cascade::identity();
    for k in 0..order / 2 {
        let mut pq = 1.0 / (2.0 * (PI * (2 * k + 1) as f64 / (2 * order) as f64).sin());
        if k == 0 {
            pq = (pq * q / FRAC_1_SQRT_2).min(MAX_Q);
        }
        let k = 1.0 / pq;
        /* (u² - k·g·u + g²) / (u² + k·g·u + g²): x - 2k·bp, as hp + k·bp + lp = x. */
        c.push(Section::second(oc, k, 1.0, -k, 1.0));
    }
    if order % 2 == 1 {
        c.push(Section::first(oc, 1.0, -1.0));
    }
    c
}
