// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
Waveshaping with first-order antiderivative antialiasing (ADAA).

A memoryless nonlinearity `f` widens the spectrum of whatever passes it, and
what it widens past Nyquist folds. ADAA replaces `f(x[n])` by the mean of `f`
over the straight line from `x[n-1]` to `x[n]`:

  y[n] = (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1]),   F' = f,

which is `f` convolved with a one-sample rectangle in continuous time: a
low-pass on what the shaper creates, before it is sampled. Where the two
inputs are too close the quotient is ill-conditioned, and the midpoint value
is the limit it tends to. The price is half a sample of delay.
*/

/// The two shapes the paper measures, each with its antiderivative.
pub trait Shape {
    fn f(x: f32) -> f32;
    /// An antiderivative, in f64: it is differenced, and the difference of
    /// two large, close values is where f32 runs out of digits.
    fn big_f(x: f64) -> f64;
}

pub struct Tanh;

impl Shape for Tanh {
    #[inline]
    fn f(x: f32) -> f32 {
        x.tanh()
    }
    /// ln cosh x, written so it cannot overflow: |x| + ln(1 + e^-2|x|) - ln 2.
    #[inline]
    fn big_f(x: f64) -> f64 {
        let a = x.abs();
        a + (-2.0 * a).exp().ln_1p() - core::f64::consts::LN_2
    }
}

pub struct HardClip;

impl Shape for HardClip {
    #[inline]
    fn f(x: f32) -> f32 {
        x.clamp(-1.0, 1.0)
    }
    #[inline]
    fn big_f(x: f64) -> f64 {
        if x.abs() <= 1.0 {
            0.5 * x * x
        } else {
            x.abs() - 0.5
        }
    }
}

// ANCHOR: adaa
pub struct Adaa<S: Shape> {
    x1: f64,     // the previous input
    big_f1: f64, // F of it
    _shape: core::marker::PhantomData<S>,
}

impl<S: Shape> Adaa<S> {
    const EPS: f64 = 1e-5;

    #[inline]
    pub fn tick(&mut self, x: f32) -> f32 {
        let x = x as f64;
        let big_f = S::big_f(x);
        let dx = x - self.x1;
        let y = if dx.abs() > Self::EPS {
            (big_f - self.big_f1) / dx
        } else {
            S::f((0.5 * (x + self.x1)) as f32) as f64 // the limit as dx -> 0
        };
        self.x1 = x;
        self.big_f1 = big_f;
        y as f32
    }
}
// ANCHOR_END: adaa

impl<S: Shape> Default for Adaa<S> {
    fn default() -> Self {
        Self { x1: 0.0, big_f1: S::big_f(0.0), _shape: core::marker::PhantomData }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_antiderivatives_differentiate_back() {
        for i in -40..=40 {
            let x = i as f64 * 0.1 + 0.05;
            let h = 1e-6;
            let d = (Tanh::big_f(x + h) - Tanh::big_f(x - h)) / (2.0 * h);
            assert!((d - x.tanh()).abs() < 1e-6, "tanh at {x}");
            let d = (HardClip::big_f(x + h) - HardClip::big_f(x - h)) / (2.0 * h);
            assert!((d - x.clamp(-1.0, 1.0)).abs() < 1e-6, "clip at {x}");
        }
        assert!(Tanh::big_f(1e6).is_finite());
    }

    #[test]
    fn a_slow_input_comes_out_shaped_and_half_a_sample_late() {
        let mut s = Adaa::<Tanh>::default();
        let w = 2.0 * core::f32::consts::PI * 50.0 / 48_000.0;
        for n in 0..4800 {
            let x = 3.0 * (w * n as f32).sin();
            let y = s.tick(x);
            if n == 0 {
                continue; // there was no x[-1] to be half a sample after
            }
            let mid = 3.0 * (w * (n as f32 - 0.5)).sin();
            assert!((y - mid.tanh()).abs() < 1e-3, "{n}: {y}");
        }
    }

    #[test]
    fn a_constant_input_takes_the_fallback_and_is_exact() {
        let mut s = Adaa::<HardClip>::default();
        s.tick(2.0);
        assert_eq!(s.tick(2.0), 1.0);
        let want = (HardClip::big_f(0.25) - HardClip::big_f(2.0)) / (0.25 - 2.0);
        assert!((s.tick(0.25) as f64 - want).abs() < 1e-7);
    }
}
