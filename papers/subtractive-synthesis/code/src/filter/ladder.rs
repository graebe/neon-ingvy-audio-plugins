// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The four-pole ladder in TPT form: four trapezoidal one-poles in series, the
output fed back negatively with gain `k`. The prototype is

  H(s) = 1 / ((1 + s)^4 + k),

which at the cutoff (s = j) is 1 / (k - 4): a quarter at k = 0, and a pole on
the axis -- self-oscillation -- at k = 4.

The loop is delay-free: the stage outputs depend on the input to the first
stage, which depends on the last stage's output. Each one-pole is
`y = G x + S_i` within a sample, so the cascade is `y4 = G^4 u + S`, and
`u = x - k y4` solves to `u = (x - k S) / (1 + k G^4)`.

The saturation is one tanh at the loop's input, applied to the solved `u`.
That keeps the solution closed-form; the fully implicit nonlinear loop needs
an iterative solver per sample, which the paper discusses.
*/

use super::prewarp;

// ANCHOR: ladder
#[derive(Default)]
pub struct Ladder {
    g: f32,      // one-pole gain G = g / (1 + g), g = tan(pi fc / fs)
    k: f32,      // feedback: 4 is the oscillation threshold
    s: [f32; 4], // the four integrators' states
    pub drive: f32,
}

impl Ladder {
    pub fn set(&mut self, fc: f32, resonance: f32, fs: f32) {
        let g = prewarp(fc, fs);
        self.g = g / (1.0 + g);
        self.k = 4.0 * resonance.clamp(0.0, 1.2); // past 1, only the tanh bounds it
    }

    #[inline]
    pub fn tick(&mut self, x: f32) -> f32 {
        let gg = self.g;
        // Each stage is y = gg x + (1 - gg) s; fold the states through.
        let sigma = self.s.map(|s| (1.0 - gg) * s);
        let big_s = sigma.iter().fold(0.0, |acc, &sig| acc * gg + sig);
        let g4 = gg * gg * gg * gg;
        let u = (x - self.k * big_s) / (1.0 + self.k * g4);
        let mut v = if self.drive > 0.0 { (self.drive * u).tanh() / self.drive } else { u };
        for s in self.s.iter_mut() {
            let w = (v - *s) * gg; // TPT one-pole
            let y = w + *s;
            *s = y + w;
            v = y;
        }
        v
    }
}
// ANCHOR_END: ladder

impl Ladder {
    /// A ladder with its saturation; a `drive` of 0 is the linear filter.
    pub fn with_drive(drive: f32) -> Self {
        Self { drive, ..Default::default() }
    }
}

#[cfg(test)]
mod tests {
    use super::super::testing::gain;
    use super::*;

    const FS: f32 = 48_000.0;

    #[test]
    fn at_the_cutoff_the_gain_is_one_over_four_minus_k() {
        for fc in [200.0f32, 2_000.0, 15_000.0] {
            for r in [0.0f32, 0.5, 0.9] {
                let mut f = Ladder::default();
                f.set(fc, r, FS);
                let want = 1.0 / (4.0 - 4.0 * r);
                let g = gain(|x| f.tick(x), fc, FS);
                assert!((g - want).abs() / want < 0.02, "fc {fc} r {r}: {g} vs {want}");
            }
        }
    }

    #[test]
    fn dc_loses_one_over_one_plus_k() {
        /* The passband gain the ladder is known for losing with resonance. */
        let mut f = Ladder::default();
        f.set(1_000.0, 0.75, FS);
        let mut y = 0.0;
        for _ in 0..48_000 {
            y = f.tick(1.0);
        }
        assert!((y - 1.0 / 4.0).abs() < 1e-4, "{y}");
    }

    #[test]
    fn the_threshold_rings_on_and_past_it_the_tanh_holds_the_level() {
        /* Linear, k = 4: a pole on the unit circle. A kick rings undamped. */
        let mut f = Ladder::default();
        f.set(1_000.0, 1.0, FS);
        f.tick(1.0);
        let peak = |f: &mut Ladder, n: usize| (0..n).map(|_| f.tick(0.0).abs()).fold(0.0f32, f32::max);
        let early = peak(&mut f, 4_800);
        let _ = peak(&mut f, 48_000);
        let late = peak(&mut f, 4_800);
        assert!(early > 1e-3 && (late / early - 1.0).abs() < 0.01, "{early} {late}");

        /* Past it, with drive: the oscillation grows until the tanh holds it,
         * at a level that does not depend on how small the kick was. */
        let level = |kick: f32| {
            let mut f = Ladder::with_drive(1.0);
            f.set(1_000.0, 1.1, FS);
            f.tick(kick);
            let _ = peak(&mut f, 96_000);
            peak(&mut f, 4_800)
        };
        let (a, b) = (level(1e-3), level(1.0));
        assert!(a > 0.1 && a < 4.0 && (a / b - 1.0).abs() < 0.01, "{a} {b}");

        /* Below it, the same kick dies away. */
        let mut f = Ladder::default();
        f.set(1_000.0, 0.9, FS);
        f.tick(1.0);
        assert!(peak(&mut f, 48_000) > 0.0 && peak(&mut f, 100) < 1e-6);
    }
}
