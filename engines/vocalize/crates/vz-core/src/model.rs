// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The vowel: five formants, and the one filter both engines are made from.

A VOICE IS A SOURCE AND A FILTER. The vocal folds buzz; the throat and the
mouth resonate, and their resonances -- the formants F1..F5 -- are what makes
an A an A. F1 and F2 alone set the five vowels apart. This module knows where
they lie, moves between them, and turns them into a filter.

THE DATA are measurements, the men's averages of Hillenbrand, Getty, Clark and
Wheeler, "Acoustic characteristics of American English vowels", J. Acoust.
Soc. Am. 97(5), 1995, at the vowels nearest the five a singer means: A /ɑ/,
E /e/, I /i/, O /o/, U /u/. F4 and F5 vary little between vowels and are taken
as constants. The bandwidths are the classic synthesis values (Klatt 1980)
around which Resonance turns.

THE PATH. Vowel 0..4 walks A-E-I-O-U; between two vowels each formant moves on
the Bark scale (Traunmüller 1990), the ear's own, so a glide from O to U
sounds even rather than rushed at one end. Size scales every formant: +100 %
is a vocal tract 1.25 times as long (frequencies x0.8), -100 % one as much
shorter. Resonance scales the bandwidths, x4 at 0 % to x1/4 at 100 %.

ONE FILTER, TWO ENGINES. The vowel is a cascade of five two-pole resonators,
each with unity gain at DC (Klatt's form):

```text
  y[n] = A x[n] + B y[n-1] + C y[n-2],   B = 2ρ cos θ,  C = -ρ²,  A = 1 - B - C
```

with θ = 2π F/fs and ρ = e^(-π BW/fs). A cascade needs no table of formant
levels: the levels follow from the frequencies, as they do in a throat. The
Time engine runs it (`Pole::run`); the Spectral engine multiplies by its
magnitude (`Pole::ln_gain`). It is the same filter, so the two engines aim at
the same vowel.

THE MORPH: HOW FAR FROM THE UNIT CIRCLE. Vocal moves a pole radius from 0 (no
resonance: A = 1, B = C = 0, the identity, exactly) to ρ, along

```text
  ρ(v) = 1 - (1 - ρ)^v
```

-- the pole's distance from the unit circle morphs geometrically, so a peak's
height in dB grows in proportion to v. The Time engine's flattener uses the
same curve (`reach`), so both halves of the morph travel together.
*/

/// How many formants a vowel has here.
pub const FORMANTS: usize = 5;

/// The vowels' formant frequencies in Hz, A E I O U (see the module note).
pub const VOWELS: [[f64; FORMANTS]; 5] = [
    [768.0, 1333.0, 2522.0, 3500.0, 4500.0], // A, /ɑ/ as in "hod"
    [476.0, 2089.0, 2691.0, 3500.0, 4500.0], // E, /e/ as in "hayed"
    [342.0, 2322.0, 3000.0, 3500.0, 4500.0], // I, /i/ as in "heed"
    [497.0, 910.0, 2459.0, 3500.0, 4500.0],  // O, /o/ as in "hoed"
    [378.0, 997.0, 2343.0, 3500.0, 4500.0],  // U, /u/ as in "who'd"
];

/// The formants' bandwidths in Hz at Resonance 50 %.
pub const BANDWIDTHS: [f64; FORMANTS] = [60.0, 90.0, 120.0, 200.0, 250.0];

/// The highest a formant may sit, as a fraction of the sample rate: above it
/// the resonator is left out, as the identity.
const HIGHEST: f64 = 0.45;

/// Hz to Bark (Traunmüller 1990).
pub fn bark(hz: f64) -> f64 {
    26.81 * hz / (1960.0 + hz) - 0.53
}

/// Bark to Hz: the inverse of [`bark`].
pub fn hz(bark: f64) -> f64 {
    1960.0 * (bark + 0.53) / (26.28 - bark)
}

/// Where a pole of radius `rho` is at morph `v` (0..1): 0 at 0, `rho` at 1,
/// its distance from the unit circle moving geometrically between.
#[inline]
pub fn reach(rho: f64, v: f64) -> f64 {
    if v <= 0.0 {
        0.0
    } else if v >= 1.0 {
        rho
    } else {
        1.0 - (1.0 - rho).powf(v)
    }
}

/// From `a` to `b` Hz, `t` (0..1) of the way on the Bark scale -- exactly `a`
/// at 0 and `b` at 1, not a round trip's rounding.
fn between(a: f64, b: f64, t: f64) -> f64 {
    if t <= 0.0 {
        a
    } else if t >= 1.0 {
        b
    } else {
        hz(bark(a) + (bark(b) - bark(a)) * t)
    }
}

/// A VOICE: its five vowels' five formants, in Hz, and their bandwidths at
/// Resonance 50 %. The textbook's, or one captured from a recording
/// (voice.rs). Row `v` is vowel `v` (A E I O U), column `k` formant k+1.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct VowelTable {
    pub freq: [[f64; FORMANTS]; 5],
    pub bandwidth: [[f64; FORMANTS]; 5],
}

/// The range a table's numbers are held to (`VowelTable::sanitised`).
const LOWEST_HZ: f64 = 50.0;
const HIGHEST_HZ: f64 = 8000.0;
const NARROWEST_HZ: f64 = 20.0;
const WIDEST_HZ: f64 = 1000.0;
/// How far apart two formants of a vowel stay, at least.
const APART_HZ: f64 = 50.0;

impl VowelTable {
    /// The textbook voice: Hillenbrand et al.'s men, Klatt's bandwidths.
    pub const TEXTBOOK: VowelTable = VowelTable { freq: VOWELS, bandwidth: [BANDWIDTHS; 5] };

    /// `t` (0..1) of the way from this voice to `other`: every formant on the
    /// Bark scale, every bandwidth linearly. Exactly `self` at 0 and `other`
    /// at 1.
    pub fn blend(&self, other: &VowelTable, t: f64) -> VowelTable {
        let t = if t.is_nan() { 0.0 } else { t.clamp(0.0, 1.0) };
        let mut out = *self;
        for v in 0..5 {
            for k in 0..FORMANTS {
                out.freq[v][k] = between(self.freq[v][k], other.freq[v][k], t);
                let (a, b) = (self.bandwidth[v][k], other.bandwidth[v][k]);
                out.bandwidth[v][k] = if t >= 1.0 { b } else { a + (b - a) * t };
            }
        }
        out
    }

    /// The table made safe to sing from, or `None` for one with a number that
    /// is not a number: frequencies within 50 Hz..8 kHz and rising through
    /// each vowel at least 50 Hz apart, bandwidths within 20 Hz..1 kHz. What
    /// a saved set or a C caller hands in passes through this first.
    pub fn sanitised(&self) -> Option<VowelTable> {
        let all = self.freq.iter().chain(self.bandwidth.iter()).flatten();
        if !all.into_iter().all(|x| x.is_finite()) {
            return None;
        }
        let mut out = *self;
        for v in 0..5 {
            let mut floor = LOWEST_HZ - APART_HZ;
            for k in 0..FORMANTS {
                let f = out.freq[v][k].clamp(LOWEST_HZ, HIGHEST_HZ).max(floor + APART_HZ);
                out.freq[v][k] = f;
                floor = f;
                out.bandwidth[v][k] = out.bandwidth[v][k].clamp(NARROWEST_HZ, WIDEST_HZ);
            }
        }
        Some(out)
    }
}

/// One vowel's formants, in Hz.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Formants {
    pub freq: [f64; FORMANTS],
    pub bandwidth: [f64; FORMANTS],
}

impl Formants {
    /// The textbook voice's formants at `vowel` (0..4, A..U), `size`
    /// (-100..100 %) and `resonance` (0..100 %): [`Formants::of`] the
    /// [`VowelTable::TEXTBOOK`].
    pub fn at(vowel: f64, size: f64, resonance: f64) -> Formants {
        Formants::of(&VowelTable::TEXTBOOK, vowel, size, resonance)
    }

    /// The formants of `table`'s voice at `vowel` (0..4, A..U), `size`
    /// (-100..100 %) and `resonance` (0..100 %). Out-of-range values are
    /// clamped.
    pub fn of(table: &VowelTable, vowel: f64, size: f64, resonance: f64) -> Formants {
        let vowel = if vowel.is_nan() { 0.0 } else { vowel.clamp(0.0, 4.0) };
        let from = (vowel.floor() as usize).min(3);
        let t = vowel - from as f64;
        let scale = 1.25f64.powf(-size.clamp(-100.0, 100.0) / 100.0);
        let widen = 4.0f64.powf(1.0 - 2.0 * resonance.clamp(0.0, 100.0) / 100.0);
        let mut f = Formants { freq: [0.0; FORMANTS], bandwidth: [0.0; FORMANTS] };
        for k in 0..FORMANTS {
            let on = between(table.freq[from][k], table.freq[from + 1][k], t);
            let (wa, wb) = (table.bandwidth[from][k], table.bandwidth[from + 1][k]);
            f.freq[k] = on * scale;
            f.bandwidth[k] = (wa + (wb - wa) * t) * widen;
        }
        f
    }

    /// The five resonators at sample rate `fs`, morphed to `v` (0..1).
    pub fn poles(&self, fs: f64, v: f64) -> [Pole; FORMANTS] {
        core::array::from_fn(|k| Pole::new(self.freq[k], self.bandwidth[k], fs, v))
    }
}

/// One two-pole resonator, unity gain at DC.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Pole {
    pub a: f64,
    pub b: f64,
    pub c: f64,
}

impl Pole {
    /// The identity: no resonance at all.
    pub const NONE: Pole = Pole { a: 1.0, b: 0.0, c: 0.0 };

    /// A resonance at `freq` Hz, `bandwidth` Hz wide, at sample rate `fs`,
    /// morphed to `v`. One above the highest a formant may sit is the
    /// identity.
    pub fn new(freq: f64, bandwidth: f64, fs: f64, v: f64) -> Pole {
        if !(freq > 0.0 && freq < HIGHEST * fs) {
            return Pole::NONE;
        }
        let rho = reach((-core::f64::consts::PI * bandwidth / fs).exp(), v);
        if rho == 0.0 {
            return Pole::NONE;
        }
        let b = 2.0 * rho * (core::f64::consts::TAU * freq / fs).cos();
        let c = -rho * rho;
        Pole { a: 1.0 - b - c, b, c }
    }

    /// One sample through the resonator; `y` is its last two outputs. The
    /// identity passes `x` exactly: 1 * x + 0 + 0.
    #[inline]
    pub fn run(&self, x: f64, y: &mut [f64; 2]) -> f64 {
        let out = self.a * x + self.b * y[0] + self.c * y[1];
        y[1] = y[0];
        y[0] = out;
        out
    }

    /// ln |H| at the angle whose cosine and sine are given, for `w` and `2w`:
    /// the resonator's gain at that frequency, as a natural log.
    #[inline]
    pub fn ln_gain(&self, cos1: f64, sin1: f64, cos2: f64, sin2: f64) -> f64 {
        let re = 1.0 - self.b * cos1 - self.c * cos2;
        let im = self.b * sin1 + self.c * sin2;
        self.a.abs().ln() - 0.5 * (re * re + im * im).ln()
    }
}

/// ln |H| of the whole cascade at `hz`, at sample rate `fs`: the vowel's
/// envelope at one frequency.
pub fn ln_gain(poles: &[Pole; FORMANTS], hz: f64, fs: f64) -> f64 {
    let w = core::f64::consts::TAU * hz / fs;
    let (s1, c1) = w.sin_cos();
    let (s2, c2) = (2.0 * w).sin_cos();
    poles.iter().map(|p| p.ln_gain(c1, s1, c2, s2)).sum()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn bark_and_hz_are_inverses_and_bark_rises() {
        let mut last = f64::NEG_INFINITY;
        for i in 1..200 {
            let f = i as f64 * 50.0;
            let z = bark(f);
            assert!(z > last);
            last = z;
            assert!((hz(z) - f).abs() < 1e-9 * f, "{f}");
        }
    }

    #[test]
    fn a_vowel_is_the_table_and_the_way_between_moves_evenly() {
        for (i, row) in VOWELS.iter().enumerate() {
            let f = Formants::at(i as f64, 0.0, 50.0);
            assert_eq!(&f.freq, row);
            assert_eq!(f.bandwidth, BANDWIDTHS);
        }
        /* Every formant moves monotonically from one vowel to the next, and
         * halfway in Bark is halfway between. */
        for (from, pair) in VOWELS.windows(2).enumerate() {
            for (k, (&a, &b)) in pair[0].iter().zip(&pair[1]).enumerate() {
                let mut last = a;
                for s in 1..=20 {
                    let f = Formants::at(from as f64 + s as f64 / 20.0, 0.0, 50.0).freq[k];
                    assert!(if b >= a { f >= last - 1e-9 } else { f <= last + 1e-9 }, "{from} F{}", k + 1);
                    last = f;
                }
                let mid = Formants::at(from as f64 + 0.5, 0.0, 50.0).freq[k];
                assert!((bark(mid) - 0.5 * (bark(a) + bark(b))).abs() < 1e-9);
            }
        }
    }

    #[test]
    fn size_scales_every_formant_and_resonance_every_bandwidth() {
        let big = Formants::at(0.0, 100.0, 50.0);
        let small = Formants::at(0.0, -100.0, 50.0);
        for (k, f) in VOWELS[0].iter().enumerate() {
            assert!((big.freq[k] - f * 0.8).abs() < 1e-9);
            assert!((small.freq[k] - f * 1.25).abs() < 1e-9);
        }
        assert!((Formants::at(0.0, 0.0, 0.0).bandwidth[0] - 240.0).abs() < 1e-9);
        assert!((Formants::at(0.0, 0.0, 100.0).bandwidth[0] - 15.0).abs() < 1e-9);
        /* Out of range, clamped; NaN is A. */
        assert_eq!(Formants::at(9.0, 0.0, 50.0).freq, VOWELS[4]);
        assert_eq!(Formants::at(f64::NAN, 0.0, 50.0).freq, VOWELS[0]);
    }

    #[test]
    fn a_blend_is_exact_at_its_ends_and_even_on_the_bark_scale_between() {
        let mut other = VowelTable::TEXTBOOK;
        for (v, row) in other.freq.iter_mut().enumerate() {
            for f in row.iter_mut() {
                *f *= 1.1 + 0.02 * v as f64;
            }
        }
        other.bandwidth[2][0] = 100.0;
        let t = VowelTable::TEXTBOOK;
        assert_eq!(t.blend(&other, 0.0), t);
        assert_eq!(t.blend(&other, 1.0), other);
        assert_eq!(t.blend(&other, f64::NAN), t);
        let half = t.blend(&other, 0.5);
        let f = half.freq[3][1];
        assert!((bark(f) - 0.5 * (bark(t.freq[3][1]) + bark(other.freq[3][1]))).abs() < 1e-9);
        assert_eq!(half.bandwidth[2][0], 80.0);
        /* The textbook's formants at a vowel of a table are that table's. */
        assert_eq!(Formants::of(&other, 3.0, 0.0, 50.0).freq, other.freq[3]);
    }

    #[test]
    fn a_table_is_made_safe_or_refused() {
        assert_eq!(VowelTable::TEXTBOOK.sanitised(), Some(VowelTable::TEXTBOOK));
        let mut bad = VowelTable::TEXTBOOK;
        bad.freq[1][2] = f64::NAN;
        assert_eq!(bad.sanitised(), None);
        bad.freq[1][2] = f64::INFINITY;
        assert_eq!(bad.sanitised(), None);
        let mut odd = VowelTable::TEXTBOOK;
        odd.freq[0] = [10.0, 5.0, 3000.0, 2990.0, 99_999.0];
        odd.bandwidth[0] = [0.0, 5.0, 2000.0, 100.0, -1.0];
        let s = odd.sanitised().unwrap();
        assert_eq!(s.freq[0], [50.0, 100.0, 3000.0, 3050.0, 8000.0]);
        assert_eq!(s.bandwidth[0], [20.0, 20.0, 1000.0, 100.0, 20.0]);
    }

    #[test]
    fn the_morph_runs_from_the_identity_to_the_vowel() {
        assert_eq!(reach(0.99, 0.0), 0.0);
        assert_eq!(reach(0.99, 1.0), 0.99);
        assert!((reach(0.99, 0.5) - 0.9).abs() < 1e-12);
        let f = Formants::at(0.0, 0.0, 50.0);
        assert!(f.poles(48_000.0, 0.0).iter().all(|p| *p == Pole::NONE));
        /* The identity passes a sample exactly. */
        let mut y = [0.0; 2];
        assert_eq!(Pole::NONE.run(0.123_456_789, &mut y), 0.123_456_789);
        /* A formant out of reach of the rate is left out. */
        assert_eq!(Pole::new(30_000.0, 100.0, 48_000.0, 1.0), Pole::NONE);
        assert_eq!(Pole::new(f64::NAN, 100.0, 48_000.0, 1.0), Pole::NONE);
    }

    #[test]
    fn every_resonator_has_unity_gain_at_dc_and_its_peak_near_its_formant() {
        let fs = 48_000.0;
        for (i, _) in VOWELS.iter().enumerate() {
            let f = Formants::at(i as f64, 0.0, 50.0);
            let poles = f.poles(fs, 1.0);
            assert!(ln_gain(&poles, 0.0, fs).abs() < 1e-9);
            for (k, p) in poles.iter().enumerate() {
                let one = [*p, Pole::NONE, Pole::NONE, Pole::NONE, Pole::NONE];
                let at = ln_gain(&one, f.freq[k], fs);
                assert!(at > ln_gain(&one, f.freq[k] * 0.9, fs) && at > ln_gain(&one, f.freq[k] * 1.1, fs));
            }
        }
    }

    #[test]
    fn the_cascade_run_is_the_cascade_measured() {
        /* The impulse response's spectrum is ln_gain: the Time engine's filter
         * IS the Spectral engine's curve. */
        let fs = 48_000.0;
        let poles = Formants::at(1.4, 30.0, 70.0).poles(fs, 0.8);
        let n = 1 << 14;
        let mut y = [[0.0; 2]; FORMANTS];
        let h: Vec<f64> = (0..n)
            .map(|i| {
                let mut x = if i == 0 { 1.0 } else { 0.0 };
                for (p, s) in poles.iter().zip(y.iter_mut()) {
                    x = p.run(x, s);
                }
                x
            })
            .collect();
        for hz in [100.0, 450.0, 1200.0, 2400.0, 3300.0, 6000.0] {
            let w = core::f64::consts::TAU * hz / fs;
            let (re, im) = h.iter().enumerate().fold((0.0, 0.0), |(re, im), (i, v)| {
                let (s, c) = (w * i as f64).sin_cos();
                (re + v * c, im - v * s)
            });
            let measured = 0.5 * (re * re + im * im).ln();
            assert!((measured - ln_gain(&poles, hz, fs)).abs() < 1e-6, "{hz} Hz");
        }
    }
}
