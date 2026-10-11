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
    /// Whether the last `step` took its input.
    took: bool,
}

impl Default for Hold {
    fn default() -> Self {
        Hold { phase: 1.0, stretch: 1.0, held: 0.0, took: false }
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
        self.took = self.phase >= 1.0;
        if self.took {
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

    /// Whether the last sample was one it took -- the instants an editor's
    /// picture of the hold marks.
    pub fn took(&self) -> bool {
        self.took
    }

    /// Where its clock is in the current period, 0..1 (1 or more: a level is
    /// due).
    pub fn phase(&self) -> f64 {
        self.phase
    }
}

/*
 * THE BAND-LIMITED HOLD, Anti-Alias's: the same clock, read exactly.
 *
 * The plain Hold above takes a level on the first host sample at or after its
 * clock's instant, and the jump to it lands on that sample: 12 kHz at 44.1 kHz
 * is held for 3 and 4 samples by turns, which a 48 kHz project does not do --
 * the host's grid is in the sound. This one takes the level AT the instant,
 * reading the input between samples through the kernel (blep::kernel), and
 * draws the jump AT the instant as a band-limited step (blep::residual). What
 * comes out is a sample-and-hold running in continuous time at Rate, heard
 * through the project's sample rate: the same at every host rate, and with
 * nothing above Nyquist left to fold back. A Rate above the host's takes
 * several levels in one host sample, each at its own instant.
 *
 * The cost is blep::LATENCY samples: the input HALF_WIDTH ahead of an instant
 * to read it, the output HALF_WIDTH after it to draw the step. The plain hold
 * is delayed by the same in lib.rs, so the two can be crossfaded.
 *
 * The level a capture becomes is the caller's (the steps: Shape, Dither, the
 * grid), asked once per capture.
 */

use crate::blep::{self, HALF_WIDTH};

/// The rings' length: the kernel's span on either side, and room.
const RING: usize = 64;
/// The most levels taken in one host sample before the rest are skipped: a
/// Rate many times the host's (a host at an absurd rate) must not make a
/// block's work unbounded.
const MOST_PER_SAMPLE: f64 = 8.0;

#[derive(Clone, Debug)]
pub struct BandLimitedHold {
    /// The input, by host sample.
    input: [f64; RING],
    /// The held level at each internal sample, before correction.
    held: [f64; RING],
    /// The band-limited steps' corrections, by internal sample.
    correction: [f64; RING],
    /// Host samples taken in.
    count: i64,
    phase: f64,
    stretch: f64,
    level: f64,
    /// The instant of the first level the last `step` took, relative to the
    /// input sample it was given: about HALF_WIDTH back. None for none.
    first: Option<f64>,
}

impl Default for BandLimitedHold {
    fn default() -> Self {
        blep::prepare();
        BandLimitedHold {
            input: [0.0; RING],
            held: [0.0; RING],
            correction: [0.0; RING],
            count: 0,
            phase: 1.0,
            stretch: 1.0,
            level: 0.0,
            first: None,
        }
    }
}

#[inline]
fn slot(t: i64) -> usize {
    t.rem_euclid(RING as i64) as usize
}

impl BandLimitedHold {
    /// Back to the start: nothing held, the next instant taken at once.
    pub fn reset(&mut self) {
        *self = BandLimitedHold::default();
    }

    /// The input between samples, at `t` host samples (`t` at most
    /// HALF_WIDTH behind the latest sample taken in).
    #[inline]
    fn read(&self, t: f64) -> f64 {
        let base = t.floor() as i64;
        let (mut sum, mut weight) = (0.0, 0.0);
        for m in (base - HALF_WIDTH as i64 + 1)..=(base + HALF_WIDTH as i64) {
            let k = blep::kernel(t - m as f64);
            sum += self.input[slot(m)] * k;
            weight += k;
        }
        if weight.abs() > 1e-9 {
            sum / weight
        } else {
            sum
        }
    }

    /// A step to `level` at instant `t`, drawn band-limited.
    #[inline]
    fn jump(&mut self, t: f64, level: f64) {
        let height = level - self.level;
        self.level = level;
        if height == 0.0 {
            return;
        }
        let base = t.floor() as i64;
        for m in (base - HALF_WIDTH as i64 + 1)..=(base + HALF_WIDTH as i64) {
            self.correction[slot(m)] += height * blep::residual(m as f64 - t);
        }
    }

    /// One host sample `x` in, one out LATENCY samples later. `step`, `jitter`
    /// and `rng` are Hold::step's; `level_of` turns a captured value into the
    /// level held.
    #[inline]
    #[allow(clippy::neg_cmp_op_on_partial_ord, reason = "a NaN step must stop the clock, not reach a division")]
    pub fn step(
        &mut self,
        x: f64,
        step: f64,
        jitter: f64,
        rng: &mut fastrand::Rng,
        mut level_of: impl FnMut(f64) -> f64,
    ) -> f64 {
        let n = self.count;
        self.input[slot(n)] = x;
        /* The internal sample: HALF_WIDTH behind the input, so the input on
         * both sides of any instant in (now - 1, now] is in hand. */
        let now = n - HALF_WIDTH as i64;
        let start = (now - 1) as f64;
        let mut t = start;
        let mut left = 1.0;
        self.first = None;
        loop {
            let inc = step * self.stretch;
            if !(inc > 0.0) {
                break;
            }
            if inc * left > MOST_PER_SAMPLE {
                /* More levels than this sample can afford: the last one,
                 * at the end of it. */
                self.phase = 0.0;
                let level = level_of(self.read(now as f64));
                self.jump(now as f64, level);
                self.first.get_or_insert((now - n) as f64);
                break;
            }
            let need = (1.0 - self.phase).max(0.0);
            if need > inc * left {
                self.phase += inc * left;
                break;
            }
            let dt = need / inc;
            t += dt;
            left -= dt;
            /* An instant at the very start of the interval belongs to it:
             * the previous sample's level is already written. */
            let at = if t > start { t.min(now as f64) } else { start + 1e-9 };
            let level = level_of(self.read(at));
            self.jump(at, level);
            self.first.get_or_insert(at - n as f64);
            self.phase = 0.0;
            let r = 2.0 * rng.f64() - 1.0;
            self.stretch = 1.0 + JITTER_DEPTH * jitter * r;
        }
        self.held[slot(now)] = self.level;
        self.count += 1;
        self.emit(now)
    }

    /// The instant of the first level the last `step` took, in samples
    /// relative to the input sample it was given (about -HALF_WIDTH), or None.
    pub fn first_take(&self) -> Option<f64> {
        self.first
    }

    /// While Anti-Alias is off: keep the input history and follow the plain
    /// hold's `level` and `phase`, so switching on starts from where the plain
    /// hold is rather than from silence.
    #[inline]
    pub fn follow(&mut self, x: f64, level: f64, phase: f64) {
        let n = self.count;
        self.input[slot(n)] = x;
        let now = n - HALF_WIDTH as i64;
        self.level = level;
        self.phase = phase;
        self.held[slot(now)] = level;
        self.count += 1;
        self.emit(now);
    }

    #[inline]
    fn emit(&mut self, now: i64) -> f64 {
        let out = now - HALF_WIDTH as i64;
        let y = self.held[slot(out)] + self.correction[slot(out)];
        self.correction[slot(out)] = 0.0;
        y
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
