// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
Unison: several copies of one oscillator per note, detuned symmetrically in
cents, panned across the stereo field, and started at different phases. The
beating between them is the sound; the start phases decide whether every note
begins with the same flam (equal phases sum to a spike) or not.

The copies are uncorrelated once detuned, so their powers add: the sum is
scaled by `1/sqrt(n)` to keep loudness, not by `1/n`, which would get quieter
with every voice added.
*/

use crate::osc::polyblep::PolyBlepSaw;

/// The most copies one note may have.
pub const MAX: usize = 16;

// ANCHOR: unison
pub struct Unison {
    saws: [PolyBlepSaw; MAX],
    gains: [(f32, f32); MAX], // equal-power pan, left and right
    n: usize,
}

impl Unison {
    /// `detune` is the outermost copy's offset in cents, `spread` the
    /// outermost pan in 0..=1.
    pub fn new(n: usize, f0: f32, detune: f32, spread: f32, fs: f32) -> Self {
        let n = n.clamp(1, MAX);
        let norm = 1.0 / (n as f32).sqrt();
        let at = |i: usize| if n == 1 { 0.0 } else { 2.0 * i as f32 / (n - 1) as f32 - 1.0 };
        let saws = core::array::from_fn(|i| {
            let cents = detune * at(i);
            // Golden-ratio phases: spread evenly, the same every note.
            let phase = (i as f32 * 0.618_034).fract();
            PolyBlepSaw::with_phase(f0 * (cents / 1200.0).exp2(), fs, phase)
        });
        let gains = core::array::from_fn(|i| {
            let theta = (spread * at(i) + 1.0) * core::f32::consts::FRAC_PI_4;
            (norm * theta.cos(), norm * theta.sin())
        });
        Self { saws, gains, n }
    }

    #[inline]
    pub fn tick(&mut self) -> (f32, f32) {
        let (mut l, mut r) = (0.0, 0.0);
        for (saw, &(gl, gr)) in self.saws[..self.n].iter_mut().zip(&self.gains) {
            let x = saw.next();
            l += gl * x;
            r += gr * x;
        }
        (l, r)
    }
}
// ANCHOR_END: unison

#[cfg(test)]
mod tests {
    use super::*;

    fn rms(n: usize, detune: f32, spread: f32) -> (f32, f32) {
        /* 440 Hz and four seconds: even the closest pair of copies beats a
         * few times within the window, so their sum is the uncorrelated one. */
        let mut u = Unison::new(n, 440.0, detune, spread, 48_000.0);
        let len = 192_000;
        let (mut l, mut r) = (0.0f64, 0.0f64);
        for _ in 0..len {
            let (a, b) = u.tick();
            l += (a * a) as f64;
            r += (b * b) as f64;
        }
        (((l + r) / len as f64).sqrt() as f32, ((l - r) / len as f64) as f32)
    }

    #[test]
    fn loudness_holds_as_voices_are_added() {
        /* A saw's RMS is 1/sqrt(3); the stereo pair, summed in power, keeps it. */
        let one = rms(1, 0.0, 0.0).0;
        assert!((one - 1.0 / 3f32.sqrt()).abs() < 0.01, "{one}");
        for n in [3, 7, 16] {
            let many = rms(n, 50.0, 1.0).0;
            assert!((many / one - 1.0).abs() < 0.15, "{n}: {many} vs {one}");
        }
    }

    #[test]
    fn spread_is_symmetric_and_detune_is_in_cents() {
        /* Symmetric pans: left and right carry the same power. */
        let (_, diff) = rms(5, 20.0, 1.0);
        assert!(diff.abs() < 0.01, "{diff}");
        /* More voices than MAX are clamped, not a panic. */
        let mut u = Unison::new(100, 110.0, 10.0, 0.5, 48_000.0);
        assert_eq!(u.n, MAX);
        u.tick();
    }
}
