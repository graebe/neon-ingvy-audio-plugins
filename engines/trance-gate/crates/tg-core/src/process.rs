// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The gain loop: step boundaries, the envelope, the gain law, and the three buffer
formats it is applied to.
*/

use crate::clock::Run;
use crate::envelope::{Stage, StageLens};
use crate::{Instance, Transport, MAX_STEPS};

impl Instance {
    /*
     * A step boundary. This is the whole of what a tie means: an ON step
     * arriving on top of a held ON step does NOT restart the envelope.
     */
    fn on_step_boundary(&mut self, prev_step: Option<usize>, new_step: usize, l: &StageLens) {
        /* THE FADE'S WEIGHTS ARE READ BEFORE THE PATTERN IS BORROWED, not
         * because the borrow checker insists but because `sounds` consults
         * `self` and `p` holds a shared borrow of it. Three reads, named. */
        let on_now = self.sounds(new_step);
        let on_prev = prev_step.is_some_and(|s| self.sounds(s));
        let w_now = if new_step < MAX_STEPS { self.fade_w[new_step] } else { 0.0 };
        let p = &self.pat[self.slot];
        let tied = prev_step.is_some_and(|s| p.tied(s));

        if on_now {
            /*
             * LEGATO IS "TREAT EVERY ADJACENT PAIR AS TIED".
             *
             * At Sustain 100% this changes nothing audible, because attack
             * already ramps from wherever the envelope is and there is
             * nowhere to ramp from a fully open gate. The difference appears
             * BELOW 100%, which is where a retrigger actually re-articulates.
             */
            if !(on_prev && (tied || self.snd().legato)) {
                /* THE FADE SCALES THE STEP'S LEVEL, AND IT SCALES IT HERE.
                 *
                 * This is the one place the struck step's level is read, and
                 * it is latched for the whole gate -- so a step whose arrival
                 * completes mid-gate does not step its own level and click.
                 * The knob is smooth ACROSS steps rather than within one,
                 * which is what "the fields are introduced one by one" means.
                 *
                 * A weight of 0 never reaches this line: `on_now` is already
                 * false there, so the step is a gap. */
                let lvl = p.depth[new_step] as f32 * (1.0 / 255.0) * w_now;

                /*
                 * THE ATTACK STARTS WHERE THE GAIN IS, NOT WHERE `env` IS.
                 *
                 * `env` is only half the gain: what you hear is `env * level`,
                 * and the level changes at this very boundary. `att_from`
                 * carries `env` across, so after a half-filled pad the
                 * envelope resumed at the right ENV and instantly the wrong
                 * GAIN -- the click the att_from ramp was added to prevent,
                 * arriving through the other factor.
                 *
                 * `env` may land ABOVE 1 -- a loud pad followed by a quiet
                 * one -- and that is not a special case: the attack is a lerp
                 * from att_from to 1, so it ramps DOWN to the new level over
                 * the attack time instead of up.
                 */
                let gain_now = self.env.level as f64 * self.step_level as f64;
                self.step_level = lvl;
                self.env.level = if lvl > 1.0e-6 {
                    (gain_now / lvl as f64) as f32
                } else {
                    0.0
                };
                self.env.enter(Stage::Attack, l);
            }
        } else if on_prev || self.env.stage != Stage::Idle {
            self.env.enter(Stage::Release, l);
        }
    }

    /// The gain for one sample, advancing every bit of state that depends on
    /// it.
    #[inline]
    fn next_gain(&mut self, r: &mut Run) -> f32 {
        if r.opening {
            self.amount_s = ni_dsp::smooth::glide(self.amount_s, 0.0, r.smooth);
            return 1.0 - self.amount_s * (1.0 - self.env.level * self.step_level);
        }

        /*
         * SUSTAIN GLIDES ONLY WHERE IT IS HEARD. It is a level in DECAY (the
         * target) and in SUSTAIN (the level itself); anywhere else the envelope
         * does not read it, and every way INTO those two stages starts from a
         * level that does not depend on it -- decay leaves 1.0 whatever it is
         * heading for. So outside them it takes the new value at once, and a
         * change can never be heard as a step.
         */
        let snd = *self.snd();
        self.sustain_s = if matches!(self.env.stage, Stage::Decay | Stage::Sustain) {
            ni_dsp::smooth::glide(self.sustain_s, snd.sustain, r.smooth)
        } else {
            snd.sustain
        };
        let l = StageLens { sustain: self.sustain_s, ..r.lens };

        if Some(r.step) != self.last_step {
            let prev = self.last_step;
            self.on_step_boundary(prev, r.step, &l);
            self.last_step = Some(r.step);
        }

        /*
         * Gate length: release inside the step, not only at its edge. A tie
         * means "hold through", so shortening it would contradict the tie --
         * and JOIN NEIGHBORS is "every adjacent ON pair is tied", so it has
         * to hold through for exactly the same reason.
         *
         * Leaving legato out of this rule made it wrong in both directions at
         * once. Below Width 100% the envelope released inside every step,
         * reached Idle, and then found no attack waiting at the boundary
         * because `on_step_boundary` had suppressed it. Idle is ABSORBING --
         * this block's own guard excludes it -- so the gate shut after step 0
         * and the pattern was SILENT from there on. At Width 100% this block
         * never runs at all, and at Sustain 100% a retrigger ramps from 1.0
         * to 1.0, so there the switch did nothing audible. Between them that
         * covered nearly every setting anyone would reach for, which is why
         * it read as broken.
         *
         * The SUCCESSOR is consulted, not the current step's tie bit, because
         * that is what "adjacent ON pair" means. A next step that is OFF
         * still closes the gate, which is what keeps this distinct from a
         * tie.
         */
        if snd.hold < 1.0
            && self.env.stage != Stage::Release
            && self.env.stage != Stage::Idle
        {
            let next = if r.length > 0 { (r.step + 1) % r.length } else { r.step };
            /* The FADED mask on both reads. A step the fade has not reached is
             * a gap, and Join Neighbors must not bridge to a gap -- see
             * `sounds`. */
            let (here, there) = (self.sounds(r.step), self.sounds(next));
            let p = &self.pat[self.slot];
            let held = here && (p.tied(r.step) || (snd.legato && there));
            if r.frac >= snd.hold as f64 && !held {
                self.env.enter(Stage::Release, &l);
            }
        }

        self.env.advance(snd.curve, &l);

        /* The step's amount is how far the gate OPENS, not how far it closes:
         *     m = 1 - amount * (1 - env * level)
         * level 0 is silent, an OFF step is a gap whatever its level (env is
         * 0 there, so the term vanishes), and the global amount is the
         * dry/wet.
         *
         * `step_level` is the STRUCK step's, latched at gate-open -- not
         * `depth[r.step]`, which is a different number the moment a release
         * or a tie outlives the step that started it.
         *
         * `amount_s` is Amount as it glides -- see `ni_dsp::smooth`. */
        self.amount_s = ni_dsp::smooth::glide(self.amount_s, snd.amount, r.smooth);
        let m = 1.0 - self.amount_s * (1.0 - self.env.level * self.step_level);

        self.phase.pos += r.inc;
        r.frac += r.inc;
        while r.frac >= 1.0 {
            r.frac -= 1.0;
            r.step += 1;
            if r.step >= r.length {
                r.step = 0;
            }
        }
        while r.frac < 0.0 {
            r.frac += 1.0;
            if r.step == 0 {
                r.step = r.length - 1;
            } else {
                r.step -= 1;
            }
        }
        m
    }

    /*
     * ONE SET OF MATHS, THREE BUFFER FORMATS.
     *
     * Move hands us int16 interleaved; VST3 and AU hand us float, usually as
     * separate channel pointers. Writing the loop three times would mean
     * three places for the gain law to drift, and the drift would be
     * inaudible until somebody A/B'd the plugin against the hardware.
     */
    pub fn process_i16(&mut self, lr: &mut [i16], frames: usize, t: Option<&Transport>) {
        /* Never past the buffer: an index out of range is a panic, and a panic
         * here is an abort of the host. */
        let frames = frames.min(lr.len() / 2);
        let Some(mut r) = self.block_setup(frames, t) else { return };
        for i in 0..frames {
            let m = self.next_gain(&mut r);
            let l = lr[i * 2] as f32 * m;
            let rr = lr[i * 2 + 1] as f32 * m;
            /*
             * ROUND, THEN CLAMP -- `as i16` alone truncates towards zero, so
             * every gated sample lost up to a whole LSB and always towards
             * silence: a bias that scales with the gain being applied, which is
             * the quantity this plugin modulates. sc-core measured it at 1.47
             * LSB against its float path. Rounded, the Move's render IS the
             * plugin's float render, rounded -- which is what the render A/B
             * compares. The clamp comes after so a value rounding up to 32768
             * is caught rather than wrapped.
             */
            lr[i * 2] = l.round().clamp(-32768.0, 32767.0) as i16;
            lr[i * 2 + 1] = rr.round().clamp(-32768.0, 32767.0) as i16;
        }
    }

    pub fn process_f32(&mut self, lr: &mut [f32], frames: usize, t: Option<&Transport>) {
        let frames = frames.min(lr.len() / 2);
        let Some(mut r) = self.block_setup(frames, t) else { return };
        for i in 0..frames {
            let m = self.next_gain(&mut r);
            lr[i * 2] *= m;
            lr[i * 2 + 1] *= m;
        }
    }

    /// No clamping on the float paths, deliberately: the gate only ever
    /// ATTENUATES (m is in 0..1), so it cannot push a signal out of range,
    /// and a plugin host is entitled to headroom above 1.0 we must not steal.
    pub fn process_f32_split(
        &mut self,
        l: &mut [f32],
        rch: &mut [f32],
        frames: usize,
        t: Option<&Transport>,
    ) {
        let frames = frames.min(l.len()).min(rch.len());
        let Some(mut r) = self.block_setup(frames, t) else { return };
        for i in 0..frames {
            let m = self.next_gain(&mut r);
            l[i] *= m;
            rch[i] *= m;
        }
    }
}
