// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The state-variable filter, TPT form: two trapezoidal integrators in a loop
with damping `k = 1/Q`, which gives low-, band- and high-pass at once (and
notch, peak and all-pass as sums of them). The prototype is

  H_LP(s) = 1 / (s^2 + k s + 1),

so at the cutoff the low-pass has gain `1/k = Q`, exactly, at any cutoff below
Nyquist; the tests hold it to that.
*/

use super::prewarp;

/// The three outputs of one step.
#[derive(Clone, Copy, Debug, Default)]
pub struct SvfOut {
    pub lp: f32,
    pub bp: f32,
    pub hp: f32,
}

// ANCHOR: svf
#[derive(Default)]
pub struct Svf {
    g: f32,      // tan(pi fc / fs)
    k: f32,      // damping, 1 / Q
    s1: f32,     // first integrator's state
    s2: f32,     // second integrator's state
}

impl Svf {
    pub fn set(&mut self, fc: f32, q: f32, fs: f32) {
        self.g = prewarp(fc, fs);
        self.k = 1.0 / q.max(0.5);
    }

    #[inline]
    pub fn tick(&mut self, x: f32) -> SvfOut {
        let (g, k) = (self.g, self.k);
        // The loop, solved: the high-pass output that is consistent with
        // both integrators' outputs in the same sample.
        let hp = (x - (k + g) * self.s1 - self.s2) / (1.0 + g * (k + g));
        let v1 = g * hp;
        let bp = v1 + self.s1;
        self.s1 = bp + v1;
        let v2 = g * bp;
        let lp = v2 + self.s2;
        self.s2 = lp + v2;
        SvfOut { lp, bp, hp }
    }
}
// ANCHOR_END: svf

#[cfg(test)]
mod tests {
    use super::super::testing::gain;
    use super::*;

    const FS: f32 = 48_000.0;

    #[test]
    fn the_low_pass_peaks_at_q_on_the_cutoff_at_any_cutoff() {
        for fc in [100.0f32, 1_000.0, 10_000.0, 18_000.0] {
            for q in [0.707f32, 2.0, 8.0] {
                let mut f = Svf::default();
                f.set(fc, q, FS);
                let g = gain(|x| f.tick(x).lp, fc, FS);
                assert!((g - q).abs() / q < 0.02, "fc {fc} q {q}: {g}");
            }
        }
    }

    #[test]
    fn the_outputs_sum_to_the_input() {
        /* x = lp + k bp + hp, the identity the loop is built on. */
        let mut f = Svf::default();
        f.set(2_000.0, 3.0, FS);
        for n in 0..1000 {
            let x = ((n * 7919) % 101) as f32 / 50.0 - 1.0;
            let o = f.tick(x);
            assert!((o.lp + o.bp / 3.0 + o.hp - x).abs() < 1e-5);
        }
    }

    #[test]
    fn low_passes_dc_and_high_passes_nyquist() {
        let mut f = Svf::default();
        f.set(1_000.0, 0.707, FS);
        let mut out = SvfOut::default();
        for _ in 0..48_000 {
            out = f.tick(1.0);
        }
        assert!((out.lp - 1.0).abs() < 1e-4 && out.hp.abs() < 1e-4);
        assert!(gain(|x| f.tick(x).lp, 20_000.0, FS) < 0.01);
    }

    #[test]
    fn a_cutoff_that_moves_every_sample_stays_stable() {
        let mut f = Svf::default();
        let mut peak = 0.0f32;
        for n in 0..96_000 {
            let fc = 20.0 + 19_000.0 * (0.5 + 0.5 * (n as f32 * 0.01).sin());
            f.set(fc, 20.0, FS);
            let x = if n % 100 < 50 { 1.0 } else { -1.0 };
            peak = peak.max(f.tick(x).lp.abs());
        }
        assert!(peak.is_finite() && peak < 100.0, "{peak}");
    }
}
