/*!
A slot's SOUND: every parameter except the slot itself, beside the pattern the
slot already kept.

A slot used to be a pattern -- steps, ties, length, levels and arrival order --
played through one instance-wide set of Rate, envelope, Width, Amount and Fade.
Now each slot carries its own copy of those, so switching slots recalls a whole
gate rather than a drawing. Slot is the one value that is not per slot: it is
the choice between them.

# What a switch does to the sound

A switch is every value moving at once, and each one moves exactly as it does
when a host automates it -- there is no second rule:

- **Amount and Sustain glide** from what the gain law is hearing towards the new
  slot's values (`amount_s`, `sustain_s`), so the change is a ramp, never a step.
- **The envelope carries on from its level.** A stage in flight keeps the length
  it was entered with -- a stage's length is latched at entry, as it always was
  for an automated Attack -- and the next stage it enters is the new slot's.
  Width is read every sample, so a narrower gate releases (a ramp) at once.
- **Curve re-anchors**: the position along the running stage is re-solved so the
  level is unchanged and only the trajectory from here differs.
- **Rate re-locks to the host.** The playhead is the host's beat position in
  steps, so a new step length is a new position in the same musical time -- the
  tracker resyncs and the step there is re-evaluated, attacking or releasing from
  wherever the gain is. Musical phase is continuous; that is what keeps the gate
  bar-aligned through a switch.
- **The fade's weights** are recomputed for the new slot's pattern and Fade, as
  they are for a new pattern. A step's level is latched when its gate opens, so
  no gate changes level mid-way.

All of it is a few stores and the two recomputations a parameter change already
pays for: allocation-free, on the audio thread.
*/

use crate::envelope::Curve;
use crate::fmt::{self, Buf};
use crate::params::Param;
use crate::{rates, FadeDir, Instance, TimeMode, STAGE_MAX_PCT};
use core::fmt::Write;

#[derive(Clone, Copy, PartialEq, Debug)]
pub(crate) struct Sound {
    pub(crate) rate_idx: usize,
    /*
     * ATTACK, DECAY AND RELEASE ARE PERCENTAGES OF THE GATE'S WIDTH, 0..200.
     *
     * Not milliseconds, despite the wire keys, which are kept because they
     * appear in every saved patch. 100% is "exactly fills the gate"; 200% is
     * "twice the gate", a stage that never finishes before the gate shuts.
     * Measured against WIDTH because the gate's open time is the musical unit
     * here. See [`Instance::stage_samples`].
     */
    pub(crate) attack: f32,
    pub(crate) decay: f32,
    /// 0..1 -- a LEVEL, not a duration.
    pub(crate) sustain: f32,
    pub(crate) release: f32,
    /*
     * WIDTH: how much of a step the gate stays open, 0..1. Sustain is a level
     * and has no length; this is a sequencer's gate length -- release begins
     * this far into the step rather than at its end.
     */
    pub(crate) hold: f32,
    /// How much the gate acts, 0..1. 1 == a closed gate is silent, 0 == the
    /// effect is bypassed.
    pub(crate) amount: f32,
    /// Adjacent ON steps hold as ONE gate instead of re-articulating.
    pub(crate) legato: bool,
    pub(crate) time_mode: TimeMode,
    pub(crate) curve: Curve,
    /*
     * THE FADE: HOW MUCH OF THE PATTERN HAS ARRIVED, 0..1. 1.0 is the neutral
     * value and the default, so every patch saved before it existed sounds as
     * it did.
     */
    pub(crate) fade: f32,
    /// Whether a step ARRIVES (soft, ramped) or APPEARS (hard).
    pub(crate) fade_soft: bool,
    /// Which end the pattern is built up from. See [`FadeDir`].
    pub(crate) fade_dir: FadeDir,
}

#[inline]
fn clampf(x: f32, lo: f32, hi: f32) -> f32 {
    if x < lo { lo } else if x > hi { hi } else { x }
}

impl Sound {
    /*
     * The percentages that reproduce the old 2 / 20 / 20 ms defaults against a
     * full-width 1/16 step at 120 BPM, so a fresh instance sounds as it always
     * did; the fade whole, which is every version before it existed.
     */
    pub(crate) const DEFAULT: Sound = Sound {
        rate_idx: rates::RATE_DEFAULT,
        attack: 1.6,
        decay: 16.0,
        sustain: 1.0,
        release: 16.0,
        hold: 1.0,
        amount: 1.0,
        legato: false,
        time_mode: TimeMode::Ms,
        curve: Curve::Linear,
        fade: 1.0,
        fade_soft: false,
        fade_dir: FadeDir::In,
    };

    /// Exactly what the blob would say about it: compared by bits, so a
    /// change the save can see is never missed (`-0.0 == 0.0` would be).
    pub(crate) fn bits(&self) -> [u32; 13] {
        [
            self.rate_idx as u32,
            self.attack.to_bits(),
            self.decay.to_bits(),
            self.sustain.to_bits(),
            self.release.to_bits(),
            self.hold.to_bits(),
            self.amount.to_bits(),
            self.fade.to_bits(),
            self.fade_soft as u32,
            self.fade_dir as u32,
            self.legato as u32,
            self.time_mode as u32,
            self.curve as u32,
        ]
    }

    /*
     * ONE SLOT'S SOUND AS A BLOB FIELD:
     *
     *   rate:attack:decay:sustain:release:hold:amount:fade:fsoft:fdir:legato:tmode:curve
     *
     * The same thirteen values, units and precisions the top-level keys have
     * always been written with -- so a slot written here and the same sound
     * written at the top level load to the same numbers.
     */
    pub(crate) fn write(&self, b: &mut dyn Write) -> core::fmt::Result {
        write!(b, "{}:", rates::RATES[self.rate_idx].label)?;
        for (v, d) in [
            (self.attack, 2),
            (self.decay, 2),
            (self.sustain, 3),
            (self.release, 2),
            (self.hold, 3),
            (self.amount, 3),
            (self.fade, 4),
        ] {
            fmt::f(b, v as f64, d)?;
            b.write_char(':')?;
        }
        write!(
            b,
            "{}:{}:{}:{}:{}",
            self.fade_soft as i32,
            self.fade_dir as i32,
            self.legato as i32,
            self.time_mode as i32,
            self.curve as i32
        )
    }

    /*
     * THE FIELD BACK, OR NOTHING. Thirteen parts or the field is not one this
     * build wrote, and the slot keeps the blob's top-level sound rather than a
     * half-read one. Every number is clamped as the numeric door clamps it.
     */
    pub(crate) fn parse(text: &str) -> Option<Sound> {
        let mut f = [""; 13];
        let mut n = 0;
        for part in text.split(':') {
            if n == f.len() {
                return None;
            }
            f[n] = part;
            n += 1;
        }
        if n != f.len() {
            return None;
        }
        let num = |i: usize| fmt::atof(f[i]) as f32;
        let on = |i: usize| fmt::atof(f[i]) >= 0.5;
        Some(Sound {
            rate_idx: rates::index_from(f[0]),
            attack: clampf(num(1), 0.0, STAGE_MAX_PCT),
            decay: clampf(num(2), 0.0, STAGE_MAX_PCT),
            sustain: clampf(num(3), 0.0, 1.0),
            release: clampf(num(4), 0.0, STAGE_MAX_PCT),
            hold: clampf(num(5), 0.0, 1.0),
            amount: clampf(num(6), 0.0, 1.0),
            fade: clampf(num(7), 0.0, 1.0),
            fade_soft: on(8),
            fade_dir: if on(9) { FadeDir::Out } else { FadeDir::In },
            legato: on(10),
            time_mode: if on(11) { TimeMode::Pct } else { TimeMode::Ms },
            curve: Curve::from_i32((fmt::atof(f[12]) + 0.5) as i32),
        })
    }

    /// [`Sound::write`] into a stack buffer, for comparing two slots as the
    /// blob would write them. 160 bytes holds the longest field with room.
    pub(crate) fn text<'a>(&self, buf: &'a mut [u8; 160]) -> &'a str {
        let mut b = Buf::new(&mut buf[..]);
        let _ = self.write(&mut b);
        let n = b.len;
        core::str::from_utf8(&buf[..n]).unwrap_or("")
    }
}

impl Instance {
    /// The current slot's sound.
    #[inline]
    pub(crate) fn snd(&self) -> &Sound {
        &self.snd[self.slot]
    }

    #[inline]
    pub(crate) fn snd_mut(&mut self) -> &mut Sound {
        &mut self.snd[self.slot]
    }

    /*
     * THE FIFTEEN VALUES ON THE NUMERIC WIRE, for the current slot -- what
     * `set_num` would have to be handed to arrive here. Slot and Length are
     * option indices, Rate an index, the switches 0|1, the rest the units the
     * string keys use.
     *
     * A shell mirroring host parameters reads this after a switch: it is the
     * new slot's values in the form the host parameters were pushed in.
     */
    pub fn numbers(&self) -> [f64; 15] {
        let s = self.snd();
        let mut v = [0.0; 15];
        v[Param::Slot as usize] = self.slot as f64;
        v[Param::Length as usize] = (self.pattern().length - 1) as f64;
        v[Param::Rate as usize] = s.rate_idx as f64;
        v[Param::Legato as usize] = s.legato as i32 as f64;
        v[Param::TimeMode as usize] = s.time_mode as i32 as f64;
        v[Param::Curve as usize] = s.curve as i32 as f64;
        v[Param::Amount as usize] = s.amount as f64;
        v[Param::Hold as usize] = s.hold as f64;
        v[Param::Attack as usize] = s.attack as f64;
        v[Param::Decay as usize] = s.decay as f64;
        v[Param::Sustain as usize] = s.sustain as f64;
        v[Param::Release as usize] = s.release as f64;
        v[Param::Fade as usize] = s.fade as f64;
        v[Param::FadeSoft as usize] = s.fade_soft as i32 as f64;
        v[Param::FadeDir as usize] = s.fade_dir as i32 as f64;
        v
    }

    /*
     * THE CURRENT SLOT'S SOUND, IN EVERY SLOT -- what a patch from before slots
     * had sounds of their own means: one sound, played by all eight. A shell
     * loading such a patch calls this once the patch's values are in place.
     * The patterns are untouched.
     */
    pub fn spread_sound(&mut self) {
        let s = *self.snd();
        if self.snd.iter().any(|o| o.bits() != s.bits()) {
            self.snd = [s; crate::SLOTS];
            self.rev = self.rev.wrapping_add(1);
        }
    }

    /*
     * MAKE SLOT `to` THE LIVE ONE. See the module notes for why each value
     * needs nothing more than this: the glides and the latched stage carry the
     * gain across on their own.
     */
    pub(crate) fn switch_slot(&mut self, to: usize) {
        let from_curve = self.snd().curve;
        self.slot = to;
        /* The cursor is GLOBAL and the length is PER SLOT, so switching to a
         * shorter pattern can leave it past the end -- where every edit lands
         * on a step the ring never draws. */
        let len = self.pat[self.slot].length;
        if self.cursor >= len {
            self.cursor = len - 1;
        }
        self.reanchor(from_curve);
        /* The new slot's Rate is a new step duration, which the `ui` readout
         * and the stage lengths are measured against. */
        self.recalc_ms_per_step();
        /* A different slot is a different pattern and Fade, so a different
         * set of weights. */
        self.recalc_fade();
    }

    /*
     * RE-ANCHOR, OR A CURVE CHANGE IS A CLICK.
     *
     * `env.t` is a position along the STAGE, and the shape decides what level
     * that position means. Halfway through a stage is 0.50 linear and 0.82
     * exponential, so swapping the shape under a live gate would move the gain
     * instantly. Solving shape_new(t') = shape_old(t) keeps the LEVEL and
     * changes only the trajectory from here.
     */
    pub(crate) fn reanchor(&mut self, from: Curve) {
        let to = self.snd().curve;
        if to != from && self.env.t > 0.0 && self.env.t < 1.0 {
            let w = crate::envelope::shape(from, self.env.t);
            self.env.t = crate::envelope::shape_inv(to, w);
        }
    }
}

#[cfg(test)]
mod tests;
