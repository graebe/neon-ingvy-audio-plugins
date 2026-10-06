// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The duck's stage machine, walking `ni_dsp::curve`'s shapes.

Every stage has the form `f(w)` with `w = shape(t)` and `t` running 0..1
across it:

```text
ATTACK    duck = att_from + (scale - att_from) * w
RELEASE   duck = rel_from * (1 - w)
```

`duck` is the ATTENUATION: 0 is untouched, `scale` is as far down as this
trigger goes; the gain is `1 - depth * duck`, applied in `lib.rs`. `scale` is
the attack's TARGET, not a factor on the output, so `from` anchoring on a
retrigger compares levels on one scale. Not tidied into a shared `lerp`:
`rel_from * (1 - w)` and `rel_from - rel_from * w` differ in floating point.
*/

pub use ni_dsp::curve::{shape, shape_inv, Curve};

/// Which part of the duck is running.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Stage {
    /// No duck. `duck` is 0 and the signal passes untouched.
    Idle,
    /// Triggered, but not yet moving -- this is "when it begins".
    Delay,
    /// Ducking down.
    Attack,
    /// Held at the bottom.
    Hold,
    /// Recovering.
    Release,
}

/// The envelope's whole state. One struct so a reset is one assignment.
#[derive(Clone, Copy, Debug)]
pub struct Env {
    pub stage: Stage,
    /// 0 = untouched, `scale` = as deep as this trigger goes.
    pub duck: f64,
    /// Samples elapsed in the current stage.
    pub pos: f64,
    /// Where `duck` stood when the current stage began. Attack reads it as
    /// `att_from`, Release as `rel_from`.
    pub from: f64,
    /// How deep THIS trigger goes, 0..1, after Vel -> Depth. The attack's
    /// target. Depth itself is a parameter and is applied in `lib.rs`; this is
    /// the part that belongs to the trigger.
    pub scale: f64,
}

impl Default for Env {
    fn default() -> Self {
        Env {
            stage: Stage::Idle,
            duck: 0.0,
            pos: 0.0,
            from: 0.0,
            scale: 1.0,
        }
    }
}

/// The four stage lengths, in samples. Computed per block by the caller, which
/// is where `Time Mode` (ms vs % of cycle) is resolved -- the machine below
/// does not care which, only that they are samples.
#[derive(Clone, Copy, Debug, Default)]
pub struct Stages {
    pub delay: f64,
    pub attack: f64,
    pub hold: f64,
    pub release: f64,
}

impl Env {
    /// Open the gate immediately and forget the trigger. This is what CC 120
    /// and CC 123 do, and what a stopped transport does to the Cycle source.
    pub fn reset(&mut self) {
        *self = Env::default();
    }

    /// Fire. `scale` is how deep this trigger goes, 0..1.
    ///
    /// THE ATTACK STARTS FROM WHERE THE ENVELOPE ACTUALLY IS, not from zero.
    /// A retrigger during the release would otherwise jump the gain back up to
    /// unity for one sample before ducking again -- a click, and the exact
    /// fault `tg-core`'s `att_from` exists to prevent.
    ///
    /// The stage is always `Delay`, even when Delay is zero: `next` skips a
    /// stage with no time left, so there is no zero-length special case here
    /// and no second copy of the "what comes after Delay" decision.
    pub fn trigger(&mut self, scale: f64, _s: &Stages) {
        self.from = self.duck;
        self.scale = scale.clamp(0.0, 1.0);
        self.pos = 0.0;
        self.stage = Stage::Delay;
    }

    /// Begin the recovery from wherever the machine had got to.
    ///
    /// NOT FROM THE FLOOR. Releasing a duck that never reached the bottom -- a
    /// short trigger, or a note-off during the attack -- from `scale` would
    /// step the gain DOWN at the moment it was asked to come up.
    ///
    /// A zero-length release needs no branch here either; `next` turns it into
    /// the cut it is.
    pub fn release(&mut self, _s: &Stages) {
        if matches!(self.stage, Stage::Idle) {
            return;
        }
        self.from = self.duck;
        self.pos = 0.0;
        self.stage = Stage::Release;
    }

    #[inline]
    fn enter(&mut self, stage: Stage) {
        self.pos = 0.0;
        self.from = self.duck;
        self.stage = stage;
    }

    /// The stage lengths changed between blocks: move `pos` so the running
    /// stage keeps the same FRACTION of itself behind it.
    ///
    /// `pos` is a sample count and `pos / len` is where the stage is, so a
    /// length that changed under it -- a knob, automation, a tempo change,
    /// every one of which moves a length measured in percent of the cycle --
    /// moved the level in one sample: shortening a half-done attack ten times
    /// put it past its end. Scaling `pos` with the length keeps `pos / len`,
    /// and so the level, where it was; the rest of the stage then runs at the
    /// new speed. A stage whose length did not change is untouched, bit for
    /// bit.
    pub fn rescale(&mut self, old: &Stages, new: &Stages) {
        let (o, n) = match self.stage {
            Stage::Idle => return,
            Stage::Delay => (old.delay, new.delay),
            Stage::Attack => (old.attack, new.attack),
            Stage::Hold => (old.hold, new.hold),
            Stage::Release => (old.release, new.release),
        };
        if o != n && o > 0.0 {
            self.pos *= n / o;
        }
    }

    /// Advance one sample and return the attenuation, 0..`scale`.
    ///
    /// `gated` is true while the trigger is still held -- Gate mode with a note
    /// down. Hold then does not expire on its own and only `release()` leaves
    /// it. Every other source passes false and Hold times out.
    ///
    /// `pos` COUNTS SAMPLES ALREADY EMITTED, and a stage is finished when
    /// `pos` reaches its length -- tested BEFORE emitting, not after.
    ///
    /// Testing after is the off-by-one this loop exists to avoid: it emits
    /// `t = 0 .. len/len` inclusive, which is `len + 1` samples per stage, so a
    /// nominal 10/10/10 envelope ran for 33 samples. Tested first, the attack
    /// emits `t = 0 .. (len-1)/len` over exactly `len` samples and the floor
    /// arrives as Hold's first sample -- no sample duplicated, none lost.
    ///
    /// THE LOOP IS WHAT MAKES A ZERO-LENGTH STAGE FREE. Each iteration either
    /// emits and returns, or moves to a strictly later stage, so it cannot spin:
    /// Delay -> Attack -> Hold -> Release -> Idle is four transitions, and an
    /// envelope with every stage at zero takes exactly that many. The bound is
    /// five so that a future stage does not silently turn a missed return into
    /// a hang.
    pub fn next(&mut self, curve: Curve, s: &Stages, gated: bool) -> f64 {
        for _ in 0..5 {
            match self.stage {
                Stage::Idle => {
                    self.duck = 0.0;
                    return 0.0;
                }
                Stage::Delay => {
                    if self.pos < s.delay {
                        self.pos += 1.0;
                        /* The delay holds whatever the last duck was, so a
                         * retrigger during a release does not jump. */
                        return self.duck;
                    }
                    self.enter(Stage::Attack);
                }
                Stage::Attack => {
                    if self.pos < s.attack {
                        let t = self.pos / s.attack;
                        self.duck =
                            self.from + (self.scale - self.from) * shape(curve, t);
                        self.pos += 1.0;
                        return self.duck;
                    }
                    /* The attack is over, so the floor IS reached -- set it
                     * explicitly rather than letting the last shaped value
                     * stand in for it. */
                    self.duck = self.scale;
                    self.enter(Stage::Hold);
                }
                Stage::Hold => {
                    if gated || self.pos < s.hold {
                        self.duck = self.scale;
                        /* A gated hold does not age: the note decides when it
                         * ends, so counting would eventually expire it anyway. */
                        if !gated {
                            self.pos += 1.0;
                        }
                        return self.duck;
                    }
                    self.enter(Stage::Release);
                }
                Stage::Release => {
                    if self.pos < s.release {
                        let t = self.pos / s.release;
                        self.duck = self.from * (1.0 - shape(curve, t));
                        self.pos += 1.0;
                        return self.duck;
                    }
                    self.duck = 0.0;
                    self.from = 0.0;
                    self.pos = 0.0;
                    self.stage = Stage::Idle;
                    return 0.0;
                }
            }
        }
        /* Unreachable with four stages; see the bound's comment. */
        self.duck
    }
}

#[cfg(test)]
mod tests;
