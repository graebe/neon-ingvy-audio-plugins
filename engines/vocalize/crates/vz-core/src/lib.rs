// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
NI Vocalize's engine: any sound, more and more a voice singing A, E, I, O or U.

THE IDEA. A voice is a source and a filter: the folds buzz, the throat and
the mouth resonate, and those resonances -- the formants -- make the vowel
(model.rs). So the input is taken as the source: its own colour, its spectral
envelope, is traded for the vowel's, and what made it itself -- its pitch, its
noise, its rhythm -- stays. Vocal is how far the trade has gone:

```text
  out envelope = in envelope^(1 - v) · vowel envelope^v,   v = Vocal
```

so 0 % is the input untouched, bit for bit, and 100 % is the input's fine
structure on the vowel's envelope alone. Turned slowly, the sound does not
get a filter put on it; it becomes a voice.

TWO ENGINES, ONE VOWEL. Mode picks how the trade is made:

```text
  in ─ Excite ─┬─ Time:     flatten (lpc.rs, A(z/γ)) ─ the vowel's resonators ─ level ─┐
               └─ Spectral: frames (spectral.rs), envelope traded bin by bin ──────────┤
   └──────────────────────────────── dry, as late as the engine ─────────────── Mix ─┴─ Output ─ out
```

- Time has no latency. Linear prediction measures the input's envelope and an
  FIR takes it out, as far as Vocal says; the vowel's five resonators put
  theirs in, as far as Vocal says; a slow follower holds the level to the
  input's. Resonant, punchy: a talkbox.
- Spectral is late by one frame (about 21 ms), and exact: the morph above,
  bin by bin, every frame at the energy it came in with. Smoother, more sung.
- Both put on the same vowel: the Spectral engine's curve is the Time
  engine's filter, measured (model.rs).

A SWITCH OF MODE DIPS, never clicks: the output fades out over 10 ms, the
engines change -- the new one primed from the history both share, so it is
whole from its first sample -- and it fades in over 10 ms. The latency changes
with it; the plugin tells its host (vz-capi's vz_latency).

NOTHING CLICKS WHEN A CONTROL MOVES. Every number glides (ni_dsp::smooth, 5
ms); the vowel's resonators and the flattener's reach are remade from the
glides every 16 samples.

THE MOVE GETS THE TIME ENGINE ONLY: without the `spectral` feature (vz-move
builds without it) Mode is read but every value is Time, and the module
carries no FFT.

THE AUDIO THREAD ALLOCATES NOTHING: every buffer is made in `new` at its
largest, every state is a field (tests/no_alloc.rs holds it to that).
*/

pub mod excite;
pub mod lpc;
pub mod model;
pub mod params;
mod ring;
#[cfg(feature = "spectral")]
pub mod spectral;

use core::fmt::Write;
use excite::Excite;
use lpc::{Analysis, Flattener, MAX_ORDER};
use model::{Formants, Pole, FORMANTS};
use ni_dsp::fmt::Buf;
use ni_dsp::smooth::{coef, glide};
pub use params::{Info, Kind, Param, Unit, MODE_LABELS, PARAM_COUNT, VOWEL_LABELS};
pub use ring::Ring;

/// Whether this build has the Spectral engine: the plugin's does, the Move's
/// does not.
pub const SPECTRAL: bool = cfg!(feature = "spectral");

/// The sample rate when none is given.
pub const DEFAULT_SAMPLE_RATE: f64 = 44_100.0;

/// Samples between remaking the vowel's resonators while a glide moves.
const SUB: usize = 16;

/// The Time engine's level follower, in ms.
const LEVEL_MS: f64 = 50.0;

/// The Time engine's level correction stays within +24 / -48 dB.
const MAX_GAIN: f64 = 16.0;
const MIN_GAIN: f64 = 1.0 / 256.0;

/// Each half of a mode switch's dip, in ms.
const DIP_MS: f64 = 10.0;

/// The engine that makes the trade.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Mode {
    /// No latency: linear prediction and five resonators.
    Time,
    /// One frame late: the envelope traded bin by bin.
    Spectral,
}

impl Mode {
    /// The mode a Mode value means in this build: every value is Time
    /// without the `spectral` feature.
    pub fn from_value(v: f64) -> Mode {
        if SPECTRAL && v >= 0.5 {
            Mode::Spectral
        } else {
            Mode::Time
        }
    }
}

/// The latency, in samples, of `mode` at `sample_rate`: what a host is told.
pub fn latency(mode: Mode, sample_rate: f64) -> usize {
    match mode {
        Mode::Time => 0,
        #[cfg(feature = "spectral")]
        Mode::Spectral => spectral::size_at(usable_rate(sample_rate)),
        #[cfg(not(feature = "spectral"))]
        Mode::Spectral => {
            let _ = sample_rate;
            0
        }
    }
}

fn db_to_amp(db: f64) -> f64 {
    (10.0f64).powf(db / 20.0)
}

/// Everything that glides, in the unit it glides in.
#[derive(Clone, Copy, Debug, PartialEq)]
struct Glides {
    /// 0..1.
    vocal: f64,
    /// 0..4.
    vowel: f64,
    /// -100..100 %.
    size: f64,
    /// 0..100 %.
    resonance: f64,
    /// 0..1.
    excite: f64,
    /// 0..1.
    mix: f64,
    /// A gain.
    output: f64,
}

impl Glides {
    fn targets(v: &[f64; PARAM_COUNT]) -> Glides {
        let at = |p: Param| v[p as usize];
        Glides {
            vocal: at(Param::Vocal) / 100.0,
            vowel: at(Param::Vowel),
            size: at(Param::Size),
            resonance: at(Param::Resonance),
            excite: at(Param::Excite) / 100.0,
            mix: at(Param::Mix) / 100.0,
            output: db_to_amp(at(Param::Output)),
        }
    }

    fn toward(&mut self, t: &Glides, c: f64) {
        for (x, target) in [
            (&mut self.vocal, t.vocal),
            (&mut self.vowel, t.vowel),
            (&mut self.size, t.size),
            (&mut self.resonance, t.resonance),
            (&mut self.excite, t.excite),
            (&mut self.mix, t.mix),
            (&mut self.output, t.output),
        ] {
            *x = glide(*x, target, c);
        }
    }

    /// What the vowel's resonators are made from.
    fn shape(&self) -> [f64; 4] {
        [self.vocal, self.vowel, self.size, self.resonance]
    }
}

/// Where a mode switch is: the output's gain on its way down or up.
#[derive(Clone, Copy, Debug, PartialEq)]
enum Switch {
    Steady,
    Out(f64),
    In(f64),
}

/// One channel's state.
struct Channel {
    /// The input as it came, for Mix.
    dry: Ring,
    /// The input after Excite: what both engines read.
    src: Ring,
    excite: Excite,
    flat: Flattener,
    /// The resonators' last two outputs.
    y: [[f64; 2]; FORMANTS],
    #[cfg(feature = "spectral")]
    spec: spectral::Channel,
}

impl Channel {
    fn new(fs: f64) -> Channel {
        Channel {
            dry: Ring::new(),
            src: Ring::new(),
            excite: Excite::new(fs),
            flat: Flattener::default(),
            y: [[0.0; 2]; FORMANTS],
            #[cfg(feature = "spectral")]
            spec: spectral::Channel::new(),
        }
    }

    fn reset(&mut self) {
        self.dry.clear();
        self.src.clear();
        self.excite.reset();
        self.flat = Flattener::default();
        self.y = [[0.0; 2]; FORMANTS];
        #[cfg(feature = "spectral")]
        self.spec.reset();
    }
}

/// One vocaliser, stereo.
pub struct Engine {
    sample_rate: f64,
    /// The glides' per-sample coefficient at this rate.
    coef: f64,
    values: [f64; PARAM_COUNT],
    targets: Glides,
    now: Glides,
    /// The engine running, the switch to another, and the dip's step.
    mode: Mode,
    switch: Switch,
    dip_step: f64,
    /// The latency of the engine running: how late the dry signal is read.
    delay: usize,
    /// The vowel's resonators at `now`: as Vocal has them (`poles`) and whole
    /// (`full`); the flattener's γ^k; and what they were made from.
    poles: [Pole; FORMANTS],
    full: [Pole; FORMANTS],
    powers: [f64; MAX_ORDER + 1],
    shaped: [f64; 4],
    sub: usize,
    analysis: Analysis,
    /// Where in the analysis' hop.
    lpc_pos: usize,
    /// The Time engine's level: the dry and the wet power, followed.
    level: [f64; 2],
    level_coef: f64,
    channels: [Channel; 2],
    #[cfg(feature = "spectral")]
    spectral: spectral::Spectral,
    /// Whether the Spectral engine's curve is older than `full`.
    #[cfg(feature = "spectral")]
    vowel_stale: bool,
}

impl Engine {
    /// A new engine at `sample_rate` (44.1 kHz if that is not positive),
    /// every parameter at its default. Allocates: not the audio thread.
    pub fn new(sample_rate: f64) -> Engine {
        let fs = usable_rate(sample_rate);
        let values = Param::ALL.map(Param::default_value);
        let targets = Glides::targets(&values);
        let mut e = Engine {
            sample_rate: fs,
            coef: coef(fs),
            values,
            targets,
            now: targets,
            mode: Mode::from_value(values[Param::Mode as usize]),
            switch: Switch::Steady,
            dip_step: 0.0,
            delay: 0,
            poles: [Pole::NONE; FORMANTS],
            full: [Pole::NONE; FORMANTS],
            powers: [0.0; MAX_ORDER + 1],
            shaped: [f64::NAN; 4],
            sub: 0,
            analysis: Analysis::new(fs),
            lpc_pos: 0,
            level: [0.0; 2],
            level_coef: 0.0,
            channels: [Channel::new(fs), Channel::new(fs)],
            #[cfg(feature = "spectral")]
            spectral: spectral::Spectral::new(fs),
            #[cfg(feature = "spectral")]
            vowel_stale: true,
        };
        e.rates();
        e.reshape();
        e
    }

    pub fn sample_rate(&self) -> f64 {
        self.sample_rate
    }

    /// The host's rate. Every table is made again for it, in place, and what
    /// was held lets go: the frames and the analysis are sized by the rate.
    pub fn set_sample_rate(&mut self, sample_rate: f64) {
        let fs = usable_rate(sample_rate);
        if fs == self.sample_rate {
            return;
        }
        self.sample_rate = fs;
        self.coef = coef(fs);
        self.analysis.set_rate(fs);
        #[cfg(feature = "spectral")]
        self.spectral.set_rate(fs);
        for ch in &mut self.channels {
            ch.excite.set_rate(fs);
        }
        self.rates();
        self.shaped = [f64::NAN; 4];
        self.reshape();
        self.reset();
    }

    fn rates(&mut self) {
        let fs = self.sample_rate;
        self.dip_step = 1000.0 / (DIP_MS * fs);
        self.level_coef = 1.0 - (-1000.0 / (LEVEL_MS * fs)).exp();
    }

    /// The panic: everything held lets go -- the history, the frames, the
    /// resonators -- so the next block starts from silence and renders as a
    /// fresh instance would. A switch under way lands. The settings stay.
    pub fn reset(&mut self) {
        for ch in &mut self.channels {
            ch.reset();
        }
        self.level = [0.0; 2];
        self.lpc_pos = 0;
        self.mode = Mode::from_value(self.values[Param::Mode as usize]);
        self.switch = Switch::Steady;
        self.delay = latency(self.mode, self.sample_rate);
    }

    /// One MIDI message. Only the panic -- CC 120 (All Sound Off) and CC 123
    /// (All Notes Off), on any channel -- does anything: it resets. The bytes
    /// are `wmidi`'s to decode, so a cut-short message is no panic.
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
        self.values[p as usize] = p.clamp(v);
        self.targets = Glides::targets(&self.values);
    }

    /// The value as set, in the parameter's unit.
    pub fn num(&self, p: Param) -> f64 {
        self.values[p as usize]
    }

    /// The text door, for the Move's `set_param`: a key and the value as
    /// text, read as `Param::parse_value` reads typing. `panic` is the host's
    /// panic. False for a key it does not own.
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

    /// The engine running now: Mode's, once a switch has landed.
    pub fn mode(&self) -> Mode {
        self.mode
    }

    /// The engine running now's latency, in samples.
    pub fn latency(&self) -> usize {
        self.delay
    }

    /// The vowel's resonators and the flattener's reach, remade from the
    /// glides when they moved.
    fn reshape(&mut self) {
        let shape = self.now.shape();
        if shape == self.shaped {
            return;
        }
        self.shaped = shape;
        let [vocal, vowel, size, resonance] = shape;
        let f = Formants::at(vowel, size, resonance);
        let full = f.poles(self.sample_rate, 1.0);
        #[cfg(feature = "spectral")]
        if full != self.full {
            self.vowel_stale = true;
        }
        self.full = full;
        self.poles = f.poles(self.sample_rate, vocal);
        self.powers = lpc::powers(vocal);
    }

    /// One sample of every glide.
    #[inline]
    fn advance(&mut self) {
        if self.now != self.targets {
            self.now.toward(&self.targets, self.coef);
        }
        self.sub += 1;
        if self.sub >= SUB {
            self.sub = 0;
            self.reshape();
        }
    }

    /// The mode switch, one sample on: the dip's gain.
    #[inline]
    fn switching(&mut self) -> f64 {
        let want = Mode::from_value(self.values[Param::Mode as usize]);
        self.switch = match self.switch {
            Switch::Steady if want != self.mode => Switch::Out(1.0),
            Switch::Steady => Switch::Steady,
            Switch::Out(g) if g - self.dip_step > 0.0 => Switch::Out(g - self.dip_step),
            Switch::Out(_) => {
                self.enter(want);
                Switch::In(0.0)
            }
            Switch::In(g) if g + self.dip_step < 1.0 => Switch::In(g + self.dip_step),
            Switch::In(_) => Switch::Steady,
        };
        match self.switch {
            Switch::Steady => 1.0,
            Switch::Out(g) | Switch::In(g) => g,
        }
    }

    /// Start `mode` from the history: the Time engine's analysis made now and
    /// its resonators emptied, or the Spectral engine's frames primed.
    fn enter(&mut self, mode: Mode) {
        self.mode = mode;
        self.delay = latency(mode, self.sample_rate);
        match mode {
            Mode::Time => {
                for ch in &mut self.channels {
                    let mut a = lpc::IDENTITY;
                    self.analysis.analyse(|k| ch.src.at(k), &mut a);
                    ch.flat = Flattener { from: a, to: a };
                    ch.y = [[0.0; 2]; FORMANTS];
                }
                self.lpc_pos = 1 % self.analysis.sizes().hop;
                self.level = [0.0; 2];
            }
            #[cfg(feature = "spectral")]
            Mode::Spectral => {
                self.refresh_vowel();
                for ch in &mut self.channels {
                    self.spectral.prime(&mut ch.spec, &ch.src, self.now.vocal);
                }
            }
            #[cfg(not(feature = "spectral"))]
            Mode::Spectral => {}
        }
    }

    #[cfg(feature = "spectral")]
    fn refresh_vowel(&mut self) {
        if self.vowel_stale {
            self.spectral.set_vowel(&self.full);
            self.vowel_stale = false;
        }
    }

    /// The Time engine, one sample of `count` channels.
    #[inline]
    fn run_time(&mut self, count: usize) -> [f64; 2] {
        let sizes = self.analysis.sizes();
        if self.lpc_pos == 0 {
            for ch in &mut self.channels[..count] {
                let mut a = lpc::IDENTITY;
                self.analysis.analyse(|k| ch.src.at(k), &mut a);
                ch.flat.advance(&a);
            }
        }
        let t = self.lpc_pos as f64 / sizes.hop as f64;
        self.lpc_pos = (self.lpc_pos + 1) % sizes.hop;

        let mut wet = [0.0; 2];
        let (mut dry_power, mut wet_power) = (0.0, 0.0);
        for (ch, w) in self.channels[..count].iter_mut().zip(wet.iter_mut()) {
            let mut x = ch.flat.run(|k| ch.src.at(k), sizes.order, t, &self.powers);
            for (p, y) in self.poles.iter().zip(ch.y.iter_mut()) {
                x = p.run(x, y);
            }
            *w = x;
            let d = ch.dry.at(0);
            dry_power += d * d;
            wet_power += x * x;
        }
        /* Both channels' power, so the correction keeps the stereo image. */
        let c = self.level_coef;
        self.level[0] += c * (dry_power - self.level[0]);
        self.level[1] += c * (wet_power - self.level[1]);
        let gain = if self.level[0] > 1e-20 && self.level[1] > 1e-20 {
            (self.level[0] / self.level[1]).sqrt().clamp(MIN_GAIN, MAX_GAIN)
        } else {
            1.0
        };
        wet.map(|w| w * gain)
    }

    /// The Spectral engine, one sample of `count` channels.
    #[cfg(feature = "spectral")]
    #[inline]
    fn run_spectral(&mut self, count: usize) -> [f64; 2] {
        self.refresh_vowel();
        let mut wet = [0.0; 2];
        for (ch, w) in self.channels[..count].iter_mut().zip(wet.iter_mut()) {
            *w = self.spectral.next(&mut ch.spec, &ch.src, self.now.vocal);
        }
        wet
    }

    /// One frame of `count` channels (1 or 2): the inputs in, the outputs
    /// back.
    #[inline]
    fn tick(&mut self, x: [f64; 2], count: usize) -> [f64; 2] {
        self.advance();
        for (ch, &x) in self.channels[..count].iter_mut().zip(x.iter()) {
            /* A NaN or an infinity from a host would sit in the history for
             * good; it is silence instead. */
            let x = if x.is_finite() { x } else { 0.0 };
            ch.dry.push(x);
            let s = ch.excite.run(x, self.now.excite);
            ch.src.push(s);
        }
        let dip = self.switching();
        let wet = match self.mode {
            Mode::Time => self.run_time(count),
            #[cfg(feature = "spectral")]
            Mode::Spectral => self.run_spectral(count),
            #[cfg(not(feature = "spectral"))]
            Mode::Spectral => self.run_time(count),
        };
        let Glides { mix, output, .. } = self.now;
        let mut out = [0.0; 2];
        for ((o, ch), w) in out.iter_mut().zip(&self.channels[..count]).zip(wet) {
            *o = (ch.dry.at(self.delay) * (1.0 - mix) + w * mix) * output * dip;
        }
        out
    }

    /// One block of host floats, in place: `left`, and `right` for a stereo
    /// track. A right channel shorter than the left is processed as far as
    /// it goes.
    pub fn process_f32(&mut self, left: &mut [f32], mut right: Option<&mut [f32]>) {
        for (i, l) in left.iter_mut().enumerate() {
            match right.as_deref_mut().and_then(|r| r.get_mut(i)) {
                Some(r) => {
                    let [yl, yr] = self.tick([f64::from(*l), f64::from(*r)], 2);
                    *l = yl as f32;
                    *r = yr as f32;
                }
                None => *l = self.tick([f64::from(*l), 0.0], 1)[0] as f32,
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
            let [l, r] = self.tick([f64::from(frame[0]) / SCALE, f64::from(frame[1]) / SCALE], 2);
            frame[0] = to_i16(l);
            frame[1] = to_i16(r);
        }
    }

    /// THE PICTURE, for an editor: at each frequency in `hz`, the left
    /// channel's envelope as it comes in (`input`) and as the engine running
    /// makes it (`output`), in dB, each with its peak at 0. The Time engine's
    /// is its filters' own response -- the analysis, the flattener at its
    /// reach and the resonators at theirs; the Spectral engine's is its last
    /// frame's envelope and the trade made on it. Allocates nothing.
    pub fn envelopes(&self, hz: &[f64], input: &mut [f64], output: &mut [f64]) {
        let fs = self.sample_rate;
        let ch = &self.channels[0];
        match self.mode {
            #[cfg(feature = "spectral")]
            Mode::Spectral => {
                let (n, bins) = (self.spectral.size() as f64, self.spectral.bins());
                let v = self.now.vocal;
                for ((f, i), o) in hz.iter().zip(input.iter_mut()).zip(output.iter_mut()) {
                    let e = ch.spec.envelope_at(f / fs * n, bins);
                    *i = e;
                    *o = e + v * (model::ln_gain(&self.full, *f, fs) - e);
                }
            }
            _ => {
                let order = self.analysis.sizes().order;
                let mut reached = ch.flat.to;
                for (a, p) in reached.iter_mut().zip(self.powers.iter()) {
                    *a *= p;
                }
                for ((f, i), o) in hz.iter().zip(input.iter_mut()).zip(output.iter_mut()) {
                    let w = core::f64::consts::TAU * f / fs;
                    let e = lpc::ln_envelope(&ch.flat.to, order, w);
                    *i = e;
                    *o = e - lpc::ln_envelope(&reached, order, w) + model::ln_gain(&self.poles, *f, fs);
                }
            }
        }
        for curve in [input, output] {
            let peak = curve.iter().cloned().fold(f64::MIN, f64::max);
            for c in curve.iter_mut() {
                *c = (*c - peak) * (20.0 / core::f64::consts::LN_10);
            }
        }
    }
}

#[allow(clippy::neg_cmp_op_on_partial_ord, reason = "a NaN rate must take the default")]
fn usable_rate(sample_rate: f64) -> f64 {
    if !(sample_rate > 0.0) || sample_rate.is_infinite() {
        DEFAULT_SAMPLE_RATE
    } else {
        sample_rate
    }
}

#[cfg(test)]
mod tests;
