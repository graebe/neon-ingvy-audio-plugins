// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The Spectral engine: the envelope replaced bin by bin.

FRAMES. Every quarter of a frame, the last `n` samples (about 21 ms: 1024 at
44.1 and 48 kHz, 2048 at 88.2 and 96, 4096 above) are windowed (square-root
Hann), transformed, changed and transformed back, and overlap-added with the
same window. The two windows make a Hann, and four Hanns a quarter apart sum
to 2, so with no change the input comes back exactly, `n` samples late --
which is the latency this engine reports.

THE ENVELOPE OF A FRAME is its real cepstrum, liftered: the log magnitude
transformed again, everything slower than 1.5 ms kept (a pitch period is
longer, so the harmonics go and the resonances stay), and transformed back.
Like linear prediction in the Time engine, it is floored 40 dB under its peak,
so no band is lifted more than that.

THE MORPH is the one the plugin is named for, here exact:

```text
  |Y| = |X| · exp(v · (ln E_vowel - ln E_input))
```

-- the input's envelope traded for the vowel's in proportion v, its fine
structure (the harmonics, the noise) kept, and its phase untouched. E_vowel is
model.rs's cascade evaluated at the bins: the filter the Time engine runs. Each
frame is then scaled to the energy it came in with, so the trade changes the
colour and not the level.

PLANNED IN `new`, NEVER ON THE AUDIO THREAD. All three sizes are planned and
every buffer is sized for the largest, so a new sample rate, a mode switch or
a panic allocates nothing.
*/

use std::sync::Arc;

use crate::model::{Pole, FORMANTS};
use crate::Ring;
use realfft::num_complex::Complex64;
use realfft::{ComplexToReal, RealFftPlanner, RealToComplex};

/// The largest frame.
pub const MAX_SIZE: usize = 4096;
const MAX_BINS: usize = MAX_SIZE / 2 + 1;

/// The frame sizes, smallest first.
const SIZES: [usize; 3] = [1024, 2048, 4096];

/// The lifter's cut, in seconds: envelope below, harmonics above.
const LIFTER_S: f64 = 0.0015;

/// How far under its peak an envelope is floored, as a natural log (40 dB).
const FLOOR_LN: f64 = 4.605_170_185_988_091;

/// The frame size at a sample rate: about 21 ms, as a power of two.
pub fn size_at(fs: f64) -> usize {
    if fs < 66_150.0 {
        SIZES[0]
    } else if fs < 132_300.0 {
        SIZES[1]
    } else {
        SIZES[2]
    }
}

struct Plan {
    forward: Arc<dyn RealToComplex<f64>>,
    inverse: Arc<dyn ComplexToReal<f64>>,
}

/// The engine's tables and scratch, shared by every channel.
pub struct Spectral {
    plans: [Plan; 3],
    fs: f64,
    n: usize,
    hop: usize,
    lifter: usize,
    window: Box<[f64; MAX_SIZE]>,
    /// cos and sin of w and 2w at every bin, for the vowel's curve.
    trig: Box<[[f64; 4]; MAX_BINS]>,
    /// ln |H| of the vowel at every bin.
    vowel: Box<[f64; MAX_BINS]>,
    frame: Box<[f64; MAX_SIZE]>,
    cepstrum: Box<[f64; MAX_SIZE]>,
    spectrum: Box<[Complex64; MAX_BINS]>,
    work: Box<[Complex64; MAX_BINS]>,
    gains: Box<[f64; MAX_BINS]>,
    scratch: Box<[Complex64]>,
}

/// One channel's frames in flight.
pub struct Channel {
    /// The overlap-add: sums for the next `n` samples of output.
    ola: Box<[f64; MAX_SIZE]>,
    /// The finished samples, one hop of them, and the next to go out.
    out: Box<[f64; MAX_SIZE / 4]>,
    pos: usize,
    /// The last frame's envelope, ln, floored: for the editor.
    envelope: Box<[f64; MAX_BINS]>,
}

impl Channel {
    pub fn new() -> Channel {
        Channel {
            ola: Box::new([0.0; MAX_SIZE]),
            out: Box::new([0.0; MAX_SIZE / 4]),
            pos: 0,
            envelope: Box::new([0.0; MAX_BINS]),
        }
    }

    pub fn reset(&mut self) {
        self.ola.fill(0.0);
        self.out.fill(0.0);
        self.pos = 0;
        self.envelope.fill(0.0);
    }

    /// The last envelope at bin position `k` (fractional), interpolated.
    pub fn envelope_at(&self, k: f64, bins: usize) -> f64 {
        let k = k.clamp(0.0, (bins - 1) as f64);
        let i = (k as usize).min(bins - 2);
        let t = k - i as f64;
        self.envelope[i] + (self.envelope[i + 1] - self.envelope[i]) * t
    }
}

impl Default for Channel {
    fn default() -> Self {
        Channel::new()
    }
}

impl Spectral {
    /// Plans every size. Allocates: not the audio thread.
    pub fn new(fs: f64) -> Spectral {
        let mut planner = RealFftPlanner::<f64>::new();
        let plans = SIZES.map(|n| Plan { forward: planner.plan_fft_forward(n), inverse: planner.plan_fft_inverse(n) });
        let scratch = plans
            .iter()
            .map(|p| p.forward.get_scratch_len().max(p.inverse.get_scratch_len()))
            .max()
            .unwrap_or(0);
        let mut s = Spectral {
            plans,
            fs,
            n: 0,
            hop: 0,
            lifter: 0,
            window: Box::new([0.0; MAX_SIZE]),
            trig: Box::new([[0.0; 4]; MAX_BINS]),
            vowel: Box::new([0.0; MAX_BINS]),
            frame: Box::new([0.0; MAX_SIZE]),
            cepstrum: Box::new([0.0; MAX_SIZE]),
            spectrum: Box::new([Complex64::new(0.0, 0.0); MAX_BINS]),
            work: Box::new([Complex64::new(0.0, 0.0); MAX_BINS]),
            gains: Box::new([0.0; MAX_BINS]),
            scratch: vec![Complex64::new(0.0, 0.0); scratch].into_boxed_slice(),
        };
        s.set_rate(fs);
        s
    }

    /// The tables for another rate, in place: allocates nothing.
    pub fn set_rate(&mut self, fs: f64) {
        self.fs = fs;
        self.n = size_at(fs);
        self.hop = self.n / 4;
        self.lifter = ((LIFTER_S * fs).round() as usize).clamp(8, self.n / 4);
        let n = self.n;
        for (i, w) in self.window[..n].iter_mut().enumerate() {
            /* Periodic: four of them a quarter apart sum to a constant. */
            *w = (0.5 - 0.5 * (core::f64::consts::TAU * i as f64 / n as f64).cos()).sqrt();
        }
        for (k, t) in self.trig[..=n / 2].iter_mut().enumerate() {
            let w = core::f64::consts::TAU * k as f64 / n as f64;
            let (s1, c1) = w.sin_cos();
            let (s2, c2) = (2.0 * w).sin_cos();
            *t = [c1, s1, c2, s2];
        }
    }

    /// The frame size: also the latency, in samples.
    pub fn size(&self) -> usize {
        self.n
    }

    pub fn bins(&self) -> usize {
        self.n / 2 + 1
    }

    /// The vowel's curve at the bins, from its five resonators.
    pub fn set_vowel(&mut self, poles: &[Pole; FORMANTS]) {
        let bins = self.bins();
        for (v, t) in self.vowel[..bins].iter_mut().zip(self.trig.iter()) {
            *v = poles.iter().map(|p| p.ln_gain(t[0], t[1], t[2], t[3])).sum();
        }
    }

    /// One sample out of `ch`, after `src` took this sample's input; a frame
    /// is run at the end of every hop.
    #[inline]
    pub fn next(&mut self, ch: &mut Channel, src: &Ring, v: f64) -> f64 {
        let y = ch.out[ch.pos];
        ch.pos += 1;
        if ch.pos >= self.hop {
            self.frame(ch, src, 0, v);
        }
        y
    }

    /// Bring `ch` to where it would be had it been running all along: the
    /// frames that end in the last frame's length, run now from the history
    /// in `src`, so the very next sample out is a whole one.
    pub fn prime(&mut self, ch: &mut Channel, src: &Ring, v: f64) {
        ch.ola.fill(0.0);
        for k in (1..=self.n / self.hop).rev() {
            self.frame(ch, src, k * self.hop, v);
        }
        ch.pos = self.hop - 1;
    }

    /// One frame: the `n` samples ending `end` samples ago.
    fn frame(&mut self, ch: &mut Channel, src: &Ring, end: usize, v: f64) {
        let (n, hop) = (self.n, self.hop);
        let bins = n / 2 + 1;
        let plan = &self.plans[SIZES.iter().position(|&s| s == n).unwrap_or(0)];
        let scratch_f = &mut self.scratch[..plan.forward.get_scratch_len()];
        for (i, x) in self.frame[..n].iter_mut().enumerate() {
            *x = src.at(end + n - 1 - i) * self.window[i];
        }
        /* Each call fails only on a buffer of the wrong length, and every one
         * of them is sized from the plan's own n. */
        let ran = plan.forward.process_with_scratch(&mut self.frame[..n], &mut self.spectrum[..bins], scratch_f);
        debug_assert!(ran.is_ok());

        /* The envelope: the real cepstrum, liftered, and back. */
        for (w, x) in self.work[..bins].iter_mut().zip(self.spectrum.iter()) {
            *w = Complex64::new(0.5 * (x.norm_sqr() + 1e-30).ln(), 0.0);
        }
        let scratch_i = &mut self.scratch[..plan.inverse.get_scratch_len()];
        let ran = plan.inverse.process_with_scratch(&mut self.work[..bins], &mut self.cepstrum[..n], scratch_i);
        debug_assert!(ran.is_ok());
        let cut = self.lifter as f64 + 1.0;
        for (i, c) in self.cepstrum[..n].iter_mut().enumerate() {
            let q = i.min(n - i);
            let taper = if q <= self.lifter { 0.5 + 0.5 * (core::f64::consts::PI * q as f64 / cut).cos() } else { 0.0 };
            *c *= taper / n as f64;
        }
        let scratch_f = &mut self.scratch[..plan.forward.get_scratch_len()];
        let ran = plan.forward.process_with_scratch(&mut self.cepstrum[..n], &mut self.work[..bins], scratch_f);
        debug_assert!(ran.is_ok());
        let peak = self.work[..bins].iter().map(|w| w.re).fold(f64::MIN, f64::max);
        for (e, w) in ch.envelope[..bins].iter_mut().zip(self.work.iter()) {
            *e = w.re.max(peak - FLOOR_LN);
        }

        /* The trade, then the frame's energy as it came in. */
        let (mut before, mut after) = (0.0, 0.0);
        for k in 0..bins {
            let g = (v * (self.vowel[k] - ch.envelope[k])).exp();
            self.gains[k] = g;
            let weight = if k == 0 || k == bins - 1 { 1.0 } else { 2.0 };
            let p = self.spectrum[k].norm_sqr() * weight;
            before += p;
            after += p * g * g;
        }
        let level = if after > 1e-30 { (before / after).sqrt() } else { 1.0 };
        for (x, g) in self.spectrum[..bins].iter_mut().zip(self.gains.iter()) {
            *x *= g * level;
        }
        /* DC and Nyquist are real, which the inverse insists on. */
        self.spectrum[0].im = 0.0;
        self.spectrum[bins - 1].im = 0.0;
        let scratch_i = &mut self.scratch[..plan.inverse.get_scratch_len()];
        let ran = plan.inverse.process_with_scratch(&mut self.spectrum[..bins], &mut self.frame[..n], scratch_i);
        debug_assert!(ran.is_ok());

        /* Windowed again and added; the first hop is finished. 0.5 / n: the
         * inverse's n, and the 2 four Hanns sum to. */
        let scale = 0.5 / n as f64;
        for (i, (o, y)) in ch.ola[..n].iter_mut().zip(self.frame.iter()).enumerate() {
            *o += y * self.window[i] * scale;
        }
        ch.out[..hop].copy_from_slice(&ch.ola[..hop]);
        ch.ola.copy_within(hop..n, 0);
        ch.ola[n - hop..n].fill(0.0);
        ch.pos = 0;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::model::Formants;

    #[test]
    fn the_size_follows_the_rate() {
        assert_eq!(size_at(44_100.0), 1024);
        assert_eq!(size_at(48_000.0), 1024);
        assert_eq!(size_at(96_000.0), 2048);
        assert_eq!(size_at(192_000.0), 4096);
        let mut s = Spectral::new(44_100.0);
        assert_eq!((s.size(), s.bins()), (1024, 513));
        s.set_rate(96_000.0);
        assert_eq!(s.size(), 2048);
    }

    #[test]
    fn with_no_trade_the_input_comes_back_one_frame_late() {
        for fs in [48_000.0, 96_000.0] {
            let mut s = Spectral::new(fs);
            s.set_vowel(&Formants::at(0.0, 0.0, 50.0).poles(fs, 1.0));
            let mut ring = Ring::new();
            let mut ch = Channel::new();
            let n = s.size();
            let mut rng = fastrand::Rng::with_seed(3);
            let x: Vec<f64> = (0..8 * n).map(|_| rng.f64() - 0.5).collect();
            let mut worst: f64 = 0.0;
            for (i, &v) in x.iter().enumerate() {
                ring.push(v);
                let y = s.next(&mut ch, &ring, 0.0);
                if i >= 2 * n {
                    worst = worst.max((y - x[i - n]).abs());
                }
            }
            assert!(worst < 1e-12, "{fs}: {worst}");
        }
    }

    #[test]
    fn primed_from_history_it_is_whole_at_once() {
        let fs = 48_000.0;
        let mut s = Spectral::new(fs);
        let mut ring = Ring::new();
        let mut ch = Channel::new();
        let n = s.size();
        let x: Vec<f64> = (0..4 * n).map(|i| (i as f64 * 0.05).sin()).collect();
        for &v in &x[..3 * n] {
            ring.push(v);
        }
        ring.push(x[3 * n]);
        s.prime(&mut ch, &ring, 0.0);
        for i in 3 * n..4 * n {
            if i > 3 * n {
                ring.push(x[i]);
            }
            let y = s.next(&mut ch, &ring, 0.0);
            assert!((y - x[i - n]).abs() < 1e-12, "{i}: {y} vs {}", x[i - n]);
        }
    }
}
