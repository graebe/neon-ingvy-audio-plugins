// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
2x oversampling with a linear-phase half-band FIR, up and down.

A half-band filter cuts at a quarter of its own rate, and its impulse response
is zero at every even offset from the centre except the centre itself, which
is one half. Half the taps are free, and the polyphase split falls out of it:
going up, the even outputs are the input itself (delayed) and the odd ones a
convolution with the odd taps; coming down, one output per input pair is the
centre tap on the even sample plus the odd taps on the odd ones.

The taps are a windowed sinc with a Kaiser window. `M = 24` odd taps per side
(95 in all) give at least 80 dB from 0.28 of the doubled rate up, and a
passband flat to a thousandth of a dB up to 0.22: 21.1 kHz and 26.9 kHz at
48 kHz, the band any synth needs and the band images must leave.
*/

/// Odd taps on each side of the centre.
pub const M: usize = 24;
/// The Kaiser window's shape.
pub const BETA: f64 = 8.0;

// ANCHOR: halfband
/// The odd taps `h[1], h[3], ..., h[2M-1]`; `h[0]` is 1/2 and the even ones 0.
pub fn halfband_taps() -> [f32; M] {
    let half = (2 * M) as f64; // the window reaches zero one tap past the last
    core::array::from_fn(|j| {
        let m = (2 * j + 1) as f64;
        let sinc = (core::f64::consts::FRAC_PI_2 * m).sin() / (core::f64::consts::PI * m);
        let r = m / half;
        (sinc * bessel_i0(BETA * (1.0 - r * r).sqrt()) / bessel_i0(BETA)) as f32
    })
}

/// The modified Bessel function of the first kind, order zero, by its series.
fn bessel_i0(x: f64) -> f64 {
    let (mut sum, mut term, mut k) = (1.0, 1.0, 1.0);
    while term > 1e-12 * sum {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        k += 1.0;
    }
    sum
}
// ANCHOR_END: halfband

/// Up by two: one input, two outputs, `M` input samples late.
pub struct Up {
    taps: [f32; M],
    hist: [f32; 2 * M], // newest first
}

// ANCHOR: up
impl Up {
    #[inline]
    pub fn tick(&mut self, x: f32) -> [f32; 2] {
        self.hist.copy_within(0..2 * M - 1, 1);
        self.hist[0] = x;
        // Odd output: the odd taps, mirrored about the delayed centre, times 2
        // for the zeros the stuffing put in.
        let mut odd = 0.0;
        for (j, &h) in self.taps.iter().enumerate() {
            odd += h * (self.hist[M - 1 - j] + self.hist[M + j]);
        }
        [self.hist[M], 2.0 * odd]
    }
}
// ANCHOR_END: up

/// Down by two: two inputs, one output, `M - 1` output samples late.
pub struct Down {
    taps: [f32; M],
    even: [f32; M],     // newest first
    odd: [f32; 2 * M],  // newest first
}

// ANCHOR: down
impl Down {
    #[inline]
    pub fn tick(&mut self, pair: [f32; 2]) -> f32 {
        self.even.copy_within(0..M - 1, 1);
        self.even[0] = pair[0];
        self.odd.copy_within(0..2 * M - 1, 1);
        self.odd[0] = pair[1];
        let mut y = 0.5 * self.even[M - 1];
        for (j, &h) in self.taps.iter().enumerate() {
            y += h * (self.odd[M - 1 - j] + self.odd[M + j]);
        }
        y
    }
}
// ANCHOR_END: down

impl Default for Up {
    fn default() -> Self {
        Self { taps: halfband_taps(), hist: [0.0; 2 * M] }
    }
}

impl Default for Down {
    fn default() -> Self {
        Self { taps: halfband_taps(), even: [0.0; M], odd: [0.0; 2 * M] }
    }
}

/// Up, a function at the doubled rate, down: the whole round trip, `2M - 1`
/// samples late.
pub struct Oversampled<F: FnMut(f32) -> f32> {
    up: Up,
    down: Down,
    pub f: F,
}

impl<F: FnMut(f32) -> f32> Oversampled<F> {
    pub fn new(f: F) -> Self {
        Self { up: Up::default(), down: Down::default(), f }
    }

    #[inline]
    pub fn tick(&mut self, x: f32) -> f32 {
        let [a, b] = self.up.tick(x);
        self.down.tick([(self.f)(a), (self.f)(b)])
    }
}

/// The latency of a round trip, in samples at the base rate.
pub const LATENCY: usize = 2 * M - 1;

#[cfg(test)]
mod tests {
    use super::*;

    /// |H| of the whole half-band filter at `f`, a fraction of the doubled rate.
    fn response(f: f64) -> f64 {
        let w = 2.0 * core::f64::consts::PI * f;
        let taps = halfband_taps();
        let h = 0.5 + taps.iter().enumerate().map(|(j, &t)| 2.0 * t as f64 * (w * (2 * j + 1) as f64).cos()).sum::<f64>();
        h.abs()
    }

    #[test]
    fn flat_passband_and_an_80_db_stopband() {
        for i in 0..=220 {
            let db = 20.0 * response(i as f64 / 1000.0).log10();
            assert!(db.abs() < 0.001, "passband {i}: {db} dB");
        }
        for i in 280..=500 {
            let db = 20.0 * response(i as f64 / 1000.0).log10();
            assert!(db < -80.0, "stopband {i}: {db} dB");
        }
        /* The half-band point: exactly half, by symmetry. */
        assert!((response(0.25) - 0.5).abs() < 1e-6);
    }

    #[test]
    fn a_round_trip_is_a_delay() {
        let mut os = Oversampled::new(|x| x);
        let w = 2.0 * core::f32::consts::PI * 1_000.0 / 48_000.0;
        let ys: Vec<f32> = (0..4800).map(|n| os.tick((w * n as f32).sin())).collect();
        for n in 1000..4800 {
            let want = (w * (n - LATENCY) as f32).sin();
            assert!((ys[n] - want).abs() < 1e-3, "{n}: {} vs {want}", ys[n]);
        }
    }

    #[test]
    fn going_up_keeps_the_input_on_the_even_samples() {
        let mut up = Up::default();
        let xs: Vec<f32> = (0..200).map(|n| ((n * 37) % 11) as f32).collect();
        for (n, &x) in xs.iter().enumerate() {
            let [even, _] = up.tick(x);
            if n >= M {
                assert_eq!(even, xs[n - M]);
            }
        }
    }
}
