// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The envelope as an analog one charges a capacitor: every stage is a one-pole
heading for a target a little BEYOND where the stage ends, and the stage ends
when the level crosses that point. Aiming past the end is what makes the
stage finish in its stated time; a one-pole aimed at the end itself would only
ever approach it.

The time constant for a stage of `T` ms from `a` to `end`, aiming at `aim`, is

  tau = T / ln(|a - aim| / |end - aim|),

so a retrigger from wherever the level is takes the remaining share of the
attack, not all of it, and starts from that level: no click.
*/

use ni_dsp::onepole::coeff;

/// Where the attack aims, as a fraction past full scale.
pub const OVERSHOOT: f32 = 0.2;
/// How far past its end decay and release aim.
pub const UNDERSHOOT: f32 = 1e-3;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Stage {
    Idle,
    Attack,
    Decay,
    Sustain,
    Release,
}

pub struct Adsr {
    pub attack_ms: f32,
    pub decay_ms: f32,
    pub sustain: f32,
    pub release_ms: f32,
    fs: f32,
    stage: Stage,
    level: f32,
    aim: f32,
    end: f32,
    coef: f32,
}

// ANCHOR: adsr
impl Adsr {
    fn enter(&mut self, stage: Stage) {
        let (end, aim, ms) = match stage {
            Stage::Attack => (1.0, 1.0 + OVERSHOOT, self.attack_ms),
            Stage::Decay => (self.sustain, self.sustain - UNDERSHOOT, self.decay_ms),
            Stage::Release => (0.0, -UNDERSHOOT, self.release_ms),
            Stage::Idle | Stage::Sustain => (self.level, self.level, 0.0),
        };
        let span = ((self.level - aim) / (end - aim)).abs().max(1.0 + 1e-6);
        let tau_ms = ms / span.ln();
        self.coef = coeff(tau_ms as f64, self.fs as f64) as f32;
        (self.stage, self.end, self.aim) = (stage, end, aim);
    }

    #[inline]
    pub fn tick(&mut self) -> f32 {
        if matches!(self.stage, Stage::Idle | Stage::Sustain) {
            return self.level;
        }
        self.level += (self.aim - self.level) * self.coef;
        let rising = self.aim > self.end;
        if (rising && self.level >= self.end) || (!rising && self.level <= self.end) {
            self.level = self.end;
            let next = match self.stage {
                Stage::Attack => Stage::Decay,
                Stage::Decay => Stage::Sustain,
                _ => Stage::Idle,
            };
            self.enter(next);
        }
        self.level
    }
}
// ANCHOR_END: adsr

impl Adsr {
    pub fn new(fs: f32) -> Self {
        Self {
            attack_ms: 5.0,
            decay_ms: 200.0,
            sustain: 0.7,
            release_ms: 300.0,
            fs,
            stage: Stage::Idle,
            level: 0.0,
            aim: 0.0,
            end: 0.0,
            coef: 0.0,
        }
    }

    pub fn gate_on(&mut self) {
        self.enter(Stage::Attack);
    }

    pub fn gate_off(&mut self) {
        if self.stage != Stage::Idle {
            self.enter(Stage::Release);
        }
    }

    pub fn stage(&self) -> Stage {
        self.stage
    }

    pub fn level(&self) -> f32 {
        self.level
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const FS: f32 = 48_000.0;

    fn samples_until(env: &mut Adsr, stage: Stage) -> usize {
        let mut n = 0;
        while env.stage() != stage {
            env.tick();
            n += 1;
            assert!(n < 10 * FS as usize, "never reached {stage:?}");
        }
        n
    }

    #[test]
    fn every_stage_takes_its_stated_time() {
        let mut e = Adsr::new(FS);
        (e.attack_ms, e.decay_ms, e.sustain, e.release_ms) = (10.0, 100.0, 0.5, 250.0);
        e.gate_on();
        let a = samples_until(&mut e, Stage::Decay);
        assert!((a as f32 - 480.0).abs() <= 2.0, "attack {a}");
        assert_eq!(e.level(), 1.0);
        let d = samples_until(&mut e, Stage::Sustain);
        assert!((d as f32 - 4800.0).abs() <= 2.0, "decay {d}");
        assert_eq!(e.tick(), 0.5);
        e.gate_off();
        let r = samples_until(&mut e, Stage::Idle);
        assert!((r as f32 - 12_000.0).abs() <= 2.0, "release {r}");
        assert_eq!(e.tick(), 0.0);
    }

    #[test]
    fn a_retrigger_continues_from_the_current_level() {
        let mut e = Adsr::new(FS);
        e.gate_on();
        for _ in 0..FS as usize / 10 {
            e.tick();
        }
        e.gate_off();
        for _ in 0..FS as usize / 20 {
            e.tick();
        }
        let before = e.level();
        e.gate_on();
        let after = e.tick();
        assert!(after >= before && after - before < 0.05, "{before} -> {after}");
        /* and the rest of the attack is shorter than a whole one */
        let n = samples_until(&mut e, Stage::Decay);
        assert!(n < (e.attack_ms * FS / 1000.0) as usize, "{n}");
    }

    #[test]
    fn gate_off_while_idle_stays_idle() {
        let mut e = Adsr::new(FS);
        e.gate_off();
        assert_eq!(e.stage(), Stage::Idle);
        assert_eq!(e.tick(), 0.0);
    }
}
