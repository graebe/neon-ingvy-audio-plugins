// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
Linear prediction: the input's own colour, measured and taken out.

WHAT IT MEASURES. Every few milliseconds the last 20 ms of a channel are
windowed and autocorrelated, and Levinson-Durbin solves for the predictor
A(z) = 1 + a1 z^-1 + ... + ap z^-p whose error is as small as it can be.
1/A(z) is the input's spectral envelope -- its resonances, its tilt; A(z) run
as a filter takes them out and leaves the input flat, an excitation the vowel
can be put on. The order follows the rate (one coefficient per 3 kHz, 8..32):
enough to follow a voice's or an instrument's resonances, too few to follow
its harmonics.

TWO GUARDS. A lag window (Gaussian, 40 Hz) widens every peak a little, so a
single loud harmonic is not mistaken for a resonance; and the zero lag is
raised by 1e-4 -- noise 40 dB down -- so no band is ever lifted more than
40 dB, and silence or a pure tone stays well-conditioned. Silence itself gives
the identity.

HOW FAR (`gamma`). The flattener runs A(z/γ): each coefficient ak times γ^k,
which moves every zero towards the origin by γ. γ = 0 is the identity,
exactly; γ = 1 is the full flattening. The engine sets γ from Vocal along the
same curve as the vowel's poles (model::reach), for a zero at radius 0.95.

WHY AN FIR, AND NO LATTICE. A(z) is a polynomial: its filter has no feedback,
so it is stable whatever its coefficients are, and they can be moved
sample by sample -- here linearly from the last analysis to the new one over
each hop -- without a thought for stability.
*/

/// The highest order at any rate.
pub const MAX_ORDER: usize = 32;

/// The analysis window's longest, in samples (20 ms at 192 kHz, and some).
pub const MAX_WINDOW: usize = 4096;

/// Noise added to the zero lag, as a fraction of it: -40 dB.
const NOISE_FLOOR: f64 = 1e-4;

/// The lag window's width, in Hz.
const LAG_HZ: f64 = 40.0;

/// The radius γ is aimed for: Vocal moves a zero here as far, in dB, as it
/// moves a formant's pole.
pub const REFERENCE_RADIUS: f64 = 0.95;

/// The analysis' sizes at one sample rate.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Sizes {
    pub order: usize,
    pub window: usize,
    /// Samples between analyses.
    pub hop: usize,
}

impl Sizes {
    pub fn at(fs: f64) -> Sizes {
        Sizes {
            order: ((fs / 3000.0).round() as usize).clamp(8, MAX_ORDER),
            window: ((0.02 * fs).round() as usize).clamp(64, MAX_WINDOW),
            hop: ((0.005 * fs).round() as usize).clamp(16, 1024),
        }
    }
}

/// A predictor: A(z)'s coefficients, a[0] = 1.
pub type Coefficients = [f64; MAX_ORDER + 1];

/// The identity: A(z) = 1.
pub const IDENTITY: Coefficients = {
    let mut a = [0.0; MAX_ORDER + 1];
    a[0] = 1.0;
    a
};

/// The analysis' fixed tables and its scratch, shared by every channel.
pub struct Analysis {
    sizes: Sizes,
    /// The Hann window over `sizes.window` samples.
    window: Box<[f64; MAX_WINDOW]>,
    lag: [f64; MAX_ORDER + 1],
    frame: Box<[f64; MAX_WINDOW]>,
}

impl Analysis {
    /// Allocates: not the audio thread.
    pub fn new(fs: f64) -> Analysis {
        let mut a = Analysis {
            sizes: Sizes::at(fs),
            window: Box::new([0.0; MAX_WINDOW]),
            lag: [0.0; MAX_ORDER + 1],
            frame: Box::new([0.0; MAX_WINDOW]),
        };
        a.set_rate(fs);
        a
    }

    /// The tables for another rate, in place: allocates nothing.
    pub fn set_rate(&mut self, fs: f64) {
        self.sizes = Sizes::at(fs);
        let n = self.sizes.window;
        for (i, w) in self.window[..n].iter_mut().enumerate() {
            *w = 0.5 - 0.5 * (core::f64::consts::TAU * (i as f64 + 0.5) / n as f64).cos();
        }
        for (k, l) in self.lag.iter_mut().enumerate() {
            let x = core::f64::consts::TAU * LAG_HZ * k as f64 / fs;
            *l = (-0.5 * x * x).exp();
        }
    }

    pub fn sizes(&self) -> Sizes {
        self.sizes
    }

    /// The predictor of the last `window` samples `recent` yields, newest
    /// first (`recent(0)` is the latest), into `out`.
    pub fn analyse(&mut self, recent: impl Fn(usize) -> f64, out: &mut Coefficients) {
        let Sizes { order, window: n, .. } = self.sizes;
        for (i, (x, w)) in self.frame[..n].iter_mut().zip(self.window[..n].iter()).enumerate() {
            *x = recent(n - 1 - i) * w;
        }
        let mut r = [0.0; MAX_ORDER + 1];
        let frame = &self.frame[..n];
        for (k, rk) in r[..=order].iter_mut().enumerate() {
            *rk = frame[k..].iter().zip(frame).map(|(a, b)| a * b).sum::<f64>() * self.lag[k];
        }
        r[0] *= 1.0 + NOISE_FLOOR;
        levinson(&r[..=order], out);
    }
}

/// Levinson-Durbin: the predictor of order `r.len() - 1` for autocorrelation
/// `r`, into `a` (the rest of it zero). The identity for a zero (or not
/// finite) `r[0]`. Returns the prediction error's power.
pub fn levinson(r: &[f64], a: &mut Coefficients) -> f64 {
    *a = IDENTITY;
    let mut err = r[0];
    if !(err > 1e-30 && err.is_finite()) {
        return 0.0;
    }
    for i in 1..r.len() {
        let acc = r[i] + (1..i).map(|j| a[j] * r[i - j]).sum::<f64>();
        let k = -acc / err;
        for j in 1..=i / 2 {
            let (x, y) = (a[j], a[i - j]);
            a[j] = x + k * y;
            a[i - j] = y + k * x;
        }
        a[i] = k;
        err *= 1.0 - k * k;
        if err <= 0.0 {
            break;
        }
    }
    err
}

/// ln |1 / A| at the angle `w`: the envelope the predictor models, as a
/// natural log, up to its overall level.
pub fn ln_envelope(a: &Coefficients, order: usize, w: f64) -> f64 {
    let (mut re, mut im) = (0.0, 0.0);
    for (k, ak) in a[..=order].iter().enumerate() {
        let (s, c) = (w * k as f64).sin_cos();
        re += ak * c;
        im -= ak * s;
    }
    -0.5 * (re * re + im * im).max(1e-300).ln()
}

/// One channel's flattener: the predictor moving from the last analysis to
/// the latest over one hop, run as A(z/γ).
#[derive(Clone, Debug)]
pub struct Flattener {
    pub from: Coefficients,
    pub to: Coefficients,
}

impl Default for Flattener {
    fn default() -> Self {
        Flattener { from: IDENTITY, to: IDENTITY }
    }
}

impl Flattener {
    /// The latest analysis becomes where the next hop starts, and `next`
    /// where it ends.
    pub fn advance(&mut self, next: &Coefficients) {
        self.from = self.to;
        self.to = *next;
    }

    /// One sample: `recent(k)` is the input k samples ago (0 is this one),
    /// `t` how far through the hop (0..1), `powers[k]` γ^k. γ = 0 passes the
    /// sample exactly.
    #[inline]
    pub fn run(&self, recent: impl Fn(usize) -> f64, order: usize, t: f64, powers: &[f64; MAX_ORDER + 1]) -> f64 {
        let taps = self.from[1..=order].iter().zip(&self.to[1..=order]).zip(&powers[1..=order]);
        let mut e = recent(0);
        for (k, ((from, to), gamma)) in taps.enumerate() {
            e += (from + (to - from) * t) * gamma * recent(k + 1);
        }
        e
    }
}

/// γ^k for k = 0..=MAX_ORDER, γ from morph `v` (see the module note).
pub fn powers(v: f64) -> [f64; MAX_ORDER + 1] {
    let gamma = crate::model::reach(REFERENCE_RADIUS, v) / REFERENCE_RADIUS;
    let mut p = [0.0; MAX_ORDER + 1];
    let mut g = 1.0;
    for x in p.iter_mut() {
        *x = g;
        g *= gamma;
    }
    p
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A known AR(4) process: two resonances, at 0.1 and 0.3 of the rate.
    fn ar4() -> [f64; 5] {
        /* (1 - 2 r cos θ z^-1 + r² z^-2) for two pole pairs, multiplied out. */
        let pair = |r: f64, th: f64| [1.0, -2.0 * r * th.cos(), r * r];
        let (p, q) = (pair(0.9, core::f64::consts::TAU * 0.1), pair(0.8, core::f64::consts::TAU * 0.3));
        let mut a = [0.0; 5];
        for i in 0..3 {
            for j in 0..3 {
                a[i + j] += p[i] * q[j];
            }
        }
        a
    }

    #[test]
    fn levinson_recovers_a_known_process() {
        let truth = ar4();
        let mut rng = fastrand::Rng::with_seed(7);
        let n = 200_000;
        let mut x = vec![0.0f64; n];
        for i in 0..n {
            let mut v = rng.f64() * 2.0 - 1.0;
            for k in 1..5 {
                if i >= k {
                    v -= truth[k] * x[i - k];
                }
            }
            x[i] = v;
        }
        let mut r = [0.0; 5];
        for (k, rk) in r.iter_mut().enumerate() {
            *rk = x[k..].iter().zip(&x).map(|(a, b)| a * b).sum();
        }
        let mut a = IDENTITY;
        let err = levinson(&r, &mut a);
        for k in 0..5 {
            assert!((a[k] - truth[k]).abs() < 0.02, "a{k}: {} vs {}", a[k], truth[k]);
        }
        assert!(a[5..].iter().all(|&c| c == 0.0));
        /* The residual is the driving noise: uniform on ±1, power 1/3. */
        assert!((err / n as f64 - 1.0 / 3.0).abs() < 0.01);
    }

    #[test]
    fn silence_and_nonsense_are_the_identity() {
        let mut a = [9.0; MAX_ORDER + 1];
        assert_eq!(levinson(&[0.0, 0.0, 0.0], &mut a), 0.0);
        assert_eq!(a, IDENTITY);
        assert_eq!(levinson(&[f64::NAN, 1.0], &mut a), 0.0);
        assert_eq!(a, IDENTITY);
        let mut an = Analysis::new(48_000.0);
        an.analyse(|_| 0.0, &mut a);
        assert_eq!(a, IDENTITY);
    }

    #[test]
    fn a_perfectly_predictable_input_stops_early_and_stays_finite() {
        /* r of a constant: every lag equal. The first reflection is -1 and
         * the error goes to zero. */
        let mut a = IDENTITY;
        let err = levinson(&[1.0, 1.0, 1.0, 1.0], &mut a);
        assert_eq!(err, 0.0);
        assert!(a.iter().all(|c| c.is_finite()));
    }

    #[test]
    fn the_flattened_input_is_flat() {
        /* A resonant input, through the flattener at γ = 1, has a much
         * flatter spectrum than it came in with. */
        let fs = 48_000.0;
        let truth = ar4();
        let mut rng = fastrand::Rng::with_seed(11);
        let n = 48_000;
        let mut x = vec![0.0f64; n];
        for i in 0..n {
            let mut v = rng.f64() * 2.0 - 1.0;
            for k in 1..5 {
                if i >= k {
                    v -= truth[k] * x[i - k];
                }
            }
            x[i] = v;
        }
        let mut an = Analysis::new(fs);
        let mut a = IDENTITY;
        an.analyse(|k| x[n - 1 - k], &mut a);
        let order = an.sizes().order;
        let spread = |a: &Coefficients| {
            let ln: Vec<f64> = (1..64).map(|i| ln_envelope(a, order, core::f64::consts::PI * i as f64 / 64.0)).collect();
            ln.iter().cloned().fold(f64::MIN, f64::max) - ln.iter().cloned().fold(f64::MAX, f64::min)
        };
        /* The envelope the analysis found spans many dB (2 nepers is 17 dB):
         * what it takes out. */
        assert!(spread(&a) > 2.0, "{}", spread(&a));

        let f = Flattener { from: a, to: a };
        let all = powers(1.0);
        let e: Vec<f64> = (order..n).map(|i| f.run(|k| x[i - k], order, 0.5, &all)).collect();
        let mut again = IDENTITY;
        an.analyse(|k| e[e.len() - 1 - k], &mut again);
        assert!(spread(&again) < 0.25 * spread(&a), "{} -> {}", spread(&a), spread(&again));
    }

    #[test]
    fn gamma_zero_is_the_identity_and_one_the_whole_predictor() {
        let none = powers(0.0);
        assert_eq!(none[0], 1.0);
        assert!(none[1..].iter().all(|&p| p == 0.0));
        let all = powers(1.0);
        assert!(all.iter().all(|&p| (p - 1.0).abs() < 1e-12));
        let f = Flattener { from: [0.3; MAX_ORDER + 1], to: [-0.2; MAX_ORDER + 1] };
        assert_eq!(f.run(|k| [0.75, 0.1, -0.4][k.min(2)], MAX_ORDER, 0.3, &none), 0.75);
        /* γ grows with v. */
        let mut last = 0.0;
        for i in 1..=10 {
            let g = powers(i as f64 / 10.0)[1];
            assert!(g > last);
            last = g;
        }
    }

    #[test]
    fn the_coefficients_move_across_the_hop() {
        let mut f = Flattener::default();
        let mut next = IDENTITY;
        next[1] = -0.5;
        f.advance(&next);
        let all = powers(1.0);
        let x = |_: usize| 1.0;
        assert_eq!(f.run(x, 1, 0.0, &all), 1.0);
        assert!((f.run(x, 1, 0.5, &all) - 0.75).abs() < 1e-12);
        assert!((f.run(x, 1, 1.0, &all) - 0.5).abs() < 1e-12);
        assert_eq!(Sizes::at(48_000.0), Sizes { order: 16, window: 960, hop: 240 });
        assert_eq!(Sizes::at(192_000.0).order, MAX_ORDER);
    }
}
