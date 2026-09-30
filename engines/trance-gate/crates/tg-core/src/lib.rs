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
*/

pub mod envelope;
pub mod fmt;
pub mod mask;
pub mod params;
pub mod rates;
pub mod state;

mod clock;
mod pattern;
mod process;

pub use pattern::Pattern;

use envelope::{Curve, Env, StageLens};

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

/// What the host says about the transport.
#[derive(Clone, Copy, Default)]
pub struct Transport {
    pub running: bool,
    pub beats: f64,
    pub bpm: f32,
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
    slot: usize,
    rate_idx: usize,

    /*
     * ATTACK, DECAY AND RELEASE ARE PERCENTAGES OF THE GATE'S WIDTH, 0..200.
     *
     * Not milliseconds, despite the wire keys, which are kept because they
     * appear in every saved patch. 100% is "exactly fills the gate"; 200% is
     * "twice the gate", a stage that never finishes before the gate shuts.
     *
     * Measured against WIDTH and not against the step because the step is not
     * the musical unit here -- the gate's open time is. It also makes ms and
     * % two readings of ONE number: ms is `value/100 * width_ms`, so its
     * maximum moves with the rate and with Width while the percentage stays
     * put. See [`Instance::stage_samples`].
     */
    attack: f32,
    decay: f32,
    /// 0..1 -- a LEVEL, not a duration.
    sustain: f32,
    release: f32,

    /*
     * How much of a step the gate stays open, 0..1.
     *
     * SUSTAIN IS A LEVEL AND HAS NO LENGTH -- in an ADSR it holds until the
     * note ends, and here "the note" is the step. That is correct and it is
     * also not what someone reaching for a shorter gate wants. This is the
     * control they are reaching for: release begins this far into the step
     * rather than at its end, which is a sequencer's gate length.
     */
    hold: f32,
    /// How much the gate acts, 0..1. 1 == a closed gate is silent, 0 == the
    /// effect is bypassed.
    amount: f32,
    /// Edit position on the ring, 0..length-1.
    cursor: usize,

    // ---- runtime, not saved ----
    step_pos: f64,
    /// Step index at the previous sample; `None` = none.
    last_step: Option<usize>,
    was_running: bool,
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
    /// Adjacent ON steps hold as ONE gate instead of re-articulating.
    legato: bool,
    time_mode: TimeMode,
    curve: Curve,
    sample_rate: f64,

    /*
     * THE FADE-IN: HOW MUCH OF THE PATTERN HAS ARRIVED, 0..1.
     *
     * 0 is "no step sounds" and 1 is "all of them do", and the N on steps
     * arrive at equal intervals between the two -- step of rank r crosses at
     * exactly r/N. A build-up is this parameter automated, which is the whole
     * reason it is a host parameter and the pattern is not.
     *
     * 1.0 IS THE NEUTRAL VALUE and the default, so a fresh instance and every
     * patch saved before this existed sound exactly as they did. The golden
     * renders are what say so.
     */
    fade: f32,
    /*
     * Whether a step ARRIVES or APPEARS.
     *
     * Soft ramps the step in on its own level -- the same quantity a drag up
     * and down in a pad sets -- over its slice of the knob's travel. Hard
     * jumps it on at the end of that slice. They are one formula and a
     * threshold, so the two agree at every arrival boundary and the switch
     * reads as smoothing rather than as a second feature.
     */
    fade_soft: bool,
    /*
     * Which end the pattern is built up from. See [`FadeDir`]. In is the
     * default and the neutral one: it is what the gate did before the direction
     * existed, so every patch and both golden renders are unaffected.
     */
    fade_dir: FadeDir,
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
}

impl Instance {
    pub fn new(sample_rate: f64) -> Self {
        let mut me = Self {
            pat: core::array::from_fn(Pattern::new),
            slot: 0,
            rate_idx: rates::RATE_DEFAULT,
            /* The percentages that reproduce the old 2 / 20 / 20 ms defaults
             * against a full-width 1/16 step at 120 BPM, so a fresh instance
             * sounds as it always did. */
            attack: 1.6,
            decay: 16.0,
            sustain: 1.0,
            release: 16.0,
            hold: 1.0,
            amount: 1.0,
            cursor: 0,
            step_pos: 0.0,
            last_step: None,
            was_running: false,
            env: Env::default(),
            step_level: 0.0,
            ms_per_step: 0.0,
            last_bpm: 120.0,
            advancing: false,
            legato: false,
            time_mode: TimeMode::Ms,
            curve: Curve::Linear,
            sample_rate: if sample_rate > 0.0 { sample_rate } else { 44100.0 },
            /* The whole pattern, arriving as one -- which is the behaviour of
             * every version before the fade existed. */
            fade: 1.0,
            fade_soft: false,
            fade_dir: FadeDir::In,
            fade_w: [1.0; MAX_STEPS],
            /* A FIXED SEED, ADVANCED PER CALL. There is no entropy source in
             * here -- no clock, no I/O, by design -- so successive presses
             * differing is what the walk provides and reproducibility is what
             * the fixed start provides. A shell that wants a specific roll
             * passes its own seed to `randomize`. */
            rng: 0x9E37_79B9,
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

    /// The slot being played and edited, 0..SLOTS.
    pub fn slot(&self) -> usize {
        self.slot
    }

    /// The edit position, 0..length.
    pub fn cursor(&self) -> usize {
        self.cursor
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
        let mut samples = (60.0 / bpm) * sr * rates::RATES[self.rate_idx].beats;
        if samples < 1.0 {
            samples = 1.0;
        }
        self.ms_per_step = (samples * 1000.0 / sr) as f32;
    }

    /// How long the gate is open for, in ms: Width of a step. The unit every
    /// envelope stage is measured in.
    #[inline]
    pub fn width_ms(&self) -> f64 {
        self.hold as f64 * self.ms_per_step as f64
    }

    /// How long a stage lasts, in samples. `value` is a percentage of the
    /// gate's WIDTH, 0..200.
    #[inline]
    fn stage_samples(&self, value: f32) -> f64 {
        value as f64 * 0.01 * self.width_ms() * (self.sample_rate / 1000.0)
    }

    #[inline]
    fn lens(&self) -> StageLens {
        StageLens {
            attack: self.stage_samples(self.attack),
            decay: self.stage_samples(self.decay),
            release: self.stage_samples(self.release),
            sustain: self.sustain,
        }
    }

    /// The playhead's position in the pattern, 0..1. Cheap and
    /// allocation-free -- for a caller on the audio thread, where the
    /// equivalent formatted readout's `snprintf` does not belong.
    pub fn phase01(&self) -> f64 {
        let length = self.pattern().length.max(1) as f64;
        let mut pos = self.step_pos % length;
        if pos < 0.0 {
            pos += length;
        }
        pos / length
    }
}

#[cfg(test)]
mod tests;
