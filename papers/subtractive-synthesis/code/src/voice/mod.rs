// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The voice: oscillator, filter, amplifier, and the envelope that moves the
last two. Everything a note owns, and nothing the instrument shares.

The cutoff is modulated in octaves, not in Hz: the envelope adds
`env_amount * env` octaves to the knob's cutoff, because pitch is heard on a
log scale and a sweep in Hz spends nearly all its time at the top. The knob's
own value is glided (ni_dsp's 5 ms one-pole), so turning it is not a staircase;
the envelope already moves smoothly and is applied as it is.
*/

pub mod adsr;
pub mod alloc;
pub mod unison;

use ni_dsp::smooth::{coef, glide};

use crate::filter::ladder::Ladder;
use crate::osc::polyblep::PolyBlepSaw;
use adsr::{Adsr, Stage};

pub struct Voice {
    osc: PolyBlepSaw,
    filter: Ladder,
    pub env: Adsr,
    fs: f32,
    glide_coef: f32,
    cutoff: f32,
    /// The knob, in Hz.
    pub cutoff_target: f32,
    pub resonance: f32,
    /// Octaves of cutoff the envelope adds at full level.
    pub env_amount: f32,
}

// ANCHOR: voice
impl Voice {
    #[inline]
    pub fn tick(&mut self) -> f32 {
        let env = self.env.tick();
        self.cutoff = glide(self.cutoff, self.cutoff_target, self.glide_coef);
        let fc = self.cutoff * (self.env_amount * env).exp2();
        self.filter.set(fc, self.resonance, self.fs); // every sample: TPT allows it
        env * self.filter.tick(self.osc.next())
    }
}
// ANCHOR_END: voice

impl Voice {
    pub fn new(fs: f32) -> Self {
        let filter = Ladder::with_drive(1.0);
        Self {
            osc: PolyBlepSaw::new(110.0, fs),
            filter,
            env: Adsr::new(fs),
            fs,
            glide_coef: coef(fs as f64),
            cutoff: 1_000.0,
            cutoff_target: 1_000.0,
            resonance: 0.3,
            env_amount: 2.0,
        }
    }

    pub fn start(&mut self, f0: f32) {
        crate::osc::Osc::set_freq(&mut self.osc, f0, self.fs);
        self.env.gate_on();
    }

    pub fn stop(&mut self) {
        self.env.gate_off();
    }

    pub fn is_idle(&self) -> bool {
        self.env.stage() == Stage::Idle
    }

    /// Fills `out`, one sample per slot.
    pub fn render(&mut self, out: &mut [f32]) {
        for o in out {
            *o = self.tick();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_note_sounds_then_ends() {
        let mut v = Voice::new(48_000.0);
        assert!(v.is_idle());
        v.start(220.0);
        let mut buf = [0.0f32; 4800];
        v.render(&mut buf);
        let peak = buf.iter().fold(0.0f32, |m, x| m.max(x.abs()));
        assert!(peak > 0.05 && peak < 2.0, "{peak}");
        v.stop();
        for _ in 0..20 {
            v.render(&mut buf);
        }
        assert!(v.is_idle());
        assert!(buf.iter().all(|x| x.abs() < 1e-6));
    }

    #[test]
    fn the_knob_glides_rather_than_jumps() {
        let mut v = Voice::new(48_000.0);
        v.start(110.0);
        v.cutoff_target = 4_000.0;
        v.tick();
        assert!(v.cutoff > 1_000.0 && v.cutoff < 1_100.0, "{}", v.cutoff);
        for _ in 0..48_000 {
            v.tick();
        }
        assert_eq!(v.cutoff, 4_000.0);
    }
}
