// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The envelope: the stage machine, walking `ni_dsp::curve`'s shapes.

Every stage has the form `f(w)` with `w = shape(env_t)` and `env_t` running
0..1 across it:

```text
ATTACK   env = att_from + (1 - att_from) * w
DECAY    env = 1        - (1 - sustain)  * w
RELEASE  env = rel_from * (1 - w)
```

Not tidied into a shared `lerp`: `rel_from * (1 - w)` and
`rel_from - rel_from * w` are one number in algebra and two in floating point.
*/

pub use ni_dsp::curve::{shape, shape_inv, Curve};

/// Which part of the envelope is running.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Stage {
    Idle,
    Attack,
    Decay,
    Sustain,
    Release,
}

/// The envelope's whole mutable state, split out of the instance so the stage
/// machine can be read without the pattern data around it.
#[derive(Clone, Copy, Debug)]
pub struct Env {
    pub stage: Stage,
    /// Position through the current stage as 0..1, advanced by a PRECOMPUTED
    /// reciprocal: the stage length cannot change inside a stage, so there is
    /// no per-sample division.
    pub t: f64,
    /// `1 / stage length in samples`; 0 for a zero-length stage.
    pub inc: f64,
    pub level: f32,
    /// Env level when RELEASE began.
    pub rel_from: f32,
    /// Env level when ATTACK began.
    ///
    /// ATTACK NEEDS THE SAME MEMORY RELEASE ALWAYS HAD, and not having it was
    /// the click: an attack restarted from SILENCE wherever the envelope
    /// actually was, so at the boundary between two adjacent ON steps the
    /// gain went 1.000 -> 0.000 in ONE SAMPLE.
    pub att_from: f32,
}

impl Default for Env {
    fn default() -> Self {
        Self {
            stage: Stage::Idle,
            t: 0.0,
            inc: 0.0,
            level: 0.0,
            rel_from: 0.0,
            att_from: 0.0,
        }
    }
}

/// The three stage lengths, in samples, as the caller has already worked them
/// out. Passed in rather than reached for so the stage machine does not need
/// the whole instance.
#[derive(Clone, Copy)]
pub struct StageLens {
    pub attack: f64,
    pub decay: f64,
    pub release: f64,
    pub sustain: f32,
}

impl Env {
    /*
     * Entering a stage, with zero-length stages walked THROUGH rather than
     * recursed through.
     *
     * This was env_enter calling env_settle calling env_enter: bounded in
     * fact (attack -> decay -> sustain is the longest chain a zero can open)
     * but the bound lived in two functions and a guard counter, so it read as
     * unbounded. A 0 ms attack must not spend a sample reporting env == 0 --
     * that is an audible click at the step edge -- which is the whole reason
     * the walk exists.
     */
    pub fn enter(&mut self, mut stage: Stage, l: &StageLens) {
        loop {
            self.stage = stage;
            self.t = 0.0;

            let len = match stage {
                Stage::Attack => {
                    self.att_from = self.level;
                    l.attack
                }
                Stage::Decay => l.decay,
                Stage::Release => {
                    self.rel_from = self.level;
                    l.release
                }
                _ => 0.0,
            };

            /* The one division a stage pays, taken once instead of per
             * sample. Guarded by the zero-length walk below, so it is only
             * ever taken on a positive length. */
            self.inc = if len > 0.0 { 1.0 / len } else { 0.0 };

            /* SUSTAIN and IDLE have no length and are where the walk stops. */
            if !matches!(stage, Stage::Attack | Stage::Decay | Stage::Release) {
                return;
            }
            if len > 0.0 {
                return;
            }

            stage = match stage {
                Stage::Attack => {
                    self.level = 1.0;
                    Stage::Decay
                }
                Stage::Decay => {
                    self.level = l.sustain;
                    Stage::Sustain
                }
                _ => {
                    self.level = 0.0;
                    Stage::Idle
                }
            };
        }
    }

    #[inline]
    pub fn advance(&mut self, curve: Curve, l: &StageLens) {
        /* The shape, evaluated once and substituted for env_t below. Linear
         * hands back env_t itself, so those three lines stay the arithmetic
         * they were. */
        let w = shape(curve, self.t);

        match self.stage {
            Stage::Attack => {
                /* FROM WHERE IT IS, not from zero -- the same thing RELEASE
                 * does with rel_from. It still REACHES 1.0 and still takes
                 * attack_ms to get there; it simply does not fall off a cliff
                 * first. */
                self.level = (self.att_from as f64 + (1.0 - self.att_from as f64) * w) as f32;
                self.t += self.inc;
                if self.t >= 1.0 {
                    self.level = 1.0;
                    self.enter(Stage::Decay, l);
                }
            }
            Stage::Decay => {
                self.level = (1.0 - (1.0 - l.sustain as f64) * w) as f32;
                self.t += self.inc;
                if self.t >= 1.0 {
                    self.level = l.sustain;
                    self.enter(Stage::Sustain, l);
                }
            }
            Stage::Sustain => self.level = l.sustain,
            Stage::Release => {
                self.level = (self.rel_from as f64 * (1.0 - w)) as f32;
                self.t += self.inc;
                if self.t >= 1.0 {
                    self.level = 0.0;
                    self.enter(Stage::Idle, l);
                }
            }
            Stage::Idle => self.level = 0.0,
        }
    }
}

#[cfg(test)]
mod tests;
