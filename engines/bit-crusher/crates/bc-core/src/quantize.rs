// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
Discretising in amplitude: the staircase, and the µ-law
curve the steps are bent by.

THE STAIRCASE. With `b` bits the step is Δ = 2^(1-b), so 2^b steps span full
scale (-1..1). A level is Δ·m for a level index m:

  mid-tread   m is a whole number, so there is a level AT zero and silence
              stays silent: y = Δ·round(x/Δ)
  mid-rise    m is a whole number and a half, so there is a step boundary at
              zero and the smallest signal becomes ±Δ/2: y = Δ·(floor(x/Δ)+½)

HOW MANY LEVELS, which is not the same in the two. Mid-rise has exactly 2^b
(1 bit: ±½), as a b-bit converter does. Mid-tread has 2^b + 1: the level at
zero, and both full scales (1 bit: -1, 0, +1; 8 bits: 257) -- symmetric, as
the common crusher formula round(x·2^(b-1)) is, and one level more than a
b-bit converter, which drops one of the full scales. `Grid::levels` says how
many, and every shell says it beside Bits rather than implying 2^b.

`b` is any real number, not only a whole one, so a sweep of Bits moves the
steps continuously instead of jumping from 256 to 128. Between whole numbers
the top level falls outside full scale; it is drawn back to ±1 (the clip), so
the top step is the short one and no level ever exceeds full scale. The
highest index is the last whose step STARTS inside full scale, which keeps a
full-scale input on the level below rather than on a phantom one above.

Antialiasing is not the staircase's business: the staircase acts on each
level a hold takes, and Anti-Alias draws the hold's jumps band-limited
(hold.rs). A staircase acting on every host sample aliases at the host's rate;
acting on the levels of a band-limited hold, it aliases only as a converter at
Rate would.

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

    /// How many levels there are: every index from -top to top, a whole
    /// number apart -- 2^b + 1 mid-tread, 2^b mid-rise, at whole Bits.
    pub fn levels(&self) -> u32 {
        (2.0 * self.top + 1.0).round() as u32
    }

    /// The level index `x` falls on. Inputs past full scale take the top one.
    /// Mid-rise takes the step above an edge, as floor() does -- so silence,
    /// which sits on the edge at zero, is the half step up -- and mid-tread
    /// rounds half away from zero, the same both ways.
    #[inline]
    pub fn index(&self, x: f64) -> f64 {
        let u = x / self.delta;
        let m = if self.offset > 0.0 { u.floor() + self.offset } else { u.round() };
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
