// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
Discretising in time: a sample-and-hold on a fractional clock.

A PHASE ACCUMULATOR, NOT A COUNTER. Each sample the phase moves on by
Rate / host rate; when it reaches 1 the hold takes the input and the phase
keeps the remainder. So a rate that does not divide the host's -- 12 kHz at
44.1 kHz, a hold of 3.675 samples -- holds for 3 and 4 samples in the
proportion that averages to 3.675, and the clock does not drift. At a rate of
the host's own or above, every sample is taken: the hold is the identity.

The first sample after a reset is taken at once (the phase starts at 1), so a
block that starts from silence does not hold silence for a period first.

JITTER moves the clock, not the samples: each period's step is the base step
times 1 + JITTER_DEPTH·jitter·r, r uniform in -1..1, drawn when the period
starts. A period is then up to 1/(1 - JITTER_DEPTH) as long, or nearly half as
short, and Rate still moves smoothly underneath it. Each channel draws from its
own generator, so jitter decorrelates left from right -- the width it adds --
while with no jitter both channels take the same samples at the same time.
*/

/// How far a period can move at Jitter 100 %, as a fraction of its step.
pub const JITTER_DEPTH: f64 = 0.8;

/// One channel's hold.
#[derive(Clone, Debug)]
pub struct Hold {
    phase: f64,
    /// This period's factor on the step: 1 without jitter.
    stretch: f64,
    held: f64,
}

impl Default for Hold {
    fn default() -> Self {
        Hold { phase: 1.0, stretch: 1.0, held: 0.0 }
    }
}

impl Hold {
    /// Back to the start: nothing held, the next sample taken at once.
    pub fn reset(&mut self) {
        *self = Hold::default();
    }

    /// One sample: `x` in, the held value out. `step` is Rate / host rate;
    /// `jitter` 0..1; `rng` this channel's generator, drawn from once a period
    /// whatever the jitter, so the sequence does not depend on it.
    #[inline]
    pub fn step(&mut self, x: f64, step: f64, jitter: f64, rng: &mut fastrand::Rng) -> f64 {
        if self.phase >= 1.0 {
            self.phase = self.phase.fract();
            self.held = x;
            let r = 2.0 * rng.f64() - 1.0;
            self.stretch = 1.0 + JITTER_DEPTH * jitter * r;
        }
        self.phase += step * self.stretch;
        self.held
    }

    /// What it holds now.
    pub fn held(&self) -> f64 {
        self.held
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn run(step: f64, jitter: f64, input: impl Iterator<Item = f64>, seed: u64) -> Vec<f64> {
        let mut h = Hold::default();
        let mut rng = fastrand::Rng::with_seed(seed);
        input.map(|x| h.step(x, step, jitter, &mut rng)).collect()
    }

    #[test]
    fn at_the_host_rate_and_above_it_is_the_identity() {
        for step in [1.0, 1.5, 2.0] {
            let out = run(step, 0.0, (0..64).map(f64::from), 1);
            assert_eq!(out, (0..64).map(f64::from).collect::<Vec<_>>(), "step {step}");
        }
    }

    #[test]
    fn a_quarter_of_the_rate_holds_exactly_four_samples() {
        let out = run(0.25, 0.0, (0..16).map(f64::from), 1);
        let want: Vec<f64> = (0..16).map(|i| f64::from(i / 4 * 4)).collect();
        assert_eq!(out, want);
    }

    #[test]
    fn a_fractional_rate_holds_for_its_mean_length_without_drift() {
        /* 12 kHz at 44.1 kHz: 3.675 samples a period. */
        let step = 12_000.0 / 44_100.0;
        let n = 44_100;
        let out = run(step, 0.0, (0..n).map(f64::from), 1);
        let takes = 1 + out.windows(2).filter(|w| w[0] != w[1]).count();
        assert!((takes as f64 - 12_000.0).abs() <= 1.0, "{takes} takes in a second");
        let lengths: Vec<usize> = out
            .chunk_by(|a, b| a == b)
            .map(<[f64]>::len)
            .collect();
        assert!(lengths[..lengths.len() - 1].iter().all(|&l| l == 3 || l == 4));
    }

    #[test]
    fn the_first_sample_is_taken_at_once() {
        let out = run(0.01, 0.0, [0.5, 0.0, 0.0].into_iter(), 1);
        assert_eq!(out, vec![0.5, 0.5, 0.5]);
    }

    #[test]
    fn jitter_is_deterministic_for_a_seed_and_changes_the_periods() {
        let a = run(0.1, 1.0, (0..4000).map(f64::from), 7);
        let b = run(0.1, 1.0, (0..4000).map(f64::from), 7);
        assert_eq!(a, b);
        let plain = run(0.1, 0.0, (0..4000).map(f64::from), 7);
        assert_ne!(a, plain);
        let lengths: Vec<usize> = a.chunk_by(|x, y| x == y).map(<[f64]>::len).collect();
        let (lo, hi) = (lengths.iter().min().unwrap(), lengths.iter().max().unwrap());
        assert!(*lo <= 6 && *hi >= 20, "periods {lo}..{hi} barely move");
    }

    #[test]
    fn without_jitter_two_generators_take_the_same_samples() {
        let a = run(0.3, 0.0, (0..500).map(f64::from), 1);
        let b = run(0.3, 0.0, (0..500).map(f64::from), 99);
        assert_eq!(a, b);
    }

    #[test]
    fn reset_forgets_what_was_held() {
        let mut h = Hold::default();
        let mut rng = fastrand::Rng::with_seed(1);
        h.step(0.7, 0.01, 0.0, &mut rng);
        assert_eq!(h.held(), 0.7);
        h.reset();
        assert_eq!(h.held(), 0.0);
        assert_eq!(h.step(-0.2, 0.01, 0.0, &mut rng), -0.2);
    }
}
