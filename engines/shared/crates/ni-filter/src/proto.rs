// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The analog prototypes: a normalised low-pass of each family, as the poles
(and zeros) of second-order factors and at most one real pole.

```text
  H(s) = dc · Π w0²·(s² + wz²) / (wz²·(s² + (w0/Q)·s + w0²)) · Π σ / (s + σ)
```

each factor at unity gain at DC, `dc` the passband's level there (below 1 for
an even Chebyshev I or elliptic, whose passband peaks at 1 and starts at the
bottom of its ripple). A factor without a zero is all-pole.

FACTORS KEEP THEIR CLOSED-FORM ORDER. Factor `i` is pole pair `i` of the
family's formula, never sorted by Q: a glide of Ripple must not swap two
sections between one design and the next.

NORMALISED TO ITS -3 dB POINT. `w3` is where the prototype as designed falls
to -3 dB, in closed form for every family (Bessel's from its table); a filter at Freq maps 1 rad/s of the NORMALISED prototype there,
so every family's Freq is its -3 dB point and switching family leaves the
curve where the handle is. Linkwitz-Riley is the exception that the
crossover asks for: -6 dB at 1, where a low and a high cut sum flat.
*/

use crate::elliptic;
use num_complex::Complex64;

/// The highest order a prototype has.
pub const MAX_ORDER: usize = 8;

/// Bessel's prototype at orders 1 to 8, which nothing parameterises: the
/// reverse Bessel polynomial's roots in the upper half plane (re, im), nearest
/// the jω axis first as Butterworth's are; its real root's distance σ, at odd
/// orders; and where it crosses -3 dB. A constant, so a design on the audio
/// thread never solves a polynomial; the tests hold it to the polynomial.
#[allow(clippy::type_complexity)]
pub(crate) const BESSEL: [(&[(f64, f64)], Option<f64>, f64); MAX_ORDER] = [
    (&[], Some(1.0), 1.0),
    (&[(-1.5, 0.8660254037844384)], None, 1.3616541287161306),
    (&[(-1.8389073226869574, 1.7543809597837217)], Some(2.322185354626086), 1.755672368681211),
    (&[(-2.1037893971796278, 2.657418041856752), (-2.8962106028203722, 0.8672341289345048)], None, 2.1139176749042154),
    (&[(-2.3246743031816433, 3.571022920337976), (-3.351956399153533, 1.7426614161831977)], Some(3.64673859532964), 2.4274107021526277),
    (
        &[(-2.515932247810823, 4.492672953653944), (-3.7357083563258233, 2.6262723114471216), (-4.24835939586337, 0.8675096732313607)],
        None,
        2.7033950612029205,
    ),
    (
        &[(-2.685676878943261, 5.42069413071675), (-4.070139163638138, 3.5171740477097417), (-4.75829052815464, 1.739286061130533)],
        Some(4.971786858527804),
        2.9517221470386934,
    ),
    (
        &[
            (-2.83898394889764, 6.353911298604889),
            (-4.3682892172023156, 4.414442500471423),
            (-5.204840790637162, 2.6161751526426387),
            (-5.587886043262967, 0.867614445353243),
        ],
        None,
        3.1796172375107274,
    ),
];

/// The families, in the order the plugins list them.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Family {
    /// Maximally flat.
    Butterworth,
    /// Maximally flat delay: the cleanest step, the slowest slope.
    Bessel,
    /// Equiripple passband (Ripple), a steeper knee.
    ChebyshevI,
    /// Equiripple stopband (Stopband), a flat passband, zeros in the stop.
    ChebyshevII,
    /// Both ripples: the steepest knee an order can have.
    Elliptic,
    /// Butterworth squared: -6 dB at Freq, so complementary cuts sum flat.
    LinkwitzRiley,
}

impl Family {
    pub const ALL: [Family; 6] = [
        Family::Butterworth,
        Family::Bessel,
        Family::ChebyshevI,
        Family::ChebyshevII,
        Family::Elliptic,
        Family::LinkwitzRiley,
    ];

    /// The family at a choice index; Butterworth for one out of range.
    pub fn from_index(i: usize) -> Family {
        Family::ALL.get(i).copied().unwrap_or(Family::Butterworth)
    }

    /// Whether Ripple shapes it.
    pub fn uses_ripple(self) -> bool {
        matches!(self, Family::ChebyshevI | Family::Elliptic)
    }

    /// Whether Stopband shapes it.
    pub fn uses_stopband(self) -> bool {
        matches!(self, Family::ChebyshevII | Family::Elliptic)
    }
}

/// One second-order factor.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Pair {
    /// Its poles' distance from the origin.
    pub w0: f64,
    /// Their Q: w0 over twice the distance from the jω axis.
    pub q: f64,
    /// A pair of zeros on the jω axis at ±j·wz, if it has them.
    pub zero: Option<f64>,
}

impl Pair {
    const NONE: Pair = Pair { w0: 1.0, q: core::f64::consts::FRAC_1_SQRT_2, zero: None };

    /// The pole in the upper half plane; on the real axis for Q = 1/2.
    pub fn pole(&self) -> Complex64 {
        let re = -self.w0 / (2.0 * self.q);
        let im = (self.w0 * self.w0 - re * re).max(0.0).sqrt();
        Complex64::new(re, im)
    }

    fn of_pole(p: Complex64, zero: Option<f64>) -> Pair {
        let w0 = p.norm();
        Pair { w0, q: w0 / (-2.0 * p.re), zero }
    }

    /// Its magnitude at s = j·x, at unity at DC.
    fn magnitude(&self, x: f64) -> f64 {
        let w2 = self.w0 * self.w0;
        let poles = Complex64::new(w2 - x * x, self.w0 * x / self.q).norm();
        let zeros = match self.zero {
            Some(wz) => (wz * wz - x * x).abs() / (wz * wz),
            None => 1.0,
        };
        w2 * zeros / poles
    }
}

/// A normalised analog low-pass.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Prototype {
    family: Family,
    order: usize,
    pairs: [Pair; MAX_ORDER / 2],
    n_pairs: usize,
    /// A single real pole at -σ.
    real: Option<f64>,
    dc: f64,
    w3: f64,
}

impl Prototype {
    /// The prototype of `family` at `order` (1..8, clamped), with `ripple_db`
    /// (0.01..3, Chebyshev I and Elliptic) and `stopband_db` (20..120,
    /// Chebyshev II and Elliptic). An odd Linkwitz-Riley order rounds up. Not
    /// cheap: what changes rarely.
    pub fn new(family: Family, order: usize, ripple_db: f64, stopband_db: f64) -> Prototype {
        let order = order.clamp(1, MAX_ORDER);
        let ripple = if ripple_db.is_finite() { ripple_db.clamp(0.01, 3.0) } else { 1.0 };
        let stop = if stopband_db.is_finite() { stopband_db.clamp(20.0, 120.0) } else { 60.0 };
        let mut p = Prototype {
            family,
            order,
            pairs: [Pair::NONE; MAX_ORDER / 2],
            n_pairs: 0,
            real: None,
            dc: 1.0,
            w3: 1.0,
        };
        match family {
            Family::Butterworth => p.butterworth(order),
            Family::Bessel => p.bessel(order),
            Family::ChebyshevI => p.chebyshev1(order, ripple),
            Family::ChebyshevII => p.chebyshev2(order, stop),
            Family::Elliptic if order >= 2 => p.elliptic(order, ripple, stop),
            Family::Elliptic => p.butterworth(1),
            Family::LinkwitzRiley => p.linkwitz_riley(order),
        }
        p
    }

    pub fn family(&self) -> Family {
        self.family
    }

    /// The order it was asked for (Linkwitz-Riley's rounded up).
    pub fn order(&self) -> usize {
        self.order
    }

    pub fn pairs(&self) -> &[Pair] {
        &self.pairs[..self.n_pairs]
    }

    pub fn real(&self) -> Option<f64> {
        self.real
    }

    /// The passband's level at DC.
    pub fn dc(&self) -> f64 {
        self.dc
    }

    /// Where the prototype as designed is at -3 dB: 1 rad/s of the
    /// normalised prototype is this one's `w3`.
    pub fn w3(&self) -> f64 {
        self.w3
    }

    /// The magnitude of the NORMALISED prototype at s = j·x.
    pub fn magnitude(&self, x: f64) -> f64 {
        self.raw_magnitude(x * self.w3)
    }

    fn raw_magnitude(&self, x: f64) -> f64 {
        let real = self.real.map_or(1.0, |s| s / (s * s + x * x).sqrt());
        self.pairs().iter().fold(self.dc * real, |m, p| m * p.magnitude(x))
    }

    fn push(&mut self, pair: Pair) {
        self.pairs[self.n_pairs] = pair;
        self.n_pairs += 1;
    }

    fn butterworth(&mut self, n: usize) {
        for k in 0..n / 2 {
            let t = core::f64::consts::PI * (2 * k + 1) as f64 / (2 * n) as f64;
            self.push(Pair { w0: 1.0, q: 1.0 / (2.0 * t.sin()), zero: None });
        }
        if n % 2 == 1 {
            self.real = Some(1.0);
        }
    }

    fn linkwitz_riley(&mut self, order: usize) {
        let half = order.div_ceil(2);
        self.order = 2 * half;
        let mut bw = Prototype::new(Family::Butterworth, half, 1.0, 60.0);
        for _ in 0..2 {
            for i in 0..bw.n_pairs {
                self.push(bw.pairs[i]);
            }
        }
        /* Two real poles at -1 are one factor: Q 1/2. */
        if bw.real.take().is_some() {
            self.push(Pair { w0: 1.0, q: 0.5, zero: None });
        }
    }

    /// The reverse Bessel polynomial's roots, from [`BESSEL`].
    fn bessel(&mut self, n: usize) {
        let (upper, real, w3) = BESSEL[n - 1];
        for &(re, im) in upper {
            self.push(Pair::of_pole(Complex64::new(re, im), None));
        }
        self.real = real;
        self.w3 = w3;
    }

    fn chebyshev1(&mut self, n: usize, ripple: f64) {
        let e = (10f64.powf(ripple / 10.0) - 1.0).sqrt();
        let a = (1.0 / e).asinh() / n as f64;
        for k in 0..n / 2 {
            let t = core::f64::consts::PI * (2 * k + 1) as f64 / (2 * n) as f64;
            self.push(Pair::of_pole(Complex64::new(-a.sinh() * t.sin(), a.cosh() * t.cos()), None));
        }
        if n % 2 == 1 {
            self.real = Some(a.sinh());
        }
        self.dc = if n % 2 == 1 { 1.0 } else { 1.0 / (1.0 + e * e).sqrt() };
        /* |H|² = 1 / (1 + ε²·T_n(x)²) is 1/2 where T_n(x) = 1/ε. */
        self.w3 = ((1.0 / e).acosh() / n as f64).cosh();
    }

    fn chebyshev2(&mut self, n: usize, stop: f64) {
        let e = 1.0 / (10f64.powf(stop / 10.0) - 1.0).sqrt();
        let a = (1.0 / e).asinh() / n as f64;
        for k in 0..n / 2 {
            let t = core::f64::consts::PI * (2 * k + 1) as f64 / (2 * n) as f64;
            let q = Complex64::new(-a.sinh() * t.sin(), a.cosh() * t.cos());
            self.push(Pair::of_pole(1.0 / q, Some(1.0 / t.cos())));
        }
        if n % 2 == 1 {
            self.real = Some(1.0 / a.sinh());
        }
        /* |H|² = ε²T_n(1/x)² / (1 + ε²T_n(1/x)²) is 1/2 where T_n(1/x) = 1/ε. */
        self.w3 = 1.0 / ((1.0 / e).acosh() / n as f64).cosh();
    }

    fn elliptic(&mut self, n: usize, ripple: f64, stop: f64) {
        let ep = (10f64.powf(ripple / 10.0) - 1.0).sqrt();
        let es = (10f64.powf(stop / 10.0) - 1.0).sqrt();
        let k1 = ep / es;
        let k = elliptic::degree(n, k1);
        let j = Complex64::new(0.0, 1.0);
        let v0 = (elliptic::asn(j / ep, k1) / n as f64).im;
        for i in 1..=n / 2 {
            let u = (2 * i - 1) as f64 / n as f64;
            let zeta = elliptic::cd(Complex64::new(u, 0.0), k).re;
            let p = j * elliptic::cd(Complex64::new(u, -v0), k);
            self.push(Pair::of_pole(p, Some(1.0 / (k * zeta))));
        }
        if n % 2 == 1 {
            self.real = Some(-(j * elliptic::sn(Complex64::new(0.0, v0), k)).re);
        }
        self.dc = if n % 2 == 1 { 1.0 } else { 10f64.powf(-ripple / 20.0) };
        /* |H|² = 1 / (1 + εp²·R_n(x)²) is 1/2 where R_n(x) = 1/εp, R_n being
         * cd(n·u·K1, k1) at x = cd(u·K, k): past the passband edge, where
         * u = (asn(1/εp, k1) - 1) / n is imaginary and x real. */
        let u = (elliptic::asn(Complex64::new(1.0 / ep, 0.0), k1) - 1.0) / n as f64;
        self.w3 = elliptic::cd(u, k).re;
    }
}
