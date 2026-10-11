// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The filters around the hold: an anti-alias low-pass before it, a fourth-order
Butterworth low-pass after it, and the DC blocker. The sections run in
`biquad`'s Direct Form 1.

BEFORE THE HOLD, at Rate/2, the pre-filter takes out what would fold back
below the new Nyquist (the aliasing): an inverse Chebyshev, steep and deep
(below). AFTER IT, at Rate/2 moved by Octave, a Butterworth smooths the steps
the hold drew (the images): a tone control, gentle on purpose, with no
ripple.

DIRECT FORM 1, because both are retuned while they run (Rate glides, and is
automated): its state is the past inputs and outputs, which a new set of
coefficients takes over without the jump a transposed form's state makes.

THE CUTOFF STAYS INSIDE 20 Hz .. 0.45·fs. biquad refuses one at Nyquist or
above, and Butterworth sections closer than that warp into a shelf; a filter
asked to sit higher is as good as open there anyway.

THE DC BLOCKER is one critically damped high-pass section (Q = ½, no
overshoot) at 5 Hz: low enough that the mix with the dry signal does not
cancel the bass, and enough to take away the half step a mid-rise grid adds to
silence. Only a mid-rise grid needs it (lib.rs fades it in with Mid-Rise).

f64 THROUGHOUT: a 5 Hz section at 96 kHz has poles within 3e-4 of the unit
circle, which f32 coefficients cannot place.
*/

use biquad::{Biquad, Coefficients, DirectForm1, Type};

/// The Qs of a fourth-order Butterworth's two sections.
const BUTTERWORTH_4: [f64; 2] = [0.541_196_100_146_197, 1.306_562_964_876_376_5];
/// The cutoff's limits.
pub const MIN_CUTOFF_HZ: f64 = 20.0;
pub const MAX_CUTOFF_RATIO: f64 = 0.45;
/// The DC blocker's corner and Q.
pub const DC_HZ: f64 = 5.0;
const DC_Q: f64 = 0.5;

/// A cutoff the sections can take at `sample_rate`.
pub fn usable_cutoff(hz: f64, sample_rate: f64) -> f64 {
    let top = MAX_CUTOFF_RATIO * sample_rate;
    if hz.is_nan() {
        return top;
    }
    hz.clamp(MIN_CUTOFF_HZ.min(top), top)
}

/// A section at `hz`, which the caller keeps below Nyquist. An absurd sample
/// rate (zero, NaN) makes a frequency no section can have -- and a NaN one
/// passes biquad's own range check -- so that is refused here, and gets a
/// section that passes the signal.
#[allow(clippy::neg_cmp_op_on_partial_ord, reason = "a NaN frequency must take the guard")]
const PASS: Coefficients<f64> = Coefficients { a1: 0.0, a2: 0.0, b0: 1.0, b1: 0.0, b2: 0.0 };

fn section(kind: Type<f64>, hz: f64, sample_rate: f64, q: f64) -> Coefficients<f64> {
    let normalised = hz / (0.5 * sample_rate);
    if !(normalised > 0.0 && normalised < 1.0) {
        return PASS;
    }
    Coefficients::from_normalized_params(kind, normalised, q).unwrap_or(PASS)
}

/// The two sections' coefficients for a fourth-order Butterworth low-pass.
#[derive(Clone, Copy, Debug)]
pub struct LowpassCoefficients([Coefficients<f64>; 2]);

impl LowpassCoefficients {
    pub fn new(hz: f64, sample_rate: f64) -> Self {
        let hz = usable_cutoff(hz, sample_rate);
        LowpassCoefficients(BUTTERWORTH_4.map(|q| section(Type::LowPass, hz, sample_rate, q)))
    }
}

/// One channel's fourth-order low-pass.
#[derive(Clone, Copy, Debug)]
pub struct Lowpass([DirectForm1<f64>; 2]);

impl Lowpass {
    pub fn new(c: &LowpassCoefficients) -> Self {
        Lowpass(c.0.map(DirectForm1::new))
    }

    /// Take new coefficients, keeping the state.
    pub fn retune(&mut self, c: &LowpassCoefficients) {
        for (s, k) in self.0.iter_mut().zip(c.0) {
            s.update_coefficients(k);
        }
    }

    #[inline]
    pub fn run(&mut self, x: f64) -> f64 {
        let y = self.0[0].run(x);
        self.0[1].run(y)
    }

    pub fn reset(&mut self) {
        for s in &mut self.0 {
            s.reset_state();
        }
    }
}

/*
 * THE PRE-FILTER: a 10th-order inverse Chebyshev (Chebyshev type II) low-pass
 * whose stopband STARTS at Rate/2 and is 60 dB deep from there on, flat in its
 * passband (-3 dB at 0.77·Rate/2). A Butterworth at Rate/2 is only 3 dB down
 * where folding begins, and a tone a little above it came back 15 dB down at
 * best; this keeps everything the hold would fold at least 60 dB under, which
 * is what "takes out what would fold back" promises.
 *
 * Designed in closed form, so it follows Rate as it glides: the analog
 * prototype (stopband edge at 1 rad/s) is the reciprocal of a Chebyshev I's
 * poles, with zeros on the jω axis at 1/cos θk (Daniels, "Approximation
 * Methods for Electronic Filter Design", ch. 6), and each pole pair with its
 * zero pair is one section, each scaled to unity gain at DC, then taken to z by
 * the bilinear transform prewarped at the stopband edge. f64, Direct Form 1.
 */

/// The order and the stopband depth.
const PRE_ORDER: usize = 10;
const PRE_SECTIONS: usize = PRE_ORDER / 2;
pub const PRE_STOPBAND_DB: f64 = 60.0;

/// One analog section: zeros at ±j·zero, poles of s² + a1·s + a0.
#[derive(Clone, Copy, Debug)]
struct Prototype {
    zero2: f64,
    a1: f64,
    a0: f64,
}

static PRE_PROTOTYPE: std::sync::LazyLock<[Prototype; PRE_SECTIONS]> = std::sync::LazyLock::new(|| {
    let eps = 1.0 / (10f64.powf(PRE_STOPBAND_DB / 10.0) - 1.0).sqrt();
    let mu = (1.0 / eps).asinh() / PRE_ORDER as f64;
    core::array::from_fn(|k| {
        let theta = (2 * k + 1) as f64 * core::f64::consts::PI / (2 * PRE_ORDER) as f64;
        /* A Chebyshev I pole, and its reciprocal: the inverse Chebyshev's. */
        let (re, im) = (-mu.sinh() * theta.sin(), mu.cosh() * theta.cos());
        let mag2 = re * re + im * im;
        let zero = 1.0 / theta.cos();
        Prototype { zero2: zero * zero, a1: -2.0 * re / mag2, a0: 1.0 / mag2 }
    })
});

/// The pre-filter's sections for a stopband starting at `hz`.
#[derive(Clone, Copy, Debug)]
pub struct PreCoefficients([Coefficients<f64>; PRE_SECTIONS]);

impl PreCoefficients {
    pub fn new(hz: f64, sample_rate: f64) -> Self {
        let hz = usable_cutoff(hz, sample_rate);
        let k = 1.0 / (core::f64::consts::PI * hz / sample_rate).tan();
        if !k.is_finite() {
            return PreCoefficients([PASS; PRE_SECTIONS]);
        }
        let k2 = k * k;
        PreCoefficients(PRE_PROTOTYPE.map(|p| {
            let g = p.a0 / p.zero2;
            let a0 = k2 + p.a1 * k + p.a0;
            Coefficients {
                b0: g * (k2 + p.zero2) / a0,
                b1: g * 2.0 * (p.zero2 - k2) / a0,
                b2: g * (k2 + p.zero2) / a0,
                a1: 2.0 * (p.a0 - k2) / a0,
                a2: (k2 - p.a1 * k + p.a0) / a0,
            }
        }))
    }
}

/// One channel's pre-filter.
#[derive(Clone, Copy, Debug)]
pub struct PreFilter([DirectForm1<f64>; PRE_SECTIONS]);

impl PreFilter {
    pub fn new(c: &PreCoefficients) -> Self {
        PreFilter(c.0.map(DirectForm1::new))
    }

    pub fn retune(&mut self, c: &PreCoefficients) {
        for (s, k) in self.0.iter_mut().zip(c.0) {
            s.update_coefficients(k);
        }
    }

    #[inline]
    pub fn run(&mut self, x: f64) -> f64 {
        self.0.iter_mut().fold(x, |y, s| s.run(y))
    }

    pub fn reset(&mut self) {
        for s in &mut self.0 {
            s.reset_state();
        }
    }
}

/// One channel's DC blocker.
#[derive(Clone, Copy, Debug)]
pub struct DcBlock(DirectForm1<f64>);

impl DcBlock {
    pub fn new(sample_rate: f64) -> Self {
        let hz = DC_HZ.min(MAX_CUTOFF_RATIO * sample_rate);
        DcBlock(DirectForm1::new(section(Type::HighPass, hz, sample_rate, DC_Q)))
    }

    #[inline]
    pub fn run(&mut self, x: f64) -> f64 {
        self.0.run(x)
    }

    pub fn reset(&mut self) {
        self.0.reset_state();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The steady-state gain at `hz` of whatever `run` filters, measured.
    fn gain(mut run: impl FnMut(f64) -> f64, hz: f64, sample_rate: f64) -> f64 {
        let n = (sample_rate * 0.5) as usize;
        let w = 2.0 * core::f64::consts::PI * hz / sample_rate;
        let mut peak: f64 = 0.0;
        for i in 0..n {
            let y = run((w * i as f64).sin());
            if i > n / 2 {
                peak = peak.max(y.abs());
            }
        }
        peak
    }

    #[test]
    fn the_lowpass_is_minus_three_db_at_its_cutoff_and_steep_above() {
        let sr = 48_000.0;
        let c = LowpassCoefficients::new(2_000.0, sr);
        let at = |hz| {
            let mut f = Lowpass::new(&c);
            gain(|x| f.run(x), hz, sr)
        };
        assert!((at(200.0) - 1.0).abs() < 0.01);
        assert!((at(2_000.0) - core::f64::consts::FRAC_1_SQRT_2).abs() < 0.01);
        /* Fourth order: 24 dB an octave, so an octave up is under -20 dB. */
        assert!(at(4_000.0) < 0.1);
    }

    #[test]
    fn the_pre_filter_is_sixty_db_down_from_its_edge_on_and_flat_below() {
        let sr = 48_000.0;
        let edge = 4_000.0;
        let c = PreCoefficients::new(edge, sr);
        let db = |hz| {
            let mut f = PreFilter::new(&c);
            20.0 * gain(|x| f.run(x), hz, sr).log10()
        };
        assert!(db(100.0).abs() < 0.05, "DC: {}", db(100.0));
        assert!(db(2_000.0).abs() < 0.5, "the band: {}", db(2_000.0));
        /* -3 dB a little above three quarters of the edge. */
        let knee = db(0.77 * edge);
        assert!((-4.5..=-1.5).contains(&knee), "the knee: {knee}");
        for hz in [edge, 4_500.0, 6_000.0, 10_000.0, 20_000.0] {
            assert!(db(hz) < -PRE_STOPBAND_DB + 1.0, "{hz} Hz: {} dB", db(hz));
        }
        /* An absurd rate passes the signal rather than fail. */
        let mut open = PreFilter::new(&PreCoefficients::new(1_000.0, 0.0));
        assert_eq!(open.run(0.25), 0.25);
        let mut f = PreFilter::new(&c);
        for _ in 0..100 {
            f.run(1.0);
        }
        f.retune(&PreCoefficients::new(4_100.0, sr));
        f.reset();
        assert_eq!(f.run(0.0), 0.0);
    }

    #[test]
    fn the_cutoff_stays_where_the_sections_can_take_it() {
        assert_eq!(usable_cutoff(1e9, 48_000.0), 0.45 * 48_000.0);
        assert_eq!(usable_cutoff(1.0, 48_000.0), MIN_CUTOFF_HZ);
        assert_eq!(usable_cutoff(f64::NAN, 48_000.0), 0.45 * 48_000.0);
        /* At an absurd rate the section passes the signal rather than fail. */
        let mut f = Lowpass::new(&LowpassCoefficients::new(1_000.0, 0.0));
        assert_eq!(f.run(0.25), 0.25);
    }

    #[test]
    fn retuning_keeps_the_state() {
        let sr = 44_100.0;
        let mut f = Lowpass::new(&LowpassCoefficients::new(1_000.0, sr));
        for _ in 0..2000 {
            f.run(1.0);
        }
        f.retune(&LowpassCoefficients::new(1_100.0, sr));
        /* A settled DC level carries straight across: no jump. */
        assert!((f.run(1.0) - 1.0).abs() < 1e-3);
        f.reset();
        assert!(f.run(0.0).abs() < 1e-12);
    }

    #[test]
    fn the_dc_blocker_removes_dc_and_passes_the_bass() {
        let sr = 48_000.0;
        let mut d = DcBlock::new(sr);
        let mut y = 0.0;
        for _ in 0..(sr as usize * 2) {
            y = d.run(0.5);
        }
        assert!(y.abs() < 1e-4, "{y}");
        let mut d = DcBlock::new(sr);
        assert!(gain(|x| d.run(x), 50.0, sr) > 0.98);
        d.reset();
        assert_eq!(d.run(0.0), 0.0);
    }
}
