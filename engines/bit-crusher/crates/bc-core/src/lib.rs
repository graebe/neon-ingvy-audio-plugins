// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
NI Bit-Crusher's engine: a continuous signal made discrete, in amplitude and
in time.

THE SIGNAL, one channel, one sample:

```text
  in ─ Drive ─ Pre-Filter ─┬─ steps ─ hold (on the host's grid) ── delay ─┐
                           └─ hold (exact, band-limited) ─ steps ─────────┴─ Anti-Alias ─ Post-Filter ─ DC ─┐
   └──────────────────────────────────── dry ─ delay ────────────────────────────────────────────── Mix ─┴─ Output ─ out
```

- Drive pushes the signal up the staircase; past full scale the top step is a
  hard clip.
- The pre-filter (stopband from Rate/2) takes out what the hold would fold
  back.
- The steps are quantize.rs's: Bits, mid-tread or mid-rise, the µ-law Shape
  around them and Dither before them -- applied to every level a hold takes.
- The hold is hold.rs's, in two forms. Anti-Alias off: the raw digital
  crusher, levels taken and jumps drawn on the host's sample grid. On: each
  level taken at its exact instant and each jump drawn band-limited, so the
  crusher sounds the same at every host rate and nothing folds back past the
  project's Nyquist (hold.rs, blep.rs). Steps and hold commute -- a level is
  quantised once, whenever it is taken -- so the two forms differ only in time.
- The post-filter (Rate/2, moved by Octave) smooths the hold's steps.
- The DC blocker takes out the half step a mid-rise grid adds to silence. It
  comes in with Mid-Rise: a mid-tread grid adds no DC, and is left its exact
  levels and its phase.

LATENCY: blep::LATENCY samples (32), always. The band-limited hold needs it;
the raw one and the dry signal are delayed by as much, so Anti-Alias can be
crossfaded and the Mix stays phase-coherent. A host is told (bc_latency), and
it never changes.

NOTHING CLICKS WHEN A CONTROL MOVES. Every number glides (ni_dsp::smooth, 5
ms): the gains linearly, Bits on bits -- already a logarithm of the step --
and Rate on its logarithm, so a sweep moves evenly in pitch. The switches
glide too, as crossfades: Mid-Rise between the two grids (and in or out of the
DC blocker), Anti-Alias between the two holds, the filters between the
filtered and the unfiltered signal, and the host's bypass between the crusher
and the delayed dry signal (set_active). Jitter and Dither are not glided:
they scale noise, and a jump in noise is not a click.

THE AUDIO THREAD ALLOCATES NOTHING: every buffer is the caller's or a fixed
array, every state a field (tests/no_alloc.rs holds it to that).
*/

pub mod blep;
pub mod filters;
pub mod hold;
pub mod params;
pub mod quantize;

use core::fmt::Write;
use filters::{DcBlock, Lowpass, LowpassCoefficients, PreCoefficients, PreFilter, Tone, ToneCoefficients};
use hold::{BandLimitedHold, Hold};
use ni_dsp::fmt::Buf;
use ni_dsp::smooth::{coef, glide};
pub use params::{Info, Kind, Param, Travel, Unit, PARAM_COUNT, REFRESH_LABELS, REFRESH_PERIOD_MS, SLOPE_LABELS, TOGGLE_LABELS};
use quantize::{Grid, Shape};

pub use blep::LATENCY;

/// The generators' seeds, one per channel: fixed, so a render is the same
/// render every time, and different, so jitter widens the stereo image.
const HOLD_SEEDS: [u64; 2] = [0x9E37_79B9_7F4A_7C15, 0xD1B5_4A32_D192_ED03];
const DITHER_SEEDS: [u64; 2] = [0x2545_F491_4F6C_DD1D, 0x94D0_49BB_1331_11EB];

/// The sample rate when none is given.
pub const DEFAULT_SAMPLE_RATE: f64 = 44_100.0;

fn db_to_amp(db: f64) -> f64 {
    (10.0f64).powf(db / 20.0)
}

/// Where a crossfade is: 0 is the first side, 1 the second.
#[inline]
fn fade(a: f64, b: f64, t: f64) -> f64 {
    if t <= 0.0 {
        a
    } else if t >= 1.0 {
        b
    } else {
        a + (b - a) * t
    }
}

/// Everything that glides, in the unit it glides in.
#[derive(Clone, Copy, Debug, PartialEq)]
struct Glides {
    drive: f64,
    bits: f64,
    shape: f64,
    mid_rise: f64,
    anti_alias: f64,
    /// log2 of Rate.
    log_rate: f64,
    pre: f64,
    post: f64,
    post_octave: f64,
    mix: f64,
    output: f64,
    /// 1 while the crusher is heard, 0 while the host bypasses it.
    active: f64,
    /// The filter: in or out, log2 of its cutoff, its peak in dB, and one
    /// weight a slope -- 1 on the chosen one -- so Slope crossfades.
    filter: f64,
    log_cutoff: f64,
    peak: f64,
    slope: [f64; 3],
}

impl Glides {
    fn targets(v: &[f64; PARAM_COUNT], active: bool) -> Glides {
        let at = |p: Param| v[p as usize];
        Glides {
            drive: db_to_amp(at(Param::Drive)),
            bits: at(Param::Bits),
            shape: at(Param::Shape) / 100.0,
            mid_rise: at(Param::MidRise),
            anti_alias: at(Param::AntiAlias),
            log_rate: at(Param::Rate).log2(),
            pre: at(Param::PreFilter),
            post: at(Param::PostFilter),
            post_octave: at(Param::PostOctave),
            mix: at(Param::Mix) / 100.0,
            output: db_to_amp(at(Param::Output)),
            active: active as i32 as f64,
            filter: at(Param::Filter),
            log_cutoff: at(Param::Cutoff).log2(),
            peak: at(Param::Peak),
            slope: core::array::from_fn(|i| (at(Param::Slope) as usize == i) as i32 as f64),
        }
    }

    fn toward(&mut self, t: &Glides, c: f64) {
        for (x, target) in [
            (&mut self.drive, t.drive),
            (&mut self.bits, t.bits),
            (&mut self.shape, t.shape),
            (&mut self.mid_rise, t.mid_rise),
            (&mut self.anti_alias, t.anti_alias),
            (&mut self.log_rate, t.log_rate),
            (&mut self.pre, t.pre),
            (&mut self.post, t.post),
            (&mut self.post_octave, t.post_octave),
            (&mut self.mix, t.mix),
            (&mut self.output, t.output),
            (&mut self.active, t.active),
            (&mut self.filter, t.filter),
            (&mut self.log_cutoff, t.log_cutoff),
            (&mut self.peak, t.peak),
        ] {
            *x = glide(*x, target, c);
        }
        for (x, target) in self.slope.iter_mut().zip(t.slope) {
            *x = glide(*x, target, c);
        }
    }
}

/// What every channel uses for one sample.
struct Now {
    drive: f64,
    tread: Grid,
    rise: Grid,
    mid_rise: f64,
    anti_alias: f64,
    shape: Shape,
    dither: f64,
    step: f64,
    jitter: f64,
    pre: f64,
    post: f64,
    mix: f64,
    output: f64,
    active: f64,
    filter: f64,
    slope: [f64; 3],
}

impl Now {
    /// THE STEPS: the level a value taken by a hold becomes -- Shape around
    /// the grid, Dither before it, mid-tread or mid-rise or between.
    #[inline]
    fn level(&self, x: f64, dither: &mut fastrand::Rng) -> f64 {
        let mut c = self.shape.compress(x);
        if self.dither > 0.0 {
            /* Triangular: two uniforms, so ±1 step at 100 %, and an error
             * independent of the signal from 50 % up. */
            c += (dither.f64() + dither.f64() - 1.0) * self.tread.delta() * self.dither;
        }
        let q = if self.mid_rise <= 0.0 {
            self.tread.quantize(c)
        } else if self.mid_rise >= 1.0 {
            self.rise.quantize(c)
        } else {
            fade(self.tread.quantize(c), self.rise.quantize(c), self.mid_rise)
        };
        self.shape.expand(q)
    }
}

/// A fixed delay of LATENCY samples.
#[derive(Clone, Debug)]
struct Delay {
    ring: [f64; 64],
    at: usize,
}

impl Default for Delay {
    fn default() -> Self {
        Delay { ring: [0.0; 64], at: 0 }
    }
}

impl Delay {
    #[inline]
    fn push(&mut self, x: f64) -> f64 {
        const _: () = assert!(LATENCY < 64);
        self.ring[self.at] = x;
        let out = self.ring[(self.at + 64 - LATENCY) % 64];
        self.at = (self.at + 1) % 64;
        out
    }
}

/// One channel's state.
#[derive(Clone, Debug)]
struct Channel {
    pre: PreFilter,
    post: Lowpass,
    /// The resonant low-pass after the crusher (the Filter panel).
    tone: Tone,
    dc: DcBlock,
    /// The raw hold, its output delayed to the band-limited hold's.
    hold: Hold,
    raw_late: Delay,
    /// The band-limited hold, and its own generators: the two holds draw the
    /// same jitter and dither sequences, so they agree on everything but time.
    exact: BandLimitedHold,
    hold_rng: fastrand::Rng,
    dither_rng: fastrand::Rng,
    exact_hold_rng: fastrand::Rng,
    exact_dither_rng: fastrand::Rng,
    /// The input, delayed as the crushed signal is.
    dry_late: Delay,
    /// What the last sample did, for an editor's pictures.
    probe: Probe,
}

/// WHAT ONE SAMPLE DID, for an editor's pictures of the two discretizations:
/// the level the quantiser was given -- after Drive and the pre-filter, so
/// Drive is in it: the axis `transfer` at 0 dB draws the staircase on, where
/// a staircase always runs along the diagonal and Drive moves the signal up
/// it -- the level it became (Dither in it), and whether the hold took that
/// level. Bypassed, the input passes and nothing is taken.
///
/// With Anti-Alias on, the band-limited hold takes its levels at exact
/// instants between samples, and a little later than the plain one (it reads
/// HALF_WIDTH samples ahead): `exact_take` is the instant of the first level
/// it took during this sample, in samples relative to this one (negative,
/// about HALF_WIDTH back), or None.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Probe {
    pub input: f64,
    pub level: f64,
    pub took: bool,
    pub exact_take: Option<f64>,
}

impl Channel {
    fn new(
        index: usize,
        pre: &PreCoefficients,
        post: &LowpassCoefficients,
        tone: &ToneCoefficients,
        sample_rate: f64,
    ) -> Channel {
        Channel {
            pre: PreFilter::new(pre),
            post: Lowpass::new(post),
            tone: Tone::new(tone),
            dc: DcBlock::new(sample_rate),
            hold: Hold::default(),
            raw_late: Delay::default(),
            exact: BandLimitedHold::default(),
            hold_rng: fastrand::Rng::with_seed(HOLD_SEEDS[index]),
            dither_rng: fastrand::Rng::with_seed(DITHER_SEEDS[index]),
            exact_hold_rng: fastrand::Rng::with_seed(HOLD_SEEDS[index]),
            exact_dither_rng: fastrand::Rng::with_seed(DITHER_SEEDS[index]),
            dry_late: Delay::default(),
            probe: Probe::default(),
        }
    }

    /// Everything the crusher holds, but not the delayed dry signal: what a
    /// return from the host's bypass starts from.
    fn reset_wet(&mut self, index: usize) {
        self.pre.reset();
        self.post.reset();
        self.dc.reset();
        self.tone.reset();
        self.hold.reset();
        self.raw_late = Delay::default();
        self.exact.reset();
        self.hold_rng = fastrand::Rng::with_seed(HOLD_SEEDS[index]);
        self.dither_rng = fastrand::Rng::with_seed(DITHER_SEEDS[index]);
        self.exact_hold_rng = fastrand::Rng::with_seed(HOLD_SEEDS[index]);
        self.exact_dither_rng = fastrand::Rng::with_seed(DITHER_SEEDS[index]);
    }

    /// Everything: the panic.
    fn reset(&mut self, index: usize) {
        self.reset_wet(index);
        self.dry_late = Delay::default();
    }

    #[inline]
    fn run(&mut self, dry: f64, now: &Now) -> f64 {
        /* A NaN or an infinity from a host would sit in the filters' state
         * for good; it is silence instead. */
        let dry = if dry.is_finite() { dry } else { 0.0 };
        let dry_late = self.dry_late.push(dry);
        if now.active <= 0.0 {
            self.probe = Probe { input: dry, level: dry, took: false, exact_take: None };
            return dry_late;
        }

        let x = dry * now.drive;
        let filtered = self.pre.run(x);
        let x = fade(x, filtered, now.pre);

        /* The raw hold: steps on every host sample, then the hold -- which
         * is the same as stepping only what it takes. */
        let level = now.level(x, &mut self.dither_rng);
        let raw = self.hold.step(level, now.step, now.jitter, &mut self.hold_rng);
        self.probe = Probe { input: x, level, took: self.hold.took(), exact_take: None };
        let raw = self.raw_late.push(raw);
        let mut exact_take = None;
        let held = if now.anti_alias > 0.0 {
            let rng = &mut self.exact_dither_rng;
            let exact = self.exact.step(x, now.step, now.jitter, &mut self.exact_hold_rng, |v| now.level(v, rng));
            exact_take = self.exact.first_take();
            fade(raw, exact, now.anti_alias)
        } else {
            self.exact.follow(x, self.hold.held(), self.hold.phase());
            raw
        };
        self.probe.exact_take = exact_take;

        let smoothed = self.post.run(held);
        let wet = fade(held, smoothed, now.post);
        /* The DC blocker follows Mid-Rise in: a mid-tread grid is symmetric
         * about zero and adds no DC, so it keeps its exact levels and its
         * phase. The blocker runs either way, ready for the switch. */
        let blocked = self.dc.run(wet);
        let wet = fade(wet, blocked, now.mid_rise);

        /* THE FILTER, on the crushed signal only, so Mix still brings the dry
         * one in clean. All three slopes run; their weights crossfade. Off,
         * it still runs, ready for the switch. */
        let (y12, y24, y48) = self.tone.run(wet);
        let toned = y12 * now.slope[0] + y24 * now.slope[1] + y48 * now.slope[2];
        let wet = fade(wet, toned, now.filter);

        let heard = (dry_late * (1.0 - now.mix) + wet * now.mix) * now.output;
        fade(dry_late, heard, now.active)
    }
}

/// One bit crusher, stereo.
pub struct Engine {
    sample_rate: f64,
    /// The glides' per-sample coefficient at this rate.
    coef: f64,
    values: [f64; PARAM_COUNT],
    targets: Glides,
    now: Glides,
    /// The grids and the curve at `now`'s Bits and Shape, kept while those
    /// stand still.
    tread: Grid,
    rise: Grid,
    shape: Shape,
    /// The filters' coefficients, shared by both channels, and the cutoffs
    /// they were made for.
    pre_coefficients: PreCoefficients,
    post_coefficients: LowpassCoefficients,
    pre_hz: f64,
    post_hz: f64,
    /// The filter's coefficients, and the cutoff and peak they were made for.
    tone_coefficients: ToneCoefficients,
    tone_hz: f64,
    tone_peak: f64,
    channels: [Channel; 2],
    /// Whether the host hears the crusher (set_active).
    active: bool,
    /// Samples processed since a parameter of the sound last changed: what
    /// says an editor's look was made with the settings it shows.
    settled: u32,
}

impl Engine {
    /// A new engine at `sample_rate` (44.1 kHz if that is not positive),
    /// every parameter at its default.
    pub fn new(sample_rate: f64) -> Engine {
        let sample_rate = usable_rate(sample_rate);
        let values = Param::ALL.map(Param::default_value);
        let targets = Glides::targets(&values, true);
        let (pre_hz, post_hz) = cutoffs(&targets);
        let (tone_hz, tone_peak) = (targets.log_cutoff.exp2(), targets.peak);
        let tone_coefficients = ToneCoefficients::new(tone_hz, tone_peak, sample_rate);
        let pre_coefficients = PreCoefficients::new(pre_hz, sample_rate);
        let post_coefficients = LowpassCoefficients::new(post_hz, sample_rate);
        Engine {
            sample_rate,
            coef: coef(sample_rate),
            values,
            targets,
            now: targets,
            tread: Grid::new(targets.bits, false),
            rise: Grid::new(targets.bits, true),
            shape: Shape::new(targets.shape),
            pre_coefficients,
            post_coefficients,
            pre_hz,
            post_hz,
            tone_coefficients,
            tone_hz,
            tone_peak,
            channels: [0, 1].map(|i| Channel::new(i, &pre_coefficients, &post_coefficients, &tone_coefficients, sample_rate)),
            active: true,
            settled: 0,
        }
    }

    /// Samples processed since a parameter that shapes an editor's looks --
    /// the quantiser's and the hold's (Param::shapes_the_looks) -- last
    /// changed, saturating. A look whose window is shorter was made with
    /// these settings throughout.
    pub fn settled(&self) -> u32 {
        self.settled
    }

    /// How late the output is, in samples: always LATENCY.
    pub fn latency(&self) -> usize {
        LATENCY
    }

    /// Whether the crusher is heard. Off -- the host's bypass -- it fades to
    /// the delayed dry signal, so the latency a host compensates for stays the
    /// same and nothing jumps; once faded out it only delays. Back on, it
    /// starts from silence (nothing held from before the bypass) and fades in.
    pub fn set_active(&mut self, active: bool) {
        if active == self.active {
            return;
        }
        if active && self.now.active <= 0.0 {
            for (i, ch) in self.channels.iter_mut().enumerate() {
                ch.reset_wet(i);
            }
        }
        self.active = active;
        self.targets = Glides::targets(&self.values, active);
    }

    pub fn sample_rate(&self) -> f64 {
        self.sample_rate
    }

    /// The host's rate. Every filter is made again for it; what the filters
    /// held is kept, as a rate change mid-stream is still the same stream.
    pub fn set_sample_rate(&mut self, sample_rate: f64) {
        let sample_rate = usable_rate(sample_rate);
        if sample_rate == self.sample_rate {
            return;
        }
        self.sample_rate = sample_rate;
        self.coef = coef(sample_rate);
        for ch in &mut self.channels {
            ch.dc = DcBlock::new(sample_rate);
        }
        self.retune(true);
    }

    /// The panic: everything held lets go, every filter is emptied and the
    /// generators start again, so the next block starts from silence and
    /// renders as a fresh instance would. The settings stay.
    pub fn reset(&mut self) {
        for (i, ch) in self.channels.iter_mut().enumerate() {
            ch.reset(i);
        }
    }

    /// One MIDI message. A crusher plays no notes, so the only messages it
    /// acts on are the panic -- CC 120 (All Sound Off) and CC 123 (All Notes
    /// Off), on any channel -- which reset it: the Move has no other way to
    /// say it. The bytes are `wmidi`'s to decode, so a cut-short or running-
    /// status message is no panic.
    pub fn on_midi(&mut self, msg: &[u8]) {
        use wmidi::{ControlFunction, MidiMessage};
        if let Ok(MidiMessage::ControlChange(
            _,
            ControlFunction::ALL_SOUND_OFF | ControlFunction::ALL_NOTES_OFF,
            _,
        )) = MidiMessage::try_from(msg)
        {
            self.reset();
        }
    }

    /// The automation door: `v` in the parameter's own unit, clamped to its
    /// range. A NaN is not a value and changes nothing.
    pub fn set_num(&mut self, p: Param, v: f64) {
        if v.is_nan() {
            return;
        }
        let v = p.clamp(v);
        if v != self.values[p as usize] && p.shapes_the_looks() {
            self.settled = 0;
        }
        self.values[p as usize] = v;
        self.targets = Glides::targets(&self.values, self.active);
    }

    /// The value as set, in the parameter's unit.
    pub fn num(&self, p: Param) -> f64 {
        self.values[p as usize]
    }

    /// The text door, for the Move's `set_param`: a key and the value as
    /// text, read as `Param::parse_value` reads typing. `panic` is not a
    /// parameter but the host's panic, which the Move can only send this way
    /// or as MIDI. False for a key it does not own.
    pub fn set_param(&mut self, key: &str, val: &str) -> bool {
        if key == "panic" {
            self.reset();
            return true;
        }
        let Some(p) = Param::from_key(key) else {
            return false;
        };
        if let Some(v) = p.parse_value(val) {
            self.set_num(p, v);
        }
        true
    }

    /// A parameter's value by its key, as a number at full round-trip
    /// precision, into `out` (NUL-terminated); its length, or -1 for a key it
    /// does not serve.
    pub fn get_param(&self, key: &str, out: &mut [u8]) -> i32 {
        let Some(p) = Param::from_key(key) else {
            return -1;
        };
        let mut b = Buf::new(out);
        if write!(b, "{}", self.num(p)).is_err() {
            return -1;
        }
        b.finish()
    }

    /// The filters' cutoffs follow the glided Rate and Octave; they are made
    /// again only when one has moved by more than a hundredth of a percent.
    fn retune(&mut self, force: bool) {
        let (pre_hz, post_hz) = cutoffs(&self.now);
        let moved = |a: f64, b: f64| force || (a / b - 1.0).abs() > 1e-4;
        if moved(pre_hz, self.pre_hz) {
            self.pre_hz = pre_hz;
            self.pre_coefficients = PreCoefficients::new(pre_hz, self.sample_rate);
            for ch in &mut self.channels {
                ch.pre.retune(&self.pre_coefficients);
            }
        }
        if moved(post_hz, self.post_hz) {
            self.post_hz = post_hz;
            self.post_coefficients = LowpassCoefficients::new(post_hz, self.sample_rate);
            for ch in &mut self.channels {
                ch.post.retune(&self.post_coefficients);
            }
        }
        let (tone_hz, tone_peak) = (self.now.log_cutoff.exp2(), self.now.peak);
        if moved(tone_hz, self.tone_hz) || force || (tone_peak - self.tone_peak).abs() > 1e-3 {
            self.tone_hz = tone_hz;
            self.tone_peak = tone_peak;
            self.tone_coefficients = ToneCoefficients::new(tone_hz, tone_peak, self.sample_rate);
            for ch in &mut self.channels {
                ch.tone.retune(&self.tone_coefficients);
            }
        }
    }

    /// One sample of every glide, and what the channels use for it.
    #[inline]
    fn advance(&mut self) -> Now {
        let before = self.now;
        if before != self.targets {
            self.now.toward(&self.targets, self.coef);
            if self.now.bits != before.bits {
                self.tread = Grid::new(self.now.bits, false);
                self.rise = Grid::new(self.now.bits, true);
            }
            if self.now.shape != before.shape {
                self.shape = Shape::new(self.now.shape);
            }
            if self.now.log_rate != before.log_rate
                || self.now.post_octave != before.post_octave
                || self.now.log_cutoff != before.log_cutoff
                || self.now.peak != before.peak
            {
                self.retune(false);
            }
        }
        let n = &self.now;
        Now {
            drive: n.drive,
            tread: self.tread,
            rise: self.rise,
            mid_rise: n.mid_rise,
            anti_alias: n.anti_alias,
            shape: self.shape,
            dither: self.values[Param::Dither as usize] / 100.0,
            step: self.step(),
            jitter: self.values[Param::Jitter as usize] / 100.0,
            pre: n.pre,
            post: n.post,
            mix: n.mix,
            output: n.output,
            active: n.active,
            filter: n.filter,
            slope: n.slope,
        }
    }

    /// The hold's step: Rate / host rate. From the glide while Rate moves,
    /// and from Rate itself once it has landed -- exp2(log2(x)) is not
    /// always x, and a step a hair short of 1/8 holds the eighth sample one
    /// too many every so often.
    #[inline]
    fn step(&self) -> f64 {
        let rate = if self.now.log_rate == self.targets.log_rate {
            self.values[Param::Rate as usize]
        } else {
            self.now.log_rate.exp2()
        };
        rate / self.sample_rate
    }

    /// One block of host floats, in place: `left`, and `right` for a stereo
    /// track. A right channel shorter than the left is processed as far as
    /// it goes.
    pub fn process_f32(&mut self, left: &mut [f32], right: Option<&mut [f32]>) {
        self.process_f32_probed(left, right, |_, _| {});
    }

    /// `process_f32`, telling `probe` what each left-channel sample did (its
    /// index in the block, and the [`Probe`]): the editor's pictures, kept by
    /// the caller. Inlined, so `process_f32`'s empty probe costs nothing.
    #[inline]
    pub fn process_f32_probed(
        &mut self,
        left: &mut [f32],
        mut right: Option<&mut [f32]>,
        mut probe: impl FnMut(usize, Probe),
    ) {
        for (i, l) in left.iter_mut().enumerate() {
            let now = self.advance();
            *l = self.channels[0].run(f64::from(*l), &now) as f32;
            probe(i, self.channels[0].probe);
            self.settled = self.settled.saturating_add(1);
            if let Some(r) = right.as_deref_mut().and_then(|r| r.get_mut(i)) {
                *r = self.channels[1].run(f64::from(*r), &now) as f32;
            }
        }
    }

    /// One block of the Move's interleaved 16-bit stereo, in place: `frames`
    /// frames of `buf`, or as many as it holds. Rounded back to the nearest
    /// sixteen-bit value and clipped there.
    pub fn process_i16(&mut self, buf: &mut [i16], frames: usize) {
        const SCALE: f64 = 32_768.0;
        let to_i16 = |y: f64| (y * SCALE).round().clamp(-SCALE, SCALE - 1.0) as i16;
        for frame in buf.as_chunks_mut::<2>().0.iter_mut().take(frames) {
            let now = self.advance();
            self.settled = self.settled.saturating_add(1);
            let l = self.channels[0].run(f64::from(frame[0]) / SCALE, &now);
            let r = self.channels[1].run(f64::from(frame[1]) / SCALE, &now);
            frame[0] = to_i16(l);
            frame[1] = to_i16(r);
        }
    }
}

/// The pre-filter's stopband edge and the post-filter's cutoff for a set of
/// glides: Rate/2, and Rate/2 moved by Octave.
fn cutoffs(g: &Glides) -> (f64, f64) {
    let nyquist = 0.5 * g.log_rate.exp2();
    (nyquist, nyquist * g.post_octave.exp2())
}

#[allow(clippy::neg_cmp_op_on_partial_ord, reason = "a NaN rate must take the default")]
fn usable_rate(sample_rate: f64) -> f64 {
    if !(sample_rate > 0.0) || sample_rate.is_infinite() {
        DEFAULT_SAMPLE_RATE
    } else {
        sample_rate
    }
}

/// THE STEPS AS A CURVE, for an editor: input -1..1 in, through Drive, Shape,
/// the grid and the clip -- what the quantiser does to each level, without
/// the noise and the time. The engine's own grid and curve, so a plot drawn
/// from it is the staircase the engine uses, never a second model of it.
/// A NaN setting is the parameter's default.
pub fn transfer(drive_db: f64, bits: f64, shape_pct: f64, mid_rise: bool, x: f64) -> f64 {
    let value = |p: Param, v: f64| if v.is_nan() { p.default_value() } else { p.clamp(v) };
    let grid = Grid::new(value(Param::Bits, bits), mid_rise);
    let shape = Shape::new(value(Param::Shape, shape_pct) / 100.0);
    let x = x * db_to_amp(value(Param::Drive, drive_db));
    shape.expand(grid.quantize(shape.compress(x)))
}

/// How many levels the staircase has at `bits`, mid-tread or mid-rise
/// (quantize.rs: 2^b + 1 and 2^b at whole Bits). A NaN Bits is its default.
pub fn level_count(bits: f64, mid_rise: bool) -> u32 {
    let bits = if bits.is_nan() { Param::Bits.default_value() } else { Param::Bits.clamp(bits) };
    Grid::new(bits, mid_rise).levels()
}

#[cfg(test)]
mod tests;
