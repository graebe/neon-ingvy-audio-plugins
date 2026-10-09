// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The filters around the hold: a fourth-order Butterworth low-pass, twice, and
the DC blocker. The sections are `biquad`'s; this file only says which.

PRE AND POST ARE THE SAME FILTER at different places and frequencies. Before
the hold, at Rate/2, it takes out what would fold back below the new Nyquist
(the aliasing); after it, at Rate/2 moved by Octave, it smooths the steps the
hold drew (the images). Fourth order -- two sections at the Butterworth Qs --
so the edge is steep enough to hear as "band-limited" and still has no ripple.

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
fn section(kind: Type<f64>, hz: f64, sample_rate: f64, q: f64) -> Coefficients<f64> {
    const PASS: Coefficients<f64> = Coefficients { a1: 0.0, a2: 0.0, b0: 1.0, b1: 0.0, b2: 0.0 };
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
