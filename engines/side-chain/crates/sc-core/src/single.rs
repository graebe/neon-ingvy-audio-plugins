// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The duck one trigger makes when nothing interrupts it: what the editor draws.

THE IDEALISED SINGLE SHOT, NOT THE ENVELOPE THE ENGINE RUNS. A trigger takes
the duck from open down to the floor, holds it and lets it back; that is the
shape the user asks for, and the shape well draws it. The running envelope
(`shape::Env`) anchors a retrigger on the level it has actually reached, so a
duck interrupted mid-recovery does not retrace this curve -- and no drawing can
show that, because it depends on when the next trigger arrives. The capture
shows what happened; this shows what was asked for.

ONE ARITHMETIC FOR THE CURVE AND ITS HANDLES. The shape and the positions of
the four handles on it come from the same lengths, so a handle sits on the
corner it moves. Lengths are percentages of the cycle, the unit the engine
stores them in, and so are the phases.

THE PHASE WRAPS ON CYCLE, which is what makes a negative Delay drawable: there
Delay is a position in a periodic cycle (`Instance::block_setup`), so -20 % is
80 %, and the duck that belongs to the next beat is already on screen at the
right-hand end of this one. On MIDI and Sidechain Delay is a wait, and a
negative one is no wait at all -- the engine's own clamp.

The editor used to carry this as a JavaScript port pinned to the engine's
curve table; it is the engine's now, behind `sc_shape_render` and
`sc_shape_marks`, so the picture cannot be a second model of the DSP.
*/

use crate::shape::{shape, Curve};

/// The parameters a single shot is drawn from, in the engine's units: the four
/// stages as percentages of the cycle, Depth 0..1, and whether the source is
/// Cycle (whose Delay wraps).
#[derive(Clone, Copy, Debug)]
pub struct Single {
    pub curve: Curve,
    pub delay: f64,
    pub attack: f64,
    pub hold: f64,
    pub release: f64,
    pub depth: f64,
    pub cycle: bool,
}

/// Where the shot's corners sit, as phases 0..100 (wrapped on Cycle), and the
/// two numbers the handles need besides: `span`, attack + hold + release
/// UNWRAPPED -- what says whether the duck can finish inside one cycle, which a
/// wrapped total would hide -- and `floor`, the gain at the bottom, 1 - Depth.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Marks {
    pub start: f64,
    pub bottom: f64,
    pub hold_end: f64,
    pub end: f64,
    pub span: f64,
    pub floor: f64,
}

/// Any phase into 0..100.
fn wrap(v: f64) -> f64 {
    ((v % 100.0) + 100.0) % 100.0
}

/// A length as the engine takes it: never negative, and a NaN is none.
fn len(v: f64) -> f64 {
    if v > 0.0 {
        v
    } else {
        0.0
    }
}

impl Single {
    fn lengths(&self) -> (f64, f64, f64) {
        (len(self.attack), len(self.hold), len(self.release))
    }

    /// Where the duck begins, as a phase.
    pub fn start(&self) -> f64 {
        let d = if self.delay.is_finite() { self.delay } else { 0.0 };
        if self.cycle {
            wrap(d)
        } else {
            d.max(0.0)
        }
    }

    /// The attenuation 0..1 at `t` % of the cycle, Depth not applied.
    #[allow(clippy::neg_cmp_op_on_partial_ord, reason = "a NaN phase must take the guard")]
    pub fn duck_at(&self, t: f64) -> f64 {
        let (a, h, r) = self.lengths();
        let e = if self.cycle { wrap(t - self.start()) } else { t - self.start() };
        if !(e >= 0.0) {
            return 0.0;
        }
        if e < a {
            return shape(self.curve, if a > 0.0 { e / a } else { 1.0 });
        }
        if e < a + h {
            return 1.0;
        }
        if e < a + h + r {
            return 1.0 - shape(self.curve, (e - a - h) / r);
        }
        0.0
    }

    /// The gain at `t`, Depth applied: what the shape well draws.
    pub fn gain_at(&self, t: f64) -> f64 {
        1.0 - self.depth.clamp(0.0, 1.0) * self.duck_at(t)
    }

    /// The gain at `out.len()` phases evenly spaced across the cycle, both
    /// ends included. Allocates nothing.
    pub fn render(&self, out: &mut [f32]) {
        let n = out.len();
        for (i, g) in out.iter_mut().enumerate() {
            let t = if n > 1 { 100.0 * i as f64 / (n - 1) as f64 } else { 0.0 };
            *g = self.gain_at(t) as f32;
        }
    }

    pub fn marks(&self) -> Marks {
        let (a, h, r) = self.lengths();
        let s = self.start();
        let at = |v: f64| if self.cycle { wrap(v) } else { v };
        Marks {
            start: at(s),
            bottom: at(s + a),
            hold_end: at(s + a + h),
            end: at(s + a + h + r),
            span: a + h + r,
            floor: 1.0 - self.depth.clamp(0.0, 1.0),
        }
    }
}

#[cfg(test)]
mod tests;
