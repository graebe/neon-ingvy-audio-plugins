// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
NI Side-Chain: a sidechain ducker.

Three ways to ask "duck now", one shape that answers. The sources disagree
about WHEN and about nothing else:

- **Cycle** -- a tempo-locked division of the bar, phase-locked onto the host's
  transport. Needs no input and no routing, which is why it is the default.
- **MIDI** -- a note. See `midi.rs` for what Live will and will not route.
- **Sidechain** -- a real key input through an aux bus. See `follower.rs`.

WHAT THIS ENGINE DOES NOT KNOW ABOUT: buses, plugin formats, webviews, and
which host it is in. It is handed a block of audio, optionally a block of key
signal, optionally a transport, and a queue of MIDI events with sample offsets.
Everything host-shaped lives in `plugins/side-chain/SideChain.cpp` and
`crates/sc-move/src/lib.rs`.

THE TIME UNIT IS A PERCENTAGE OF THE CYCLE, ALWAYS, AND `Time Mode` IS A
DISPLAY CHOICE.

This is `tg-core`'s decision and it is quoted here because a reader coming from
a compressor will expect otherwise: "ms and % are two readings of one number".
The stage lengths are stored as percentages of the current cycle and rendered
in whichever unit the user asked to see.

It matters more here than it does for a gate. A parameter whose MEANING depended
on `Time Mode` would be a parameter whose automation lane changes what it does
when another control moves -- and the musical default for a ducker is the
relative one anyway: a shape proportional to the cycle keeps its proportions
when the tempo or the rate changes, which is what "in time with the music"
means. An absolute-milliseconds mode is deliberately not offered; the ms
READOUT is, computed from bpm and rate, so "a 5 ms attack" is still a thing you
can see and dial.
*/

pub mod follower;
pub use ni_dsp::fmt;
pub mod midi;
pub mod params;
pub mod rates;
pub mod shape;

#[cfg(test)]
mod tests;

use follower::Follower;
use ni_dsp::phase::{Edge, PhaseTracker};
use midi::{Action, Midi, Queue};
use shape::{Curve, Env, Stage, Stages};

/// The longest block the key buffer can hold.
///
/// A host handing over more than this is chunked BY THE SHELL, which already
/// chunks for its own dry-copy buffers (`SideChain.cpp`, and `TranceGate.cpp:284`
/// before it). The engine's contract is simply that a block longer than this
/// gets a key signal only for its first `MAX_BLOCK` frames, which is why the
/// shell must not rely on that behaviour.
pub const MAX_BLOCK: usize = 8192;

/// A stage runs to twice the cycle and no further. Past that it cannot finish
/// before the next trigger under any setting, so the extra range would be knob
/// travel with nothing on the end of it. `tg-core`'s `STAGE_MAX_PCT`, same
/// reasoning.
pub const STAGE_MAX_PCT: f64 = 200.0;

/// Delay runs a whole cycle EITHER WAY: -100..+100.
///
/// NEGATIVE IS AN EARLY SIDECHAIN, and it is a real thing to want -- ducking
/// slightly ahead of the beat is how a mix is made to breathe into the kick
/// rather than after it. Past a whole cycle in either direction the next
/// trigger has already fired and the control stops describing anything a
/// listener can hear.
///
/// HOW IT IS POSSIBLE AT ALL is worth stating, because anticipating an event is
/// not: the Cycle source is PERIODIC, so "20% early" is just "80% into the
/// previous cycle", which is a position we have already passed. MIDI and
/// Sidechain have no such luxury -- see `block_setup`.
pub const DELAY_RANGE_PCT: f64 = 100.0;

/// Where the trigger comes from.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
#[repr(i32)]
pub enum Source {
    Cycle = 0,
    Midi = 1,
    Sidechain = 2,
}

impl Source {
    pub fn from_i32(v: i32) -> Self {
        match v {
            1 => Source::Midi,
            2 => Source::Sidechain,
            _ => Source::Cycle,
        }
    }
    pub const LABELS: [&'static str; 3] = ["Cycle", "MIDI", "Sidechain"];
}

/// How the envelope's times are SHOWN. The engine does not consult it; it is
/// carried so one place owns the plugin's whole state.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
#[repr(i32)]
pub enum TimeMode {
    Ms = 0,
    Pct = 1,
}

impl TimeMode {
    pub fn from_i32(v: i32) -> Self {
        if v == 0 {
            TimeMode::Ms
        } else {
            TimeMode::Pct
        }
    }
    pub const LABELS: [&'static str; 2] = ["ms", "% of cycle"];
}

pub use ni_dsp::Transport;

pub struct Instance {
    sample_rate: f64,

    /* ---- parameters ---- */
    source: Source,
    rate_idx: usize,
    time_mode: TimeMode,
    /// All four as a percentage of the cycle. See the module header.
    delay_pct: f64,
    attack_pct: f64,
    hold_pct: f64,
    release_pct: f64,
    /// 0..1.
    depth: f64,
    /// `depth` as the gain law hears it, gliding towards it -- see
    /// `ni_dsp::smooth`. Runtime, not saved.
    depth_s: f64,
    curve: Curve,
    midi: Midi,
    /// Linear amplitude, converted from the user's dB once, in `params.rs`.
    threshold: f64,
    lockout_ms: f64,

    /* ---- state ---- */
    env: Env,
    /// The stage lengths the envelope ran under last block. A stage's
    /// position is a sample count, so when its length changes the position
    /// is rescaled to keep the FRACTION -- see `Env::rescale`.
    stages: Stages,
    follower: Follower,
    queue: Queue,
    /// The key signal for the block about to be rendered, and how much of it
    /// is real. Deinterleaved so the detector reads two contiguous runs.
    ///
    /// ON THE HEAP, allocated once in `new`: inline they were 64 KB of an
    /// instance every shell builds with `Box::new(Instance::new(..))` -- on the
    /// caller's stack first, and on the Move that caller is the audio thread.
    key_l: Box<[f32]>,
    key_r: Box<[f32]>,
    key_len: usize,

    /* ---- the cycle's phase-locked loop ---- */
    /// Position in cycles, and whether the transport ran last block.
    phase: PhaseTracker,
    /// The last whole cycle a trigger fired on. `None` forces the next
    /// boundary to fire even if the index has not changed -- which is what a
    /// seek back onto the cycle we were already on needs.
    last_cycle: Option<i64>,

    /* ---- published once per block, so `get_param` stays trivial ---- */
    /// `get_param` runs on the audio callback too. Anything it reports that
    /// costs arithmetic is computed here, once, rather than there, per read.
    ms_per_cycle: f32,
    /// The same length in samples, kept because the sweep divides by it per
    /// sample and recomputing it from `ms_per_cycle` would be a multiply and a
    /// divide in the inner loop to recover a number we already had.
    samples_per_cycle: f64,
    last_bpm: f32,
    advancing: bool,
    /// Monotonic count of triggers. The UI watches it CHANGE rather than
    /// timing anything itself -- that is how "nothing has fired for 500 ms"
    /// is answered without the engine owning a clock it has no use for.
    fires: u32,
    /// Samples since the last trigger, saturating at one cycle.
    ///
    /// THIS IS WHAT GIVES THE TWO WELLS ONE SHARED AXIS. The editor draws the
    /// shape across one cycle; the scope has to lay the audio out on that same
    /// axis or the dip in the waveform does not sit under the curve that made
    /// it, and then the picture is two pictures. See `sweep01`.
    since_trigger: f64,
    /// The attenuation as of the last sample rendered, for the meter.
    duck_now: f32,
    /// Whether the shell says an aux bus is actually patched. The engine does
    /// not infer it: an unconnected bus and a silent one are the same block of
    /// zeroes, and guessing between them is how a host bug becomes a mystery.
    key_connected: bool,
}

impl Instance {
    pub fn new(sample_rate: f64) -> Self {
        let sr = if sample_rate > 0.0 { sample_rate } else { 44100.0 };
        Instance {
            sample_rate: sr,
            source: Source::Cycle,
            rate_idx: rates::RATE_DEFAULT,
            time_mode: TimeMode::Ms,
            delay_pct: 0.0,
            attack_pct: 2.0,
            hold_pct: 8.0,
            release_pct: 35.0,
            depth: 1.0,
            depth_s: 1.0,
            curve: Curve::Exp,
            midi: Midi::default(),
            /* -24 dBFS in linear amplitude. */
            threshold: 0.063_095_734_448_019_33,
            lockout_ms: 20.0,
            env: Env::default(),
            stages: Stages::default(),
            follower: Follower::new(sr),
            queue: Queue::default(),
            key_l: vec![0.0; MAX_BLOCK].into_boxed_slice(),
            key_r: vec![0.0; MAX_BLOCK].into_boxed_slice(),
            key_len: 0,
            last_cycle: None,
            phase: PhaseTracker::default(),
            ms_per_cycle: 0.0,
            samples_per_cycle: 1.0,
            last_bpm: 120.0,
            advancing: false,
            fires: 0,
            since_trigger: f64::INFINITY,
            duck_now: 0.0,
            key_connected: false,
        }
    }

    pub fn sample_rate(&self) -> f64 {
        self.sample_rate
    }

    #[allow(clippy::neg_cmp_op_on_partial_ord, reason = "a NaN rate must take the guard, and is refused with it")]
    pub fn set_sample_rate(&mut self, sample_rate: f64) {
        if !(sample_rate > 0.0) || sample_rate == self.sample_rate {
            return;
        }
        self.sample_rate = sample_rate;
        self.follower.set_sample_rate(sample_rate);
        /* A rate change invalidates every length the envelope measured in
         * samples, so the honest thing is to open and start again rather than
         * finish the current stage at the wrong speed. */
        self.env.reset();
        self.last_cycle = None;
        self.phase.was_running = false;
    }

    pub fn set_key_connected(&mut self, connected: bool) {
        if self.key_connected != connected {
            self.key_connected = connected;
            /* A bus that has just appeared or vanished must not leave the
             * detector latched above a threshold it can no longer see. */
            self.follower.reset();
        }
    }

    /// Queue a MIDI message at a sample offset within the next block.
    ///
    /// A NOTE IS A TRIGGER ONLY WHEN MIDI IS THE SOURCE. On Cycle or Sidechain
    /// the trigger note is music passing through the track, and ducking on it
    /// put a second, unsynchronised dip into a pattern locked to the bar. A
    /// PANIC is not a trigger and is honoured whatever the source: CC 120/123
    /// is the only reset a Schwung host can deliver.
    pub fn on_midi(&mut self, msg: &[u8], at: usize) {
        match self.midi.decode(msg) {
            Some(Action::Reset) => self.queue.push(at, Action::Reset),
            Some(action) if self.source == Source::Midi => self.queue.push(at, action),
            _ => {}
        }
    }

    /// Hand over the key signal for the next block. Called before `process`.
    pub fn push_key(&mut self, l: &[f32], r: &[f32], frames: usize) {
        let n = frames.min(l.len()).min(r.len()).min(MAX_BLOCK);
        self.key_l[..n].copy_from_slice(&l[..n]);
        self.key_r[..n].copy_from_slice(&r[..n]);
        self.key_len = n;
    }

    /// Open the gate now and forget the trigger. A panic, and what a stopped
    /// transport does to the Cycle source.
    pub fn reset(&mut self) {
        self.env.reset();
        self.follower.reset();
        self.queue.clear();
        self.midi.held = 0;
        self.duck_now = 0.0;
    }

    /* ------------------------------------------------------------------ */

    /// Per-block setup: resolve tempo, stage lengths and the cycle's phase.
    ///
    /// Unlike `tg-core`'s, this never returns "nothing to do" for a block that
    /// has samples in it. A gate with the
    /// transport stopped has no work; a ducker does -- two of its three
    /// sources have nothing to do with the transport, and a MIDI-triggered
    /// duck must still work with the timeline parked.
    ///
    /// `None` for an EMPTY block, and only then: no time passed, so nothing --
    /// the phase, the transport edge, the MIDI queue -- may move. Clamping the
    /// queue into zero frames and clearing it lost every note waiting in it.
    fn block_setup(&mut self, frames: usize, t: Option<&Transport>) -> Option<Run> {
        if frames == 0 {
            return None;
        }
        let mut bpm = self.last_bpm;
        if let Some(t) = t {
            if t.bpm > 1.0 && t.bpm < 1000.0 {
                bpm = t.bpm;
            }
        }
        self.last_bpm = bpm;

        let beats_per_cycle = rates::RATES[self.rate_idx].beats;
        let mut samples_per_cycle = (60.0 / bpm as f64) * self.sample_rate * beats_per_cycle;
        if samples_per_cycle < 1.0 {
            samples_per_cycle = 1.0;
        }
        self.ms_per_cycle = (samples_per_cycle * 1000.0 / self.sample_rate) as f32;
        self.samples_per_cycle = samples_per_cycle;

        /* The percentages become samples HERE, once per block, which is the
         * only place that knows both the cycle length and the unit. */
        let pct = |p: f64| samples_per_cycle * (p / 100.0);

        /*
         * DELAY IS TWO DIFFERENT MECHANISMS, AND THE SOURCE DECIDES WHICH.
         *
         * On CYCLE it is a phase offset on the trigger, not a wait: the cycle is
         * periodic, so firing at 80% of it is the same event as firing 20% before
         * the next beat. That is what makes a NEGATIVE delay possible at all, and
         * the envelope needs no delay stage because the offset is already in the
         * trigger instant. A positive delay could be done either way and lands on
         * exactly the same samples; doing both through the phase keeps one
         * mechanism rather than two.
         *
         * On MIDI and SIDECHAIN it is a wait after the trigger, because there is
         * nothing periodic to anticipate: a note that has not arrived cannot be
         * ducked ahead of. A negative delay there is CLAMPED TO ZERO rather than
         * refused -- the automation lane is allowed to sweep through it, and the
         * editor says which sources can use it.
         */
        let stages = Stages {
            delay: if matches!(self.source, Source::Cycle) {
                0.0
            } else {
                pct(self.delay_pct.max(0.0))
            },
            attack: pct(self.attack_pct),
            hold: pct(self.hold_pct),
            release: pct(self.release_pct),
        };
        /* A length that moved under a running stage -- a knob, automation, a
         * tempo change -- keeps the stage's progress and so its level. */
        self.env.rescale(&self.stages, &stages);
        self.stages = stages;

        let cycle = matches!(self.source, Source::Cycle);
        /* In cycles, and signed. See the note on the stages above. */
        let offset = self.delay_pct / 100.0;

        /* A stopped transport is not beat 0, it is no beat at all -- and
         * neither is a position the host could not give: NaN from JUCE's
         * shell (ni::readClock) when there is no musical position, or an
         * infinity, which `>= 0.0` alone would take as running. */
        let beats = match t {
            Some(t) if t.running && t.beats.is_finite() && t.beats >= 0.0 => t.beats,
            _ => -1.0,
        };
        let running = beats >= 0.0;
        self.advancing = running;

        /*
         * ARRIVING SOMEWHERE: what the trigger should do about it. `None`
         * fires on the next sample, which is right only when the transport
         * lands within a sample of a trigger point -- play on the downbeat must
         * duck. Anywhere else, record the boundary already passed so the next
         * one is heard, rather than a duck at an arbitrary phase and another a
         * moment later. A jump re-evaluates even onto the current cycle: the
         * test is `index > last_cycle`.
         */
        let arrive = |pos: f64, inc: f64| -> Option<i64> {
            let shifted = pos - offset;
            let floor = shifted.floor();
            if shifted - floor < inc {
                None
            } else {
                Some(floor as i64)
            }
        };

        let target = if running { Some(beats / beats_per_cycle) } else { None };
        let (inc, edge) = self.phase.follow(target, samples_per_cycle, frames, self.sample_rate);
        match edge {
            Edge::Started | Edge::Jumped => {
                self.last_cycle = arrive(self.phase.pos, 1.0 / samples_per_cycle);
            }
            Edge::Tracking => {}
            Edge::Stopped | Edge::Parked => {
                self.last_cycle = None;
                if cycle && edge == Edge::Stopped {
                    /* Stopped means open, but ONLY for the source that
                     * depends on the transport -- a MIDI duck must still work
                     * in a stopped session. And open by RELEASING, once, at
                     * the stop: what the duck would have done had the cycle
                     * simply not fired again. */
                    self.env.release(&stages);
                }
            }
        }

        self.queue.prepare(frames);

        Some(Run {
            smooth: ni_dsp::smooth::coef(self.sample_rate),
            stages,
            cycle,
            inc,
            offset,
            lockout: self.lockout_ms * self.sample_rate / 1000.0,
        })
    }

    /// One sample's gain. THE ONE GAIN LAW, whatever the buffer format.
    #[inline]
    fn next_gain(&mut self, r: &Run, i: usize) -> f32 {
        /* --- did anything ask us to duck on this sample? --- */
        while let Some(action) = self.queue.pop_at(i) {
            match action {
                Action::Trigger(scale) => {
                    self.env.trigger(scale, &r.stages);
                    self.fires = self.fires.wrapping_add(1);
                    self.since_trigger = 0.0;
                }
                Action::Release => self.env.release(&r.stages),
                Action::Reset => self.env.reset(),
            }
        }

        if r.cycle {
            if self.advancing {
                /*
                 * THE TRIGGER SITS AT `offset` INTO THE CYCLE, and shifting the
                 * position rather than the test is what lets the offset be
                 * negative: at -0.2 the boundary lands at 80% of the cycle,
                 * which is 20% before the next beat and a place we have already
                 * been.
                 */
                let idx = (self.phase.pos - r.offset).floor() as i64;
                /*
                 * FORWARD CROSSINGS ONLY.
                 *
                 * The phase advances, so a real crossing always increments. A
                 * DECREMENT means the offset moved under us -- somebody dragged
                 * Delay, or automation swept it -- and firing on that would put
                 * an extra duck in the middle of a bar for every pixel of the
                 * drag. `None` is the exception and fires: it is set by a seek
                 * or a transport start, where the next boundary must be heard
                 * even if the index has not changed.
                 */
                let fire = match self.last_cycle {
                    None => true,
                    Some(prev) => idx > prev,
                };
                self.last_cycle = Some(idx);
                if fire {
                    self.env.trigger(1.0, &r.stages);
                    self.fires = self.fires.wrapping_add(1);
                    self.since_trigger = 0.0;
                }
                self.phase.pos += r.inc;
            }
        } else if matches!(self.source, Source::Sidechain) {
            /* A key buffer shorter than the block reads as silence past its
             * end rather than as the last sample held -- holding would let one
             * transient re-trigger for the rest of the block. */
            let (kl, kr) = if i < self.key_len {
                (self.key_l[i], self.key_r[i])
            } else {
                (0.0, 0.0)
            };
            if self.follower.next(kl, kr, self.threshold, r.lockout) {
                self.env.trigger(1.0, &r.stages);
                self.fires = self.fires.wrapping_add(1);
                self.since_trigger = 0.0;
            }
        }

        /* Gate mode holds at the bottom while a note is down. The cycle and
         * the sidechain have nothing to hold, so they always time out. */
        let gated = matches!(self.source, Source::Midi) && self.midi.gate && self.midi.held > 0;
        let duck = self.env.next(self.curve, &r.stages, gated);
        self.duck_now = duck as f32;
        /* Saturating rather than wrapping: past one cycle the sweep is parked
         * at its right-hand edge, which is the honest picture for a source that
         * simply has not fired again. Wrapping would draw a second dip that
         * never happened. */
        if self.since_trigger < self.samples_per_cycle {
            self.since_trigger += 1.0;
        }

        /* DEPTH GLIDES ONLY WHILE IT IS HEARD. With no duck the gain is 1.0
         * whatever Depth is, so it takes a new value at once there -- which is
         * also what keeps a patch set before its first trigger rendering the
         * same bits it always did. Mid-duck it glides; see `ni_dsp::smooth`. */
        self.depth_s = if duck == 0.0 {
            self.depth
        } else {
            ni_dsp::smooth::glide(self.depth_s, self.depth, r.smooth)
        };

        /* DEPTH ZERO IS A TRUE BYPASS AND NEEDS NO SPECIAL CASE: the product
         * collapses to exactly 1.0, and multiplying by exactly 1.0 is the
         * identity in IEEE 754 for every input including the denormals and the
         * signed zeroes. `tg-core` spends a branch proving this; one multiply
         * is cheaper than the branch and leaves one code path. */
        (1.0 - self.depth_s * duck) as f32
    }

    /// Called at the end of every `process`, whatever the format.
    #[inline]
    fn block_done(&mut self) {
        /* The queue is per block. An event the walk never reached is an event
         * that never happened -- which is why `prepare` clamps. */
        self.queue.clear();
        self.key_len = 0;
    }

    /*
     * ONE SET OF MATHS, THREE BUFFER FORMATS.
     *
     * Move hands over int16 interleaved; VST3, AU and CLAP hand over float,
     * usually as separate channel pointers. Writing the loop three times would
     * mean three places for the gain law to drift, and the drift would be
     * inaudible until somebody A/B'd the plugin against the hardware -- which
     * is exactly what `sc_render_ab` does.
     */
    pub fn process_i16(&mut self, lr: &mut [i16], frames: usize, t: Option<&Transport>) {
        /* Never past the buffer: an index out of range is a panic, and a panic
         * here is an abort of the host. */
        let frames = frames.min(lr.len() / 2);
        let Some(r) = self.block_setup(frames, t) else { return };
        for i in 0..frames {
            let m = self.next_gain(&r, i);
            let l = lr[i * 2] as f32 * m;
            let rr = lr[i * 2 + 1] as f32 * m;
            /*
             * ROUND, THEN CLAMP -- and `as i16` alone does neither.
             *
             * A float-to-int cast in Rust TRUNCATES TOWARDS ZERO, so every
             * sample loses up to a full LSB and always in the same direction:
             * towards silence. That is not a rounding error, it is a bias, and
             * it is correlated with the signal because it scales with the gain
             * being applied -- which is exactly the quantity this plugin is
             * modulating. The render A/B measured 1.47 LSB between this path
             * and the float one because of it; with rounding the same
             * comparison is inside one.
             *
             * The clamp is asymmetric because i16 is: -32768 is representable
             * and +32768 is not. It comes after the round so that a value
             * rounding up to 32768 is caught rather than wrapped.
             */
            lr[i * 2] = l.round().clamp(-32768.0, 32767.0) as i16;
            lr[i * 2 + 1] = rr.round().clamp(-32768.0, 32767.0) as i16;
        }
        self.block_done();
    }

    pub fn process_f32(&mut self, lr: &mut [f32], frames: usize, t: Option<&Transport>) {
        let frames = frames.min(lr.len() / 2);
        let Some(r) = self.block_setup(frames, t) else { return };
        for i in 0..frames {
            let m = self.next_gain(&r, i);
            lr[i * 2] *= m;
            lr[i * 2 + 1] *= m;
        }
        self.block_done();
    }

    /// No clamping on the float paths, deliberately: a ducker only ever
    /// ATTENUATES -- the gain is in 0..1 -- so it cannot push a signal out of
    /// range, and a host is entitled to headroom above 1.0 we must not steal.
    pub fn process_f32_split(
        &mut self,
        l: &mut [f32],
        rch: &mut [f32],
        frames: usize,
        t: Option<&Transport>,
    ) {
        self.process_f32_split_tap(l, rch, None, None, frames, t)
    }

    /// The split path, with the applied gain written out per sample.
    ///
    /// WHY THE ENGINE HANDS THIS OUT RATHER THAN THE SHELL DERIVING IT. The
    /// editor draws what the ducker actually DID, beside what it was asked to
    /// do, and the two differ whenever a trigger interrupts a recovery -- which
    /// is most of the interesting cases. The shell has the dry and the wet, so
    /// it could divide one by the other; that answer is meaningless wherever the
    /// input is near silence, which is exactly where a duck is most visible.
    ///
    /// `gain` is the MULTIPLIER APPLIED, 0..1 -- not the attenuation and not the
    /// envelope. Depth is already in it, so the trace shows the effect the
    /// listener heard rather than the shape behind it.
    ///
    /// `sweep` is where each sample sits on the display axis, 0..1.
    ///
    /// THE SHELL CANNOT COMPUTE THIS ITSELF WITHOUT BECOMING A SECOND COPY OF
    /// THE PHASE LOGIC. It bins its capture columns by the sweep, so it needs a
    /// per-sample value; deriving one from the block's start and a local
    /// increment would mean re-implementing the phase-locked loop's correction,
    /// the saturation past one cycle, and the difference between the three
    /// sources -- and the first time any of those changed, the picture would
    /// stop matching the sound in a way that looks like a drawing bug.
    ///
    /// Either pointer may be `None`; the Move module passes neither.
    pub fn process_f32_split_tap(
        &mut self,
        l: &mut [f32],
        rch: &mut [f32],
        mut gain: Option<&mut [f32]>,
        mut sweep: Option<&mut [f32]>,
        frames: usize,
        t: Option<&Transport>,
    ) {
        let frames = frames.min(l.len()).min(rch.len());
        let Some(r) = self.block_setup(frames, t) else { return };
        for i in 0..frames {
            let m = self.next_gain(&r, i);
            l[i] *= m;
            rch[i] *= m;
            if let Some(g) = gain.as_deref_mut() {
                if i < g.len() {
                    g[i] = m;
                }
            }
            if let Some(sw) = sweep.as_deref_mut() {
                if i < sw.len() {
                    sw[i] = self.sweep01() as f32;
                }
            }
        }
        self.block_done();
    }

    /* ---- what the shells read back ---- */

    /// Cycle phase, 0..1. The allocation-free answer for the audio thread;
    /// the UI gets the same number inside the `ui` readout.
    pub fn phase01(&self) -> f64 {
        let p = self.phase.pos - self.phase.pos.floor();
        if p.is_finite() {
            p.clamp(0.0, 1.0)
        } else {
            0.0
        }
    }

    pub fn ms_per_cycle(&self) -> f32 {
        self.ms_per_cycle
    }

    /// Where we are across the DISPLAY WINDOW, 0..1 -- which is one cycle long.
    ///
    /// ONE DEFINITION FOR ALL THREE SOURCES, and that is the point of it being
    /// here rather than in the shell. For Cycle it is the transport's phase. For
    /// MIDI and Sidechain there is no transport phase at all, so it is the time
    /// since the last trigger over one cycle -- which for the Cycle source is
    /// the same number, and for the other two is the only meaningful answer.
    ///
    /// The scope indexes its columns by this, so the audio lands on the same
    /// axis the editor draws the shape on and a dip sits under the curve that
    /// made it.
    ///
    /// WHAT THAT COSTS, stated because it is a real trade: a column is written
    /// once per cycle, so the picture refreshes at the cycle rate rather than
    /// continuously. At 1/4 and 120 bpm that is twice a second, which is what
    /// every sidechain plugin does and reads as a live waveform. At 1/1 and
    /// 60 bpm it is once every four seconds, and the picture IS that old --
    /// the alternative is a rolling window that does not line up with the
    /// editor, which is a worse picture that merely looks fresher.
    #[allow(clippy::neg_cmp_op_on_partial_ord, reason = "a NaN cycle length must take the guard, not reach the division")]
    pub fn sweep01(&self) -> f64 {
        if matches!(self.source, Source::Cycle) {
            return self.phase01();
        }
        if !(self.samples_per_cycle > 0.0) || !self.since_trigger.is_finite() {
            return 1.0;
        }
        (self.since_trigger / self.samples_per_cycle).clamp(0.0, 1.0)
    }
    pub fn advancing(&self) -> bool {
        self.advancing
    }
    pub fn fires(&self) -> u32 {
        self.fires
    }
    pub fn duck_now(&self) -> f32 {
        self.duck_now
    }
    pub fn key_level(&self) -> f64 {
        self.follower.level()
    }
    pub fn key_connected(&self) -> bool {
        self.key_connected
    }
    pub fn dropped(&self) -> u32 {
        self.queue.dropped()
    }
    pub fn stage(&self) -> Stage {
        self.env.stage
    }
}

/// Per-block state the sample loop walks. Held by value and passed by
/// reference so the loop reads it without borrowing `self` twice -- `next_gain`
/// needs `&mut self` for the envelope, so the block's constants cannot live
/// behind the same borrow.
struct Run {
    /// The parameter glide's per-sample coefficient at this sample rate.
    smooth: f64,
    stages: Stages,
    cycle: bool,
    /// Cycles per sample, with the phase-locked loop's correction term for
    /// this block already folded in.
    inc: f64,
    /// Where in the cycle the trigger sits, in cycles, signed. Negative is
    /// early -- see the note where the stages are built.
    offset: f64,
    /// The retrigger lockout in samples.
    lockout: f64,
}
