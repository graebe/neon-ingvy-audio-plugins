// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
tanh, antialiased: a soft clip that folds back less than it would sampled
plainly.

A memoryless curve widens the spectrum of whatever passes it, and what it
widens past Nyquist folds. First-order antiderivative antialiasing (ADAA)
replaces `f(x[n])` by the mean of `f` over the straight line from `x[n-1]`
to `x[n]`:

```text
  y[n] = (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1]),   F' = f = tanh,  F = ln cosh
```

-- `f` convolved with a one-sample rectangle before it is sampled, a low-pass
on what the curve creates. Where the two inputs are too close the quotient is
ill-conditioned, and the midpoint value is the limit it tends to. The price is
half a sample of delay. papers/subtractive-synthesis measures the same
construction; this is its product-grade copy, in f64 throughout.
*/

/// ln cosh x, written so it cannot overflow: |x| + ln(1 + e^-2|x|) - ln 2.
#[inline]
pub fn ln_cosh(x: f64) -> f64 {
    let a = x.abs();
    a + (-2.0 * a).exp().ln_1p() - core::f64::consts::LN_2
}

/// tanh through first-order ADAA. One per channel: it remembers one sample.
#[derive(Clone, Copy, Debug)]
pub struct AdaaTanh {
    /// The previous input, and ln cosh of it.
    x1: f64,
    big_f1: f64,
}

impl Default for AdaaTanh {
    fn default() -> Self {
        AdaaTanh::new()
    }
}

impl AdaaTanh {
    /// Below this the quotient is cancellation, and the midpoint is used.
    const EPS: f64 = 1e-6;

    pub const fn new() -> AdaaTanh {
        AdaaTanh { x1: 0.0, big_f1: 0.0 }
    }

    /// As new: the next sample is shaped as if silence came before it.
    pub fn reset(&mut self) {
        *self = AdaaTanh::new();
    }

    /// tanh of `x`, antialiased, half a sample late.
    #[inline]
    pub fn run(&mut self, x: f64) -> f64 {
        let big_f = ln_cosh(x);
        let dx = x - self.x1;
        let y = if dx.abs() > Self::EPS {
            (big_f - self.big_f1) / dx
        } else {
            (0.5 * (x + self.x1)).tanh()
        };
        self.x1 = x;
        self.big_f1 = big_f;
        y
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ln_cosh_differentiates_to_tanh_and_never_overflows() {
        for i in -40..=40 {
            let x = i as f64 * 0.1 + 0.05;
            let h = 1e-6;
            let d = (ln_cosh(x + h) - ln_cosh(x - h)) / (2.0 * h);
            assert!((d - x.tanh()).abs() < 1e-6, "at {x}");
        }
        assert_eq!(ln_cosh(0.0), 0.0);
        assert!(ln_cosh(1e300).is_finite());
    }

    #[test]
    fn a_slow_input_comes_out_shaped_and_half_a_sample_late() {
        let mut s = AdaaTanh::new();
        let w = core::f64::consts::TAU * 50.0 / 48_000.0;
        for n in 0..4800 {
            let y = s.run(3.0 * (w * n as f64).sin());
            if n > 0 {
                let mid = 3.0 * (w * (n as f64 - 0.5)).sin();
                assert!((y - mid.tanh()).abs() < 1e-4, "{n}: {y}");
            }
        }
    }

    #[test]
    fn a_held_input_takes_the_midpoint_and_reset_forgets() {
        let mut s = AdaaTanh::new();
        s.run(0.5);
        assert_eq!(s.run(0.5), 0.5f64.tanh());
        s.reset();
        let want = ln_cosh(2.0) / 2.0;
        assert!((s.run(2.0) - want).abs() < 1e-15);
        assert_eq!(AdaaTanh::default().run(0.0), 0.0);
    }
}
