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
 * THE FILTER: the resonant low-pass after the crusher, the owner's Filter
 * panel -- Cutoff, Peak, and a Slope of 12, 24 or 48 dB per octave.
 *
 * Each slope is a Butterworth of that order -- two, four or eight poles, one,
 * two or four sections -- so at Peak 0 dB it is flat up to the cutoff and -3
 * dB there. PEAK RAISES THE RESPONSE AT THE CUTOFF BY EXACTLY THAT MANY dB:
 * a two-pole section's gain at its own corner is its Q, so multiplying the
 * Q of the chain's sharpest section by 10^(Peak/20) multiplies the whole
 * response at the cutoff by just that, and leaves the passband and the slope
 * as they were. Up to +24 dB: a resonant bump, never a filter that rings on
 * its own (the sections stay stable at any Q).
 *
 * ALL THREE SLOPES RUN, every sample, so moving Slope is a crossfade between
 * them (lib.rs), never a click -- seven sections a channel, a few hundred
 * nanoseconds a sample.
 *
 * MATCHED SECTIONS, NOT BILINEAR ONES. The bilinear transform squeezes the
 * whole axis into 0..Nyquist and puts a zero at Nyquist, so near the top a
 * low-pass falls ever faster to nothing: at 48 kHz a 24 dB filter at 14 kHz
 * fell 60 dB in half an octave, and its peak narrowed -- a slope it did not
 * say, and heard as such. Each section here is matched to its analog one
 * instead (Vicanek, "Matched Second Order Digital Filters", 2016): poles by
 * the impulse invariance, exact; zeros chosen so the magnitude is the analog
 * one at DC, at the corner and at Nyquist. The cascades then follow the
 * analog Butterworth within about 2 dB from 20 Hz to 20 kHz at 44.1 kHz and
 * up, the corner's gain still exactly Q -- so Peak still rises by exactly
 * Peak -- for the same cost a sample and no latency. They also take a cutoff
 * up to 0.49·fs, so 20 kHz is 20 kHz at 44.1 kHz too.
 */

/// How near Nyquist the Filter's cutoff may sit: matched sections stay
/// stable and true there.
pub const TONE_MAX_CUTOFF_RATIO: f64 = 0.49;

/// A low-pass section matched to the analog s^2 + s/Q + 1 at `hz` (Vicanek
/// 2016, the low-pass): poles e^(s·T), and b0, b1 (b2 = 0) giving the analog
/// magnitude at DC and at `hz` -- where it is Q -- and close to it at
/// Nyquist. `hz` is inside the Filter's range (the caller's clamp); an absurd
/// rate passes the signal, as [`section`] does.
#[allow(clippy::neg_cmp_op_on_partial_ord, reason = "a NaN rate must take the guard")]
fn matched_low_pass(hz: f64, sample_rate: f64, q: f64) -> Coefficients<f64> {
    if !(sample_rate > 0.0 && hz > 0.0 && q > 0.0) {
        return PASS;
    }
    let w0 = core::f64::consts::TAU * hz / sample_rate;
    let zeta = 0.5 / q;
    let decay = (-zeta * w0).exp();
    let ring = if zeta <= 1.0 { ((1.0 - zeta * zeta).sqrt() * w0).cos() } else { ((zeta * zeta - 1.0).sqrt() * w0).cosh() };
    let (a1, a2) = (-2.0 * decay * ring, decay * decay);
    let phi1 = (0.5 * w0).sin().powi(2);
    let phi0 = 1.0 - phi1;
    let phi2 = 4.0 * phi0 * phi1;
    let (big_a0, big_a1, big_a2) = ((1.0 + a1 + a2).powi(2), (1.0 - a1 + a2).powi(2), -4.0 * a2);
    let r1 = (big_a0 * phi0 + big_a1 * phi1 + big_a2 * phi2) * q * q;
    let big_b1 = ((r1 - big_a0 * phi0) / phi1).max(0.0);
    let b0 = 0.5 * (big_a0.sqrt() + big_b1.sqrt());
    Coefficients { a1, a2, b0, b1: big_a0.sqrt() - b0, b2: 0.0 }
}

/// The Qs of Butterworth sections: one for 12 dB, two for 24, four for 48,
/// the sharpest last.
const BUTTERWORTH_2: [f64; 1] = [core::f64::consts::FRAC_1_SQRT_2];
const BUTTERWORTH_8: [f64; 4] = [0.509_795_579_104_159, 0.601_344_886_935_045, 0.899_976_223_136_415, 2.562_915_447_741_505];

/// The three chains' coefficients at one cutoff and peak.
#[derive(Clone, Copy, Debug)]
pub struct ToneCoefficients {
    c12: [Coefficients<f64>; 1],
    c24: [Coefficients<f64>; 2],
    c48: [Coefficients<f64>; 4],
}

impl ToneCoefficients {
    pub fn new(hz: f64, peak_db: f64, sample_rate: f64) -> Self {
        let top = TONE_MAX_CUTOFF_RATIO * sample_rate;
        let hz = if hz.is_nan() { top } else { hz.clamp(MIN_CUTOFF_HZ.min(top), top) };
        let k = if peak_db.is_nan() { 1.0 } else { 10f64.powf(peak_db.max(0.0) / 20.0) };
        fn chain<const N: usize>(qs: [f64; N], hz: f64, k: f64, sample_rate: f64) -> [Coefficients<f64>; N] {
            core::array::from_fn(|i| matched_low_pass(hz, sample_rate, if i == N - 1 { qs[i] * k } else { qs[i] }))
        }
        ToneCoefficients {
            c12: chain(BUTTERWORTH_2, hz, k, sample_rate),
            c24: chain(BUTTERWORTH_4, hz, k, sample_rate),
            c48: chain(BUTTERWORTH_8, hz, k, sample_rate),
        }
    }

    /// The response at `hz` of the chain for slope `index` (0, 1, 2 for 12,
    /// 24, 48 dB), in dB: what an editor draws the filter with.
    pub fn response_db(&self, index: usize, hz: f64, sample_rate: f64) -> f64 {
        let w = core::f64::consts::TAU * hz / sample_rate;
        let gain = |c: &Coefficients<f64>| {
            /* |H(e^jw)| of b0 + b1 z^-1 + b2 z^-2 over 1 + a1 z^-1 + a2 z^-2. */
            let (c1, s1, c2, s2) = (w.cos(), w.sin(), (2.0 * w).cos(), (2.0 * w).sin());
            let (nr, ni) = (c.b0 + c.b1 * c1 + c.b2 * c2, -(c.b1 * s1 + c.b2 * s2));
            let (dr, di) = (1.0 + c.a1 * c1 + c.a2 * c2, -(c.a1 * s1 + c.a2 * s2));
            ((nr * nr + ni * ni) / (dr * dr + di * di)).sqrt()
        };
        let sections: &[Coefficients<f64>] = match index {
            0 => &self.c12,
            1 => &self.c24,
            _ => &self.c48,
        };
        20.0 * sections.iter().map(gain).product::<f64>().max(1e-12).log10()
    }
}

/// One channel's three chains.
#[derive(Clone, Copy, Debug)]
pub struct Tone {
    s12: [DirectForm1<f64>; 1],
    s24: [DirectForm1<f64>; 2],
    s48: [DirectForm1<f64>; 4],
}

impl Tone {
    pub fn new(c: &ToneCoefficients) -> Self {
        Tone { s12: c.c12.map(DirectForm1::new), s24: c.c24.map(DirectForm1::new), s48: c.c48.map(DirectForm1::new) }
    }

    /// Take new coefficients, keeping the state.
    pub fn retune(&mut self, c: &ToneCoefficients) {
        for (s, k) in self.s12.iter_mut().zip(c.c12) {
            s.update_coefficients(k);
        }
        for (s, k) in self.s24.iter_mut().zip(c.c24) {
            s.update_coefficients(k);
        }
        for (s, k) in self.s48.iter_mut().zip(c.c48) {
            s.update_coefficients(k);
        }
    }

    /// One sample through all three: (12, 24, 48 dB).
    #[inline]
    pub fn run(&mut self, x: f64) -> (f64, f64, f64) {
        let y12 = self.s12[0].run(x);
        let y24 = self.s24.iter_mut().fold(x, |y, s| s.run(y));
        let y48 = self.s48.iter_mut().fold(x, |y, s| s.run(y));
        (y12, y24, y48)
    }

    pub fn reset(&mut self) {
        self.s12.iter_mut().chain(self.s24.iter_mut()).chain(self.s48.iter_mut()).for_each(|s| s.reset_state());
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
