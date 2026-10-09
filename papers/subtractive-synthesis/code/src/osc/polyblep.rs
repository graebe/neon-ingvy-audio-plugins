// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
PolyBLEP: the trivial waveform, with a two-sample polynomial correction laid
over each discontinuity. The correction is the difference between an ideal
step and the integral of a triangular (linear B-spline) pulse, so the corner
the sampling cannot represent is replaced by one it can.

The pulse is the difference of two saws half a pulse width apart, which is how
PWM stays alias-suppressed: each of its two edges is a saw's edge, corrected.
*/

use super::{Osc, Phasor};

// ANCHOR: polyblep
/// The residual of a unit step at phase 0, for a phase `t` in [0, 1) and an
/// increment `dt` per sample. Non-zero only one sample either side of the step.
#[inline]
pub fn polyblep(t: f32, dt: f32) -> f32 {
    if t < dt {
        let x = t / dt; // just after the step
        2.0 * x - x * x - 1.0
    } else if t > 1.0 - dt {
        let x = (t - 1.0) / dt; // just before it
        x * x + 2.0 * x + 1.0
    } else {
        0.0
    }
}

pub struct PolyBlepSaw {
    phasor: Phasor,
}

impl PolyBlepSaw {
    #[inline]
    pub fn next(&mut self) -> f32 {
        let dt = self.phasor.inc;
        let t = self.phasor.tick();
        2.0 * t - 1.0 - polyblep(t, dt)
    }
}
// ANCHOR_END: polyblep

impl PolyBlepSaw {
    pub fn new(f0: f32, fs: f32) -> Self {
        Self::with_phase(f0, fs, 0.0)
    }

    /// Starting somewhere other than the wrap, as a unison voice does.
    pub fn with_phase(f0: f32, fs: f32, phase: f32) -> Self {
        Self { phasor: Phasor { phase: phase.rem_euclid(1.0), ..Phasor::new(f0, fs) } }
    }
}

impl Osc for PolyBlepSaw {
    fn next(&mut self) -> f32 {
        PolyBlepSaw::next(self)
    }
    fn set_freq(&mut self, f0: f32, fs: f32) {
        self.phasor.set_freq(f0, fs);
    }
}

// ANCHOR: pulse
/// A pulse of width `width` in (0, 1): two corrected saws, one shifted.
pub struct PolyBlepPulse {
    phasor: Phasor,
    pub width: f32,
}

impl PolyBlepPulse {
    #[inline]
    pub fn next(&mut self) -> f32 {
        let dt = self.phasor.inc;
        let t = self.phasor.tick();
        let mut u = t + self.width;
        if u >= 1.0 {
            u -= 1.0;
        }
        let saw = |p: f32| 2.0 * p - 1.0 - polyblep(p, dt);
        saw(t) - saw(u) + 2.0 * self.width - 1.0
    }
}
// ANCHOR_END: pulse

impl PolyBlepPulse {
    pub fn new(f0: f32, fs: f32, width: f32) -> Self {
        Self { phasor: Phasor::new(f0, fs), width }
    }
}

impl Osc for PolyBlepPulse {
    fn next(&mut self) -> f32 {
        PolyBlepPulse::next(self)
    }
    fn set_freq(&mut self, f0: f32, fs: f32) {
        self.phasor.set_freq(f0, fs);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_residual_is_continuous_and_vanishes_away_from_the_step() {
        let dt = 0.01;
        /* At the step itself the two halves meet the jump: -1 after, +1 before. */
        assert_eq!(polyblep(0.0, dt), -1.0);
        assert!((polyblep(1.0 - 1e-7, dt) - 1.0).abs() < 1e-3);
        /* At one sample away both halves reach zero. */
        assert!(polyblep(dt - 1e-7, dt).abs() < 1e-4);
        assert!(polyblep(1.0 - dt + 1e-7, dt).abs() < 1e-4);
        assert_eq!(polyblep(0.5, dt), 0.0);
    }

    #[test]
    fn the_saw_keeps_its_mean_and_range() {
        let mut o = PolyBlepSaw::new(440.0, 48_000.0);
        let n = 48_000;
        let xs: Vec<f32> = (0..n).map(|_| o.next()).collect();
        let mean = xs.iter().sum::<f32>() / n as f32;
        assert!(mean.abs() < 1e-3, "{mean}");
        assert!(xs.iter().all(|x| x.abs() <= 1.0 + 1e-6));
    }

    #[test]
    fn the_pulse_is_high_for_its_width() {
        for width in [0.1f32, 0.25, 0.5, 0.8] {
            let mut o = PolyBlepPulse::new(100.0, 48_000.0, width);
            let n = 48_000;
            let high = (0..n).filter(|_| o.next() > 0.0).count() as f32 / n as f32;
            assert!((high - width).abs() < 0.01, "{width}: {high}");
            /* and its mean is the duty cycle's: 2w - 1 */
            let mut o = PolyBlepPulse::new(100.0, 48_000.0, width);
            let mean = (0..n).map(|_| o.next()).sum::<f32>() / n as f32;
            assert!((mean - (2.0 * width - 1.0)).abs() < 2e-3, "{width}: {mean}");
        }
    }
}
