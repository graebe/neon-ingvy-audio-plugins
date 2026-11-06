// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! Sensory dissonance: how rough sounds are together, worked out from their
//! partials.
//!
//! Two sine tones close in frequency beat, and the ear cannot hear them
//! apart. Plomp and Levelt (1965) measured how rough that sounds against how
//! far apart the two are, a distance that scales with the critical band. A
//! note is a sum of sines, its partials, and notes together are as rough as
//! all their partials are, pair by pair: Helmholtz's idea, in Sethares'
//! formula (1993, and *Tuning, Timbre, Spectrum, Scale*, 2005).
//! `docs/dissonance.md` has the sources for every constant and table here.
//!
//! So the smooth intervals are those whose partials coincide or stand well
//! apart, and which those are depends on the timbre: harmonic partials make
//! the just intervals smooth, a bar's partials other ones.
//!
//! ```
//! use music_core::Timbre;
//!
//! let strings = Timbre::harmonic(6);
//! let c4 = 261.63;
//! let fifth = strings.dissonance(&[c4, c4 * 1.5]);
//! let tritone = strings.dissonance(&[c4, c4 * 2f64.sqrt()]);
//! assert!(fifth < tritone);
//! ```
//!
//! Everything here needs `std`, for `exp`, and allocates nothing: a curve or a
//! landscape is written into a slice the caller owns.

/// Sethares' fit of Plomp and Levelt's curve, as printed (*Tuning, Timbre,
/// Spectrum, Scale*, 2005, eqs. E.1-E.4): the critical band's scale,
/// `D_STAR / (S1·f + S2)` with f the lower frequency, and the two decay
/// rates. His published code rounds differently (3.51, 0.0207, 18.96); the
/// printed values are the ones a reader can check.
const D_STAR: f64 = 0.24;
const S1: f64 = 0.021;
const S2: f64 = 19.0;
const B1: f64 = 3.5;
const B2: f64 = 5.75;

/// How rough two sines are together: 0 when they coincide, rising to its
/// peak about a quarter of a critical band apart, then falling away.
///
/// The pair counts as loud as its quieter sine, since that is how deep the
/// beating goes: the book's refinement of the 1993 paper's product. The
/// order of the two does not matter.
///
/// ```
/// use music_core::dissonance;
///
/// assert_eq!(dissonance::pair(440.0, 1.0, 440.0, 1.0), 0.0);
/// let near = dissonance::pair(440.0, 1.0, 460.0, 1.0);
/// let far = dissonance::pair(440.0, 1.0, 660.0, 1.0);
/// assert!(near > far);
/// ```
#[must_use]
pub fn pair(f1: f64, a1: f64, f2: f64, a2: f64) -> f64 {
    let (low, high) = if f1 <= f2 { (f1, f2) } else { (f2, f1) };
    let x = D_STAR / (S1 * low + S2) * (high - low);
    a1.min(a2) * ((-B1 * x).exp() - (-B2 * x).exp())
}

/// How far apart two sines at and above `f` sound roughest, in hertz.
///
/// ```
/// use music_core::dissonance;
///
/// let peak = dissonance::roughest(440.0);
/// let at = |df: f64| dissonance::pair(440.0, 1.0, 440.0 + df, 1.0);
/// assert!(at(peak) > at(peak - 1.0) && at(peak) > at(peak + 1.0));
/// ```
#[must_use]
pub fn roughest(f: f64) -> f64 {
    (B2 / B1).ln() / (B2 - B1) * (S1 * f + S2) / D_STAR
}

/// One sine of a sound: its frequency as a multiple of the fundamental, and
/// its amplitude.
#[derive(Clone, Copy, PartialEq, Debug)]
pub struct Partial {
    /// Frequency over the fundamental's; the fundamental itself is 1.
    pub ratio: f64,
    /// Amplitude, the fundamental's usually 1.
    pub amp: f64,
}

/// The most partials a [`Timbre`] holds.
pub const MAX_PARTIALS: usize = 16;

/// The most points on a side of a [`Timbre::landscape`].
pub const MAX_SIDE: usize = 256;

/// The partials of a sound, the same at every pitch it plays.
#[derive(Clone, Copy, PartialEq, Debug)]
pub struct Timbre {
    partials: [Partial; MAX_PARTIALS],
    len: usize,
}

const SILENT: Partial = Partial {
    ratio: 1.0,
    amp: 0.0,
};

/// Where no source gives the partials' loudness, each is this much of the
/// one below, as in Sethares' harmonic timbre.
const ROLL_OFF: f64 = 0.88;

/// A free bar's modes over its lowest: (βL / 4.7300)², βL the roots of
/// cos βL · cosh βL = 1 (Sethares 1993, eq. 8).
const BAR: [f64; 6] = [1.0, 2.7565, 5.4039, 8.9330, 13.3443, 18.6380];

/// A circular membrane's modes over its lowest: the zeros j of the Bessel
/// functions J₀ … J₅, in rising order, over j₀,₁ = 2.404826.
const MEMBRANE: [f64; 10] = [
    1.0, 1.5933, 2.1355, 2.2954, 2.6531, 2.9173, 3.1555, 3.5001, 3.5985, 3.6474,
];

/// An ideal minor-third church bell's partials over its prime: hum, prime,
/// tierce, quint, nominal, deciem, undeciem, duodeciem and upper octave
/// (Lehr, in *Tuning, Timbre, Spectrum, Scale*, table 6.2).
const BELL: [f64; 9] = [0.5, 1.0, 1.2, 1.5, 2.0, 2.5, 2.61, 3.0, 4.0];

/// A Javanese saron's partials (*Tuning, Timbre, Spectrum, Scale*, §10.2).
const SARON: [f64; 4] = [1.0, 2.76, 4.72, 5.92];

impl Timbre {
    /// A timbre of these partials, or `None` for none at all, more than
    /// [`MAX_PARTIALS`], a ratio that is not positive or an amplitude below 0.
    #[must_use]
    pub fn new(partials: &[Partial]) -> Option<Self> {
        let valid =
            |p: &Partial| p.ratio > 0.0 && p.ratio.is_finite() && p.amp >= 0.0 && p.amp.is_finite();
        if partials.is_empty() || partials.len() > MAX_PARTIALS || !partials.iter().all(valid) {
            return None;
        }
        let mut out = Self {
            partials: [SILENT; MAX_PARTIALS],
            len: partials.len(),
        };
        out.partials[..partials.len()].copy_from_slice(partials);
        Some(out)
    }

    /// Built from `count` ratios, each partial `k` (from 0) at `amp(k)`.
    fn from_fn(count: usize, ratio: impl Fn(usize) -> f64, amp: impl Fn(usize) -> f64) -> Self {
        let len = count.clamp(1, MAX_PARTIALS);
        let mut out = Self {
            partials: [SILENT; MAX_PARTIALS],
            len,
        };
        for (k, p) in out.partials[..len].iter_mut().enumerate() {
            *p = Partial {
                ratio: ratio(k),
                amp: amp(k),
            };
        }
        out
    }

    /// One sine: no partials above the fundamental.
    #[must_use]
    pub fn sine() -> Self {
        Self::harmonic(1)
    }

    /// `count` harmonics, 1, 2, 3 … times the fundamental, each 0.88 as
    /// loud as the one below: Sethares' harmonic timbre (6 of them in his
    /// book's curves).
    #[must_use]
    pub fn harmonic(count: usize) -> Self {
        Self::stretched(count, 2.0)
    }

    /// Harmonics stretched so the second partial lies at `octave` instead
    /// of 2: partial k at k^log₂(octave). Below 2 they are compressed. The
    /// smooth octave moves with them, to `octave`.
    #[must_use]
    pub fn stretched(count: usize, octave: f64) -> Self {
        let power = octave.log2();
        Self::from_fn(count, |k| ((k + 1) as f64).powf(power), rolled_off)
    }

    /// The odd harmonics, 1, 3, 5 … times the fundamental at 1/n: a square
    /// wave's, the kind of spectrum a clarinet's low register has.
    #[must_use]
    pub fn odd(count: usize) -> Self {
        Self::from_fn(count, |k| (2 * k + 1) as f64, |k| 1.0 / (2 * k + 1) as f64)
    }

    /// A free bar's first `count` modes (at most 6): a xylophone bar before
    /// it is tuned.
    #[must_use]
    pub fn bar(count: usize) -> Self {
        Self::listed(&BAR, count)
    }

    /// A circular membrane's first `count` modes (at most 10): a drum head.
    #[must_use]
    pub fn membrane(count: usize) -> Self {
        Self::listed(&MEMBRANE, count)
    }

    /// A church bell's first `count` partials (at most 9), from the hum an
    /// octave below the strike note.
    #[must_use]
    pub fn bell(count: usize) -> Self {
        Self::listed(&BELL, count)
    }

    /// A saron's `count` partials (at most 4): the gamelan's metallophone.
    #[must_use]
    pub fn saron(count: usize) -> Self {
        Self::listed(&SARON, count)
    }

    /// The first `count` of a table's ratios, rolled off.
    fn listed(table: &[f64], count: usize) -> Self {
        Self::from_fn(count.min(table.len()), |k| table[k], rolled_off)
    }

    /// The partials, lowest first as they were given.
    #[must_use]
    pub fn partials(&self) -> &[Partial] {
        &self.partials[..self.len]
    }

    /// How rough one note on `f` is by itself: its partials against each other.
    fn within(&self, f: f64) -> f64 {
        let p = self.partials();
        let mut sum = 0.0;
        for (i, a) in p.iter().enumerate() {
            for b in &p[i + 1..] {
                sum += pair(a.ratio * f, a.amp, b.ratio * f, b.amp);
            }
        }
        sum
    }

    /// How rough two notes on `f` and `g` are against each other.
    fn across(&self, f: f64, g: f64) -> f64 {
        let p = self.partials();
        let mut sum = 0.0;
        for a in p {
            for b in p {
                sum += pair(a.ratio * f, a.amp, b.ratio * g, b.amp);
            }
        }
        sum
    }

    /// How rough these notes are together, one fundamental each: every pair
    /// of partials in the sound, within a note and across notes.
    #[must_use]
    pub fn dissonance(&self, fundamentals: &[f64]) -> f64 {
        let mut sum = 0.0;
        for (i, &f) in fundamentals.iter().enumerate() {
            sum += self.within(f);
            for &g in &fundamentals[i + 1..] {
                sum += self.across(f, g);
            }
        }
        sum
    }

    /// The dissonance of two notes, `f0` and one `from_cents + i·step_cents`
    /// above it, into `out[i]`, for as many points as `out` holds.
    pub fn curve(&self, f0: f64, from_cents: f64, step_cents: f64, out: &mut [f64]) {
        let base = self.within(f0);
        for (i, d) in out.iter_mut().enumerate() {
            let g = above(f0, from_cents + i as f64 * step_cents);
            *d = base + self.within(g) + self.across(f0, g);
        }
    }

    /// The dissonance of three notes over an `n` × `n` grid, into
    /// `out[y·n + x]`: one note on `f0`, one `x·step_cents` above it and one
    /// `y·step_cents` above it, as Sethares draws his dissonance surfaces.
    /// Swapping the upper two changes nothing, so the grid is symmetric about
    /// its diagonal, where they coincide.
    ///
    /// Returns `false`, writing nothing, when `n` is 0 or above [`MAX_SIDE`]
    /// or `out` holds fewer than n² points.
    pub fn landscape(&self, f0: f64, step_cents: f64, n: usize, out: &mut [f64]) -> bool {
        if n == 0 || n > MAX_SIDE || out.len() < n * n {
            return false;
        }
        // Everything but the upper two notes against each other depends on
        // one note's height alone: work it out once per height.
        let mut alone = [0.0; MAX_SIDE];
        for (k, a) in alone[..n].iter_mut().enumerate() {
            let g = above(f0, k as f64 * step_cents);
            *a = self.within(g) + self.across(f0, g);
        }
        let base = self.within(f0);
        for x in 0..n {
            let mid = above(f0, x as f64 * step_cents);
            for y in x..n {
                let top = above(f0, y as f64 * step_cents);
                let d = base + alone[x] + alone[y] + self.across(mid, top);
                out[y * n + x] = d;
                out[x * n + y] = d;
            }
        }
        true
    }
}

/// The loudness of partial `k`, counted from 0, where no source gives one.
fn rolled_off(k: usize) -> f64 {
    ROLL_OFF.powi(k as i32)
}

/// `f` raised by `cents`.
fn above(f: f64, cents: f64) -> f64 {
    f * (cents / 1200.0).exp2()
}

/// The valleys of a curve: its local minima at least `prominence` deep,
/// as a fraction of the curve's whole range, into `out` as indices, lowest
/// index first. Returns how many it wrote; `out` caps them.
///
/// A valley's depth is how far the curve must climb from it, on its lower
/// side, before it can reach a point lower still (or the curve's end). A
/// valley needs a point on either side, so a curve's ends never are one:
/// sample a little beyond the range that matters, below the unison too.
///
/// ```
/// use music_core::dissonance::valleys;
///
/// let curve = [2.0, 0.0, 3.0, 1.0, 1.1, 2.0, 0.5, 4.0];
/// let mut out = [0; 4];
/// let n = valleys(&curve, 0.1, &mut out);
/// assert_eq!(&out[..n], &[1, 3, 6]);
/// ```
pub fn valleys(curve: &[f64], prominence: f64, out: &mut [usize]) -> usize {
    let (lo, hi) = curve
        .iter()
        .fold((f64::INFINITY, f64::NEG_INFINITY), |(lo, hi), &v| {
            (lo.min(v), hi.max(v))
        });
    let need = prominence * (hi - lo);
    // From `v`, the highest point passed before a lower one or the end.
    let climb = |v: f64, side: &mut dyn Iterator<Item = &f64>| {
        let mut peak = v;
        for &c in side {
            if c < v {
                break;
            }
            peak = peak.max(c);
        }
        peak - v
    };
    let mut count = 0;
    for (i, w) in curve.windows(3).enumerate() {
        let [before, v, after] = [w[0], w[1], w[2]];
        if count == out.len() {
            break;
        }
        if !(before > v && after >= v) {
            continue;
        }
        let depth =
            climb(v, &mut curve[..=i].iter().rev()).min(climb(v, &mut curve[i + 2..].iter()));
        if depth > 0.0 && depth >= need {
            out[count] = i + 1;
            count += 1;
        }
    }
    count
}
