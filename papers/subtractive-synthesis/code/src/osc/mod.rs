// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
Oscillators: the trivial one the others are measured against, the two
polynomial corrections (PolyBLEP, DPW), and the mip-mapped wavetable.

Every oscillator here is driven by the same [`Phasor`], so their differences
are exactly the differences of the paper's section 3.1 and nothing else.
*/

pub mod dpw;
pub mod naive;
pub mod polyblep;
pub mod wavetable;

/// A phase in [0, 1) that advances by `f0 / fs` per sample.
#[derive(Clone, Copy, Debug, Default)]
pub struct Phasor {
    pub phase: f32,
    pub inc: f32,
}

impl Phasor {
    pub fn new(f0: f32, fs: f32) -> Self {
        Self { phase: 0.0, inc: f0 / fs }
    }

    pub fn set_freq(&mut self, f0: f32, fs: f32) {
        self.inc = f0 / fs;
    }

    /// The current phase, then one step forward, wrapped.
    #[inline]
    pub fn tick(&mut self) -> f32 {
        let p = self.phase;
        self.phase += self.inc;
        if self.phase >= 1.0 {
            self.phase -= 1.0;
        }
        p
    }
}

/// What every oscillator in this module is: one sample per call.
pub trait Osc {
    fn next(&mut self) -> f32;
    fn set_freq(&mut self, f0: f32, fs: f32);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_phase_stays_in_range_and_keeps_its_period() {
        let mut p = Phasor::new(1000.0, 48_000.0);
        let mut wraps = 0;
        let mut last = 0.0;
        for _ in 0..48_000 {
            let x = p.tick();
            assert!((0.0..1.0).contains(&x));
            if x < last {
                wraps += 1;
            }
            last = x;
        }
        assert!((999..=1000).contains(&wraps), "{wraps}");
        p.set_freq(2000.0, 48_000.0);
        assert_eq!(p.inc, 2000.0 / 48_000.0);
    }

    #[test]
    fn every_oscillator_answers_through_the_trait() {
        /* What the harness and a voice see: a retune, then samples in range. */
        let table = wavetable::Wavetable::new(&[wavetable::saw_frame(256)], 1);
        let mut oscs: Vec<Box<dyn Osc + '_>> = vec![
            Box::new(naive::NaiveSaw::new(100.0, 48_000.0)),
            Box::new(polyblep::PolyBlepSaw::new(100.0, 48_000.0)),
            Box::new(polyblep::PolyBlepPulse::new(100.0, 48_000.0, 0.5)),
            Box::new(dpw::DpwSaw::new(100.0, 48_000.0)),
            Box::new(wavetable::WavetableOsc::new(&table, 100.0, 48_000.0, 24_000.0)),
        ];
        for o in oscs.iter_mut() {
            o.set_freq(440.0, 48_000.0);
            let peak = (0..4800).map(|_| o.next().abs()).fold(0.0f32, f32::max);
            assert!((0.9..1.2).contains(&peak), "{peak}");
        }
    }
}
