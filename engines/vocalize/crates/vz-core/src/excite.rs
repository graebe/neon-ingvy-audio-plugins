// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
Excite: harmonics for a source too pure to shape.

A formant can only lift what is there. A sine, a sub bass or a flute has one
partial or few, and no vowel can be put on a spectrum with nothing at F2. So
before anything else the input may pass a soft clip, which adds odd
harmonics -- the more Excite, the harder the clip and the more of them.

THE SAME AT EVERY LEVEL. The clip works on the input divided by its own peak
envelope (instant attack, 50 ms release) and the result is multiplied back:
a quiet note is excited as much as a loud one, and the level stays the
input's. The curve is tanh, antialiased (ni_dsp::shaper), driven from 1 to 8
and normalised so a peak stays a peak.

At 0 % the input passes exactly: `x + 0 * (...)` is `x`.
*/

use ni_dsp::shaper::AdaaTanh;

/// The envelope's release, in ms.
const RELEASE_MS: f64 = 50.0;

/// The drive at 100 %.
const MAX_DRIVE: f64 = 8.0;

#[derive(Clone, Debug)]
pub struct Excite {
    env: f64,
    release: f64,
    shaper: AdaaTanh,
}

impl Excite {
    pub fn new(fs: f64) -> Excite {
        Excite { env: 0.0, release: release(fs), shaper: AdaaTanh::new() }
    }

    pub fn set_rate(&mut self, fs: f64) {
        self.release = release(fs);
    }

    pub fn reset(&mut self) {
        self.env = 0.0;
        self.shaper.reset();
    }

    /// One sample at `amount` (0..1).
    #[inline]
    pub fn run(&mut self, x: f64, amount: f64) -> f64 {
        let a = x.abs();
        self.env = if a >= self.env { a } else { self.env * self.release };
        let drive = 1.0 + (MAX_DRIVE - 1.0) * amount;
        let u = if self.env > 1e-12 { x / self.env } else { 0.0 };
        let shaped = self.shaper.run(drive * u) / drive.tanh() * self.env;
        x + amount * (shaped - x)
    }
}

fn release(fs: f64) -> f64 {
    (-1000.0 / (RELEASE_MS * fs)).exp()
}

#[cfg(test)]
mod tests {
    use super::*;
    use realfft::RealFftPlanner;

    /// The level of each of the first `count` harmonics of a sine at bin
    /// `bin` of an n-point spectrum, in dB of the fundamental.
    fn harmonics(x: &[f64], bin: usize, count: usize) -> Vec<f64> {
        let n = x.len();
        let plan = RealFftPlanner::<f64>::new().plan_fft_forward(n);
        let mut input = x.to_vec();
        let mut out = plan.make_output_vec();
        plan.process(&mut input, &mut out).unwrap();
        let f = out[bin].norm();
        (1..=count).map(|h| 20.0 * (out[bin * h].norm() / f).log10()).collect()
    }

    fn sine(level: f64, bin: usize, n: usize) -> Vec<f64> {
        (0..n).map(|i| level * (core::f64::consts::TAU * (bin * i) as f64 / n as f64).sin()).collect()
    }

    #[test]
    fn nothing_passes_exactly() {
        let mut e = Excite::new(48_000.0);
        for x in sine(0.7, 10, 4096) {
            assert_eq!(e.run(x, 0.0), x);
        }
    }

    #[test]
    fn a_sine_gains_odd_harmonics_whatever_its_level() {
        let n = 8192;
        let bin = 32;
        let run = |level: f64| {
            let mut e = Excite::new(48_000.0);
            let x = sine(level, bin, n);
            /* Twice through, so the envelope has settled: the second pass is
             * measured. */
            for &s in &x {
                e.run(s, 1.0);
            }
            let y: Vec<f64> = x.iter().map(|&s| e.run(s, 1.0)).collect();
            harmonics(&y, bin, 5)
        };
        let loud = run(0.9);
        let quiet = run(0.01);
        assert!(loud[2] > -20.0, "the third: {}", loud[2]);
        assert!(loud[4] > -30.0, "the fifth: {}", loud[4]);
        assert!(loud[1] < -60.0 && loud[3] < -60.0, "even ones: {loud:?}");
        for h in [2, 4] {
            assert!((loud[h] - quiet[h]).abs() < 0.5, "{loud:?} vs {quiet:?}");
        }
    }

    #[test]
    fn the_level_stays_the_inputs_and_reset_forgets() {
        let mut e = Excite::new(48_000.0);
        let x = sine(0.5, 16, 4096);
        let peak = x.iter().map(|&s| e.run(s, 1.0).abs()).fold(0.0, f64::max);
        assert!((peak - 0.5).abs() < 0.02, "{peak}");
        e.reset();
        assert_eq!(e.run(0.0, 1.0), 0.0);
        e.set_rate(96_000.0);
        assert!(e.release > release(48_000.0));
    }
}
