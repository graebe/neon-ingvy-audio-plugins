// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
DPW, the differentiated parabolic wave: integrate the saw analytically (its
integral is a parabola, which has no discontinuity to alias), sample that, and
differentiate in discrete time. The differentiator's high-pass shape pushes the
folded harmonics down by roughly 12 dB per octave more than the naive saw's.

Second order. The scaling makes the first difference of the parabola a saw of
unit amplitude again.
*/

use super::{Osc, Phasor};

// ANCHOR: dpw
pub struct DpwSaw {
    phasor: Phasor,
    prev: f32,  // the parabola one sample ago
    scale: f32, // pi / (4 sin(pi f0 / fs))
}

impl DpwSaw {
    #[inline]
    pub fn next(&mut self) -> f32 {
        let s = 2.0 * self.phasor.tick() - 1.0;
        let parabola = s * s;
        let y = (parabola - self.prev) * self.scale;
        self.prev = parabola;
        y
    }

    pub fn set_freq(&mut self, f0: f32, fs: f32) {
        self.phasor.set_freq(f0, fs);
        let w = core::f32::consts::PI * f0 / fs;
        self.scale = core::f32::consts::PI / (4.0 * w.sin());
    }
}
// ANCHOR_END: dpw

impl DpwSaw {
    pub fn new(f0: f32, fs: f32) -> Self {
        let mut o = Self { phasor: Phasor::new(f0, fs), prev: 0.0, scale: 0.0 };
        o.set_freq(f0, fs);
        /* The parabola one sample before phase 0, so the first output is not
         * the full parabola jumping in from zero. */
        let s = 2.0 * (1.0 - o.phasor.inc) - 1.0;
        o.prev = s * s;
        o
    }
}

impl Osc for DpwSaw {
    fn next(&mut self) -> f32 {
        DpwSaw::next(self)
    }
    fn set_freq(&mut self, f0: f32, fs: f32) {
        DpwSaw::set_freq(self, f0, fs);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn it_is_a_unit_saw_at_low_frequency() {
        let mut o = DpwSaw::new(100.0, 48_000.0);
        let xs: Vec<f32> = (0..4800).map(|_| o.next()).collect();
        let peak = xs.iter().fold(0.0f32, |m, x| m.max(x.abs()));
        assert!((peak - 1.0).abs() < 0.01, "{peak}");
        /* No start-up transient: the first sample is already on the ramp. */
        assert!(xs[0].abs() <= 1.01, "{}", xs[0]);
        /* The ramp is rising between the wraps. */
        assert!(xs[10..200].windows(2).all(|w| w[1] > w[0]));
    }
}
