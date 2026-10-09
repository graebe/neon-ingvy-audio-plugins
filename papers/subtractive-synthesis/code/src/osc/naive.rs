// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The trivial sawtooth: the analog waveform, sampled. Every harmonic above
Nyquist folds back into the band; this is the baseline the rest beat.
*/

use super::{Osc, Phasor};

// ANCHOR: naive
pub struct NaiveSaw {
    phasor: super::Phasor,
}

impl NaiveSaw {
    #[inline]
    pub fn next(&mut self) -> f32 {
        2.0 * self.phasor.tick() - 1.0
    }
}
// ANCHOR_END: naive

impl NaiveSaw {
    pub fn new(f0: f32, fs: f32) -> Self {
        Self { phasor: Phasor::new(f0, fs) }
    }
}

impl Osc for NaiveSaw {
    fn next(&mut self) -> f32 {
        NaiveSaw::next(self)
    }
    fn set_freq(&mut self, f0: f32, fs: f32) {
        self.phasor.set_freq(f0, fs);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn it_ramps_from_minus_one_to_one() {
        let mut o = NaiveSaw::new(1000.0, 48_000.0);
        let xs: Vec<f32> = (0..48).map(|_| o.next()).collect();
        assert_eq!(xs[0], -1.0);
        assert!(xs.windows(2).all(|w| w[1] > w[0]));
        assert!(xs[47] > 0.95);
    }
}
