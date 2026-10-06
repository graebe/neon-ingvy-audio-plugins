// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
Trance Gate -- a tempo-locked step gate with a per-step ADSR.

Modelled on the Kilohearts Trance Gate: 8 pattern slots, a pattern of up to
128 steps with ties, a Rate (the length of one step), an ADSR applied at each
step, and an Amount.

# Threading

Every entry point runs on an audio callback. On Move that is SCHED_FIFO 70,
pinned to core 3, with ~2370us of slack per 128-frame block; in a plugin it is
whatever the host provides. There is no control thread.

So: **no allocation outside [`Instance::new`], no I/O, no locks, no logging**.
The workspace sets `panic = "abort"` because unwinding out of `extern "C"`
into a C host is undefined behaviour, and a crash the OS reports is better
than one that corrupts the host's stack on the way out.

The three READERS are the exception, and they are kept apart for it:
[`state::Patch::parse`], [`slotfile::SlotFile::parse`] and
[`paste::Clip::parse`] read a text with serde_json, which allocates for a text
no build wrote. What they return is applied by [`Instance::load`],
[`Instance::apply_file`] and [`Instance::apply_clip`], which allocate nothing.
The state module says which thread reads what.
*/

pub mod envelope;
pub use ni_dsp::fmt;
pub mod mask;
pub mod params;
pub mod paste;
pub mod rates;
pub mod slotfile;
pub mod state;

mod clock;
mod json;
mod pattern;
mod process;
mod sound;

pub use pattern::Pattern;

use envelope::{Env, StageLens};
use sound::Sound;
use ni_dsp::phase::PhaseTracker;

pub const MAX_STEPS: usize = 128;
pub const SLOTS: usize = 8;
pub const DEPTH_FULL: u8 = 255;

/// A stage runs to twice the gate's width and no further -- past that it
/// cannot finish under any Width, so the extra range would be knob travel
/// with nothing on the end of it.
pub const STAGE_MAX_PCT: f32 = 200.0;

/// What the envelope's three time values MEAN to a shell showing them. The
/// engine does not consult it: ms and % are two readings of one number.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum TimeMode {
    Ms = 0,
    Pct = 1,
}

/*
 * WHICH WAY THE FADE BUILDS THE PATTERN UP.
 *
 * The knob means the same thing in both: HOW MUCH OF THE DRAWN PATTERN IS
 * PRESENT, 0..1. Only the missing part differs -- In leaves silence where a step
 * has not arrived, Out leaves the gate open. So 100% is the pattern either way,
 * which is what keeps it the neutral default and lets the direction be switched
 * at rest without changing a sample.
 *
 * OUT FILLS HOLES; IT DOES NOT BYPASS THE GATE. At Out 0% every step sounds, and
 * below Width 100% the gate still pulses -- a denser gate, not an open one.
 * Amount is what bypasses.
 */
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum FadeDir {
    /// The steps you drew ON arrive, one at a time. 0% is silence.
    In = 0,
    /// The steps you drew OFF -- the holes -- arrive. 0% is every hole filled.
    Out = 1,
}

impl FadeDir {
    pub fn from_i32(v: i32) -> FadeDir {
        if v >= 1 { FadeDir::Out } else { FadeDir::In }
    }
}

pub use ni_dsp::Transport;

/// The playhead and the clock as the last block left them: what a state blob
/// does not carry. A shell that mirrors the engine off the audio thread copies
/// this across, so the mirror's readouts draw the same picture as the engine's.
/// See [`Instance::mirror`].
#[derive(Clone, Copy, Default, Debug, PartialEq)]
pub struct Playhead {
    pub step_pos: f64,
    pub advancing: bool,
    pub last_bpm: f32,
    pub ms_per_step: f32,
    pub sample_rate: f64,
    pub cursor: usize,
    pub meter: rates::Meter,
}

pub struct Instance {
    /*
     * EVERY FIELD IS PRIVATE, and the slots are a fixed array. An index a
     * caller could write directly -- a slot of 9, a rate of 40 -- was one
     * panic away from an abort of the host (the workspace builds with
     * panic = "abort"), and nothing on that path could validate it. The doors
     * are `set_param` / `set_num`, which clamp; the readouts are `get_param`
     * and the accessors below.
     */
    pat: [Pattern; SLOTS],
    /// Every slot's sound: all the parameters but the slot. See [`sound`].
    snd: [Sound; SLOTS],
    slot: usize,

    /// The current slot's `amount` and `sustain` as the gain law hears them:
    /// gliding towards its values -- see `ni_dsp::smooth`. Runtime, not saved.
    /// One pair for the instance, which is what makes a slot switch glide.
    amount_s: f32,
    sustain_s: f32,
    /// Edit position on the ring, 0..length-1.
    cursor: usize,

    // ---- runtime, not saved ----
    /// The playhead, in steps, and whether the transport ran last block.
    phase: PhaseTracker,
    /// Step index at the previous sample; `None` = none.
    last_step: Option<usize>,
    env: Env,
    /*
     * THE STRUCK STEP'S LEVEL, HELD FOR THE WHOLE GATE.
     *
     * Read fresh every sample as `depth[current_step]`, this is wrong in the
     * two places where a gate and a step are not the same span: a RELEASE
     * outliving its step was scaled by the NEXT step's amount, and a TIE
     * stepped the level mid-gate -- a discontinuity in the gain, which is a
     * click. Latched when the envelope enters ATTACK and held until IDLE.
     */
    step_level: f32,

    /// Published for the UI, computed once per block, because `get_param`
    /// runs on the audio callback too and must stay trivial.
    ms_per_step: f32,
    last_bpm: f32,
    /// "The playhead is moving" -- the UI's extrapolator is the only reader
    /// and is written against the concept, not against what drives it.
    advancing: bool,
    sample_rate: f64,
    /// The host's time signature, as last told: what the `params` readout's
    /// detents are counted in. Runtime, not saved -- it is the host's.
    meter: rates::Meter,

    /*
     * THE FADE'S WEIGHT PER STEP, CACHED.
     *
     * `next_gain` consults this twice a sample and a step's RANK costs a pass
     * over the pattern to find, so deriving it in the sample loop would put an
     * O(length) search inside an O(frames) one. Recomputed by `recalc_fade`
     * whenever anything it depends on moves -- the same arrangement
     * `ms_per_step` has, and for the same reason.
     */
    fade_w: [f32; MAX_STEPS],
    /*
     * The generator's state. A small xorshift rather than anything from a
     * library: `set_param` runs on the audio callback, where `rand()` is not
     * RT-safe in the strict sense, and the core has no dependencies to reach
     * for anyway.
     */
    rng: u32,
    /*
     * THE SAVED STATE'S REVISION: moves whenever anything the state blob
     * carries may have changed, and at no other time. See
     * [`Instance::state_rev`]. Runtime, not saved.
     */
    rev: u64,
}

impl Instance {
    pub fn new(sample_rate: f64) -> Self {
        let mut me = Self {
            pat: core::array::from_fn(Pattern::new),
            snd: [Sound::DEFAULT; SLOTS],
            slot: 0,
            /* 0, NOT `amount`: a fresh instance is a stopped one, and stopped
             * is an open gate. The first start glides in from here. */
            amount_s: 0.0,
            sustain_s: 1.0,
            cursor: 0,
            phase: PhaseTracker::default(),
            last_step: None,
            env: Env::default(),
            step_level: 0.0,
            ms_per_step: 0.0,
            last_bpm: 120.0,
            advancing: false,
            sample_rate: if sample_rate > 0.0 { sample_rate } else { 44100.0 },
            meter: rates::Meter::COMMON,
            fade_w: [1.0; MAX_STEPS],
            /* A FIXED SEED, ADVANCED PER CALL. There is no entropy source in
             * here -- no clock, no I/O, by design -- so successive presses
             * differing is what the walk provides and reproducibility is what
             * the fixed start provides. A shell that wants a specific roll
             * passes its own seed to `randomize`. */
            rng: 0x9E37_79B9,
            rev: 0,
        };
        me.recalc_ms_per_step();
        me.recalc_fade();
        me
    }
}
impl Instance {
    /// The sample rate the engine renders at.
    pub fn sample_rate(&self) -> f64 {
        self.sample_rate
    }

    /// A non-positive or non-finite rate is refused rather than stored: every
    /// length the engine measures in samples divides by it.
    pub fn set_sample_rate(&mut self, sample_rate: f64) {
        if !(sample_rate > 0.0) || !sample_rate.is_finite() {
            return;
        }
        self.sample_rate = sample_rate;
        /* ms_per_step cancels the sample rate out, so this changes nothing
         * today. It is here so that "ms_per_step is current" holds at every
         * door into the struct rather than at the two that happen to matter. */
        self.recalc_ms_per_step();
    }

    /// The host's time signature, numerator over denominator. One the host
    /// could not mean -- or 0/0 for "it did not say" -- is 4/4. Changes no
    /// sample: only the Length detents in the `params` readout are counted in
    /// it.
    pub fn set_meter(&mut self, num: i32, den: i32) {
        self.meter = rates::Meter::new(num, den);
    }

    /// The time signature the detents are counted in.
    pub fn meter(&self) -> rates::Meter {
        self.meter
    }

    /*
     * WHETHER THE STATE BLOB IS STILL THE ONE A SHELL LAST FORMATTED.
     *
     * The blob is the whole patch -- up to ~5 KB of hex for eight full slots --
     * and a plugin shell republishes its readouts about a hundred times a
     * second, on the audio thread. Formatting it every time cost ~40 us a
     * publish for a patch that is between two edits for most of a session. A
     * shell compares this with the revision its copy was made at and formats
     * only when they differ.
     *
     * It moves on every `set_param` (the editor's and the Move's door, a
     * handful of calls a second at most) and on a `set_num` that actually
     * changed a saved value -- not on the fifteen a plugin pushes every block
     * unchanged. Moving when nothing changed costs one reformat; failing to move
     * when something did would publish a stale patch, so every doer errs to
     * the first.
     */
    pub fn state_rev(&self) -> u64 {
        self.rev
    }

    /// The slot being played and edited, 0..SLOTS.
    pub fn slot(&self) -> usize {
        self.slot
    }

    /// The edit position, 0..length.
    pub fn cursor(&self) -> usize {
        self.cursor
    }

    /// The playhead and clock, for a mirror. See [`Playhead`].
    pub fn playhead(&self) -> Playhead {
        Playhead {
            step_pos: self.phase.pos,
            advancing: self.advancing,
            last_bpm: self.last_bpm,
            ms_per_step: self.ms_per_step,
            sample_rate: self.sample_rate,
            cursor: self.cursor,
            meter: self.meter,
        }
    }

    /*
     * BECOME A COPY OF AN ENGINE whose saved state is `state` and whose last
     * block left `p` -- for a shell's view, which replays pending edits off the
     * audio thread and has to answer with the readouts the engine will give.
     *
     * THE ORDER IS THE POINT. The clock goes first, because loading a legacy
     * blob converts its milliseconds against the width at the tempo known at
     * that moment; then the blob; then what the blob does not carry. The
     * block's `ms_per_step` is copied rather than recomputed, because a
     * running transport measured it and the formula at rest would not agree.
     */
    pub fn mirror(&mut self, state: &str, p: &Playhead) {
        self.set_sample_rate(p.sample_rate);
        self.last_bpm = p.last_bpm;
        self.set_param("state", state);
        self.ms_per_step = p.ms_per_step;
        self.phase.pos = p.step_pos;
        self.advancing = p.advancing;
        self.meter = p.meter;
        self.cursor = p.cursor.min(self.pattern().length.max(1) - 1);
    }

    /// The pattern in slot `slot`, or `None` past the last one.
    pub fn pattern_in(&self, slot: usize) -> Option<&Pattern> {
        self.pat.get(slot)
    }

    #[inline]
    pub fn pattern(&self) -> &Pattern {
        &self.pat[self.slot]
    }

    /// One step's duration in ms, at the rate and tempo currently known.
    /// Called whenever either changes, so the `ui` readout never reports a
    /// stale one -- a value that only appeared after the first block meant a
    /// blank panel on open.
    pub fn recalc_ms_per_step(&mut self) {
        let bpm = if self.last_bpm > 1.0 { self.last_bpm as f64 } else { 120.0 };
        let sr = if self.sample_rate > 0.0 { self.sample_rate } else { 44100.0 };
        let mut samples = (60.0 / bpm) * sr * rates::RATES[self.snd().rate_idx].beats;
        if samples < 1.0 {
            samples = 1.0;
        }
        self.ms_per_step = (samples * 1000.0 / sr) as f32;
    }

    /// One cycle of the pattern in ms: the step length times the steps in the
    /// current slot.
    pub fn cycle_ms(&self) -> f64 {
        self.ms_per_step as f64 * self.pattern().length.max(1) as f64
    }

    /// How long the gate is open for, in ms: Width of a step. The unit every
    /// envelope stage is measured in.
    #[inline]
    pub fn width_ms(&self) -> f64 {
        self.snd().hold as f64 * self.ms_per_step as f64
    }

    /// How long a stage lasts, in samples. `value` is a percentage of the
    /// gate's WIDTH, 0..200.
    #[inline]
    fn stage_samples(&self, value: f32) -> f64 {
        value as f64 * 0.01 * self.width_ms() * (self.sample_rate / 1000.0)
    }

    #[inline]
    fn lens(&self) -> StageLens {
        let s = *self.snd();
        StageLens {
            attack: self.stage_samples(s.attack),
            decay: self.stage_samples(s.decay),
            release: self.stage_samples(s.release),
            sustain: s.sustain,
        }
    }

    /// The playhead's position in the pattern, 0..1. Cheap and
    /// allocation-free -- for a caller on the audio thread, where the
    /// equivalent formatted readout's `snprintf` does not belong.
    pub fn phase01(&self) -> f64 {
        let length = self.pattern().length.max(1) as f64;
        let mut pos = self.phase.pos % length;
        if pos < 0.0 {
            pos += length;
        }
        pos / length
    }
}

#[cfg(test)]
mod tests;
