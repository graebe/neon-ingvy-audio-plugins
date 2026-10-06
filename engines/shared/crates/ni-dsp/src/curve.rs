// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
Envelope curves: a warp on time.

A stage has the form `f(t)` with `t` running 0..1 across it, so a curve is one
function substituted for `t`. Every shape obeys `shape(0) = 0`, `shape(1) = 1`
and is monotonic, so a stage starts, ends and lasts exactly as before; only the
path between changes. LINEAR RETURNS `t` UNTOUCHED, which keeps reference
renders bit-identical.
*/

/// The path a stage takes between its endpoints.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
#[repr(i32)]
pub enum Curve {
    Linear = 0,
    Exp = 1,
    SCurve = 2,
}

impl Curve {
    pub fn from_i32(v: i32) -> Self {
        match v {
            1 => Curve::Exp,
            2 => Curve::SCurve,
            _ => Curve::Linear,
        }
    }

    pub const COUNT: i32 = 3;

    /// The wire labels, in enum order.
    pub const LABELS: [&'static str; 3] = ["Linear", "Exponential", "S-Curve"];
}

/// The bend: halfway through an exponential stage the envelope is ~82% of
/// the way.
const CURVE_K: f64 = 3.0;
/// `1 - exp(-3)`, spelled out exactly as the C did so the division is the
/// same division. NOT `1.0 - (-CURVE_K).exp()`.
const DENOM: f64 = 0.95021293163213605;

/// Fast, then easing into the target -- what "exponential envelope" means on
/// hardware.
#[inline]
fn curve_exp(t: f64) -> f64 {
    (1.0 - (-CURVE_K * t).exp()) / DENOM
}

#[inline]
fn curve_exp_inv(w: f64) -> f64 {
    let x = 1.0 - w * DENOM;
    if x <= 1e-12 {
        return 1.0;
    }
    -x.ln() / CURVE_K
}

#[inline]
pub fn shape(curve: Curve, t: f64) -> f64 {
    /* `!(t > 0.0)` rather than `t <= 0.0`: a NaN fails both that and the
     * `>= 1.0` below, and would otherwise reach the curve and come out as a
     * NaN level. Ordered this way it lands on the stage's start. */
    if !(t > 0.0) {
        return 0.0;
    }
    if t >= 1.0 {
        return 1.0;
    }
    match curve {
        Curve::Exp => curve_exp(t),
        /* TWO EXPONENTIALS, JOINED: the first half mirrored (slow, then
         * accelerating), the second the right way up -- slow-fast-slow, meeting
         * in the middle at the same slope, Einv'(1) being E'(0). A corner there
         * would be a kink in the gain, audible as surely as a step. */
        Curve::SCurve => {
            if t < 0.5 {
                0.5 * (1.0 - curve_exp(1.0 - 2.0 * t))
            } else {
                0.5 + 0.5 * curve_exp(2.0 * t - 1.0)
            }
        }
        Curve::Linear => t,
    }
}

/// The inverse, which is what lets the curve change mid-stage without a
/// click: the level is re-anchored through it. Monotonic and analytic for all
/// three.
#[inline]
pub fn shape_inv(curve: Curve, w: f64) -> f64 {
    if !(w > 0.0) {
        return 0.0;
    }
    if w >= 1.0 {
        return 1.0;
    }
    match curve {
        Curve::Exp => curve_exp_inv(w),
        Curve::SCurve => {
            if w < 0.5 {
                0.5 * (1.0 - curve_exp_inv(1.0 - 2.0 * w))
            } else {
                0.5 + 0.5 * curve_exp_inv(2.0 * w - 1.0)
            }
        }
        Curve::Linear => w,
    }
}
