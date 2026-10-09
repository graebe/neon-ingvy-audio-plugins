// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
Discretising in amplitude: the staircase, its antiderivative, and the µ-law
curve the steps are bent by.

THE STAIRCASE. With `b` bits the step is Δ = 2^(1-b), so 2^b steps span full
scale (-1..1). A level is Δ·m for a level index m:

  mid-tread   m is a whole number, so there is a level AT zero and silence
              stays silent: y = Δ·round(x/Δ)
  mid-rise    m is a whole number and a half, so there is a step boundary at
              zero and the smallest signal becomes ±Δ/2: y = Δ·(floor(x/Δ)+½)

`b` is any real number, not only a whole one, so a sweep of Bits moves the
steps continuously instead of jumping from 256 to 128. Between whole numbers
the top level falls outside full scale; it is drawn back to ±1 (the clip), so
the top step is the short one and no level ever exceeds full scale. The
highest index is the last whose step STARTS inside full scale, which keeps a
full-scale input on the level below rather than on a phantom one above.

ANTIALIASING (first-order ADAA, Parker, Zavalishin & Le Bivic, DAFx-16;
Bilbao, Esqueda, Parker & Välimäki, IEEE SPL 2017). The staircase is a
memoryless nonlinearity, so its harmonics fold back below Nyquist. Instead of
Q(x[n]) the output is the mean of Q over the segment from x[n-1] to x[n]:

```text
y[n] = (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1]),    F' = Q
```

F of a staircase is continuous and piecewise linear. On the step with level
index m it is Δ·m·x - Δ²·m²/2 (which is (x² - (x - Δm)²)/2), and on the top
step, whose level the clip moved, it continues from the step's lower edge with
the clipped level as its slope. The paper's ill-conditioned case -- the two
inputs too close to divide by -- is handled exactly: on one step the mean IS
that step's level, and across steps the gap is never smaller than the distance
to a boundary, so only an input that sits on a boundary to 1e-12 takes the
fallback, the level at the midpoint. The result is clamped between the two
endpoint levels, which the true mean always lies between, so rounding can
never make it overshoot.

THE SHAPE (µ-law, as G.711 and Redux's Shape). The input is compressed, the
compressed value quantised, and the result expanded again, so the steps are
fine near silence and coarse near full scale. µ runs from 0 (even steps) to
255 as Shape goes from 0 to 100 %, along µ = 256^shape - 1, so the bend grows
evenly with the knob rather than all at once near the top.
*/

/// One quantiser setting: the step, which grid, and the top level index.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Grid {
    delta: f64,
    /// 0 for mid-tread, ½ for mid-rise: the fractional part of every index.
    offset: f64,
    /// The highest level index, whose step starts inside full scale.
    top: f64,
}

/// Closer than this and two ADAA inputs are one point.
const ADAA_EPS: f64 = 1e-12;

impl Grid {
    /// The grid for `bits` (at least 1), mid-tread or mid-rise.
    pub fn new(bits: f64, mid_rise: bool) -> Grid {
        let bits = if bits.is_nan() { 1.0 } else { bits.max(1.0) };
        let delta = (1.0 - bits).exp2();
        let offset = if mid_rise { 0.5 } else { 0.0 };
        /* The largest m (≡ offset, mod 1) with m - ½ < 1/Δ: the step it
         * stands for starts below full scale. */
        let top = ((1.0 / delta + 0.5 - offset).ceil() - 1.0) + offset;
        Grid { delta, offset, top }
    }

    /// The step, Δ.
    pub fn delta(&self) -> f64 {
        self.delta
    }

    /// The level index `x` falls on. Inputs past full scale take the top one.
    #[inline]
    pub fn index(&self, x: f64) -> f64 {
        let m = (x / self.delta - self.offset).round() + self.offset;
        m.clamp(-self.top, self.top)
    }

    /// The level of index `m`, never past full scale.
    #[inline]
    pub fn level(&self, m: f64) -> f64 {
        (m * self.delta).clamp(-1.0, 1.0)
    }

    /// The staircase: `x` quantised, and clipped to full scale.
    #[inline]
    pub fn quantize(&self, x: f64) -> f64 {
        self.level(self.index(x))
    }

    /// F, the staircase's antiderivative, with F(0) = 0 on mid-tread.
    #[inline]
    pub fn antiderivative(&self, x: f64) -> f64 {
        let m = self.index(x);
        if m.abs() < self.top {
            return self.delta * m * x - 0.5 * self.delta * self.delta * m * m;
        }
        /* The top step: from its lower edge, where the step below meets it
         * and the open formula still holds, with the clipped level as the
         * slope. */
        let edge = m.signum() * self.delta * (self.top - 0.5);
        let at_edge = self.delta * m * edge - 0.5 * self.delta * self.delta * m * m;
        at_edge + self.level(m) * (x - edge)
    }

    /// The staircase's mean over the segment from `x0` to `x1`: first-order
    /// ADAA. Equals `quantize(x1)` while both ends sit on one step.
    #[inline]
    pub fn mean(&self, x0: f64, x1: f64) -> f64 {
        let (m0, m1) = (self.index(x0), self.index(x1));
        if m0 == m1 {
            return self.level(m1);
        }
        let dx = x1 - x0;
        if dx.abs() < ADAA_EPS {
            return self.quantize(0.5 * (x0 + x1));
        }
        let y = (self.antiderivative(x1) - self.antiderivative(x0)) / dx;
        let (a, b) = (self.level(m0), self.level(m1));
        y.clamp(a.min(b), a.max(b))
    }
}

/// The µ-law bend around the quantiser.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Shape {
    mu: f64,
    /// ln(1 + µ), the curve's scale.
    log1p_mu: f64,
}

/// The µ at Shape 100 %: G.711's.
pub const MU_MAX: f64 = 255.0;
/// Below this µ the curve is a straight line to within rounding.
const MU_LINEAR: f64 = 1e-6;

impl Shape {
    /// The curve for `amount` 0..1.
    pub fn new(amount: f64) -> Shape {
        let a = if amount.is_nan() { 0.0 } else { amount.clamp(0.0, 1.0) };
        let mu = (1.0 + MU_MAX).powf(a) - 1.0;
        Shape { mu, log1p_mu: mu.ln_1p() }
    }

    fn is_linear(&self) -> bool {
        self.mu < MU_LINEAR
    }

    /// Into the quantiser: odd, and 1 at full scale.
    #[inline]
    pub fn compress(&self, x: f64) -> f64 {
        if self.is_linear() {
            return x;
        }
        x.signum() * (self.mu * x.abs()).ln_1p() / self.log1p_mu
    }

    /// Out of it: the inverse of `compress`.
    #[inline]
    pub fn expand(&self, y: f64) -> f64 {
        if self.is_linear() {
            return y;
        }
        y.signum() * (y.abs() * self.log1p_mu).exp_m1() / self.mu
    }
}

#[cfg(test)]
mod tests;
