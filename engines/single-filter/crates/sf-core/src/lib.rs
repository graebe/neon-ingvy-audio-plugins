// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The single-filter engine: ONE NI Filter Designer filter as a product of its
own -- NI High Pass, NI Low Pass, NI Band Pass, NI Notch, NI Bell, NI Low
Shelf, NI High Shelf, NI Tilt, NI All Pass.

```text
  in ── the filter ── Output ── out
        in Stereo, Left, Right, Mid or Side
```

THE FILTER IS FILTER DESIGNER'S NODE (ni-filter-node): the same design, the
same glides, morphs and fades, the same budget -- so a single filter sounds,
sample for sample, as a node of that type does. What it is is fixed when the
engine is made ([`Engine::new`]); its parameters are the ones that type uses
(params.rs).

THE CONTROL GRID. Every [`SUB`] samples, counted from the engine's first,
the glides move and the cascade is designed again where anything moved.
Counted from the first sample, not from the block, so a render is the same
bits at any block size.

NOTHING CLICKS. Numbers glide; what the filter is fades over 20 ms; Output
glides every sample. Minimum phase, no latency.

THE PANIC. CC 120 (All Sound Off) and CC 123 (All Notes Off), on any channel,
reset the engine: every lane empties, every fade lands, every glide arrives;
the settings stay. The Move's host has no other way to say it.

THE AUDIO THREAD ALLOCATES NOTHING: every buffer is made in `new`
(tests/no_alloc.rs).
*/

pub mod params;

use core::fmt::Write;
use ni_analyzer::Rings;
use ni_dsp::fmt::Buf;
use ni_dsp::smooth::{coef, glide};
use ni_filter::Family;
use ni_filter_node::node::{Budget, Node, FADE_MS};
use ni_filter_node::{Channel, Globals, Settings, Target};
pub use ni_analyzer::{Lane, RING};
pub use ni_filter_node::{Shape, SUB};
pub use params::{Field, Info, FIELDS, SHAPES};

/// The sample rate when none is given.
pub const DEFAULT_SAMPLE_RATE: f64 = 44_100.0;

/// The longest tail [`tail_seconds`] reports.
pub const TAIL_MAX_SECONDS: f64 = 30.0;

fn usable_rate(sample_rate: f64) -> f64 {
    if sample_rate.is_finite() && sample_rate > 0.0 {
        sample_rate.clamp(8_000.0, 768_000.0)
    } else {
        DEFAULT_SAMPLE_RATE
    }
}

/// The filter `values` (by [`Field::slot`]) ask `shape` to play: always on.
pub fn target_of(shape: Shape, values: &[f64; FIELDS]) -> Target {
    let v = |f: Field| values[f.slot()];
    Target {
        settings: Settings {
            shape,
            family: Family::from_index(v(Field::Family).round().max(0.0) as usize),
            order: v(Field::Order).round().max(0.0) as usize + 1,
            freq: v(Field::Freq),
            gain_db: v(Field::Gain),
            q: v(Field::Q),
            ripple_db: v(Field::Ripple),
            stopband_db: v(Field::Stopband),
            matched: v(Field::Matched) >= 0.5 && shape.uses_matched(),
        },
        channel: Channel::from_value(v(Field::Channel)),
        on: true,
    }
}

/// Adaptive Q as `values` hold it, 0 or 1: a Bell's only, and it glides.
fn adaptive_of(values: &[f64; FIELDS]) -> f64 {
    if values[Field::AdaptiveQ.slot()] >= 0.5 {
        1.0
    } else {
        0.0
    }
}

/// THE TAIL of `shape` at `values` and `fs`: the seconds the output goes on
/// once the input stops, until it is 60 dB down. An upper bound, at most
/// [`TAIL_MAX_SECONDS`]. Pure; not the audio thread.
pub fn tail_seconds(shape: Shape, values: &[f64; FIELDS], fs: f64) -> f64 {
    if !fs.is_finite() || fs <= 0.0 || shape == Shape::Empty {
        return 0.0;
    }
    let g = Globals { scale: 1.0, adaptive: adaptive_of(values) };
    let decay = ni_filter_node::design::decay_samples(&target_of(shape, values).settings, g, fs, 60.0);
    (decay / fs).min(TAIL_MAX_SECONDS)
}

/// The engine.
pub struct Engine {
    shape: Shape,
    sample_rate: f64,
    values: [f64; FIELDS],
    node: Node,
    /// Adaptive Q (0..1), gliding on the control grid.
    globals: Globals,
    /// Output, linear, gliding every sample.
    out: f64,
    ctl_coef: f64,
    sample_coef: f64,
    fade_step: f64,
    /// Where in the control grid the next sample is.
    phase: usize,
    /// The block is a mono track's: the right lane is the left's copy.
    mono: bool,
    /// The input and output, in its lane, while a window shows the analyzer.
    rings: Rings,
    buf_l: [f64; SUB],
    buf_r: [f64; SUB],
}

impl Engine {
    /// A new engine for `shape` at `sample_rate` (44.1 kHz if that is not
    /// positive), every parameter at its default. `None` for Empty, which is
    /// no product. Allocates: not the audio thread.
    pub fn new(shape: Shape, sample_rate: f64) -> Option<Engine> {
        if shape == Shape::Empty {
            return None;
        }
        let values = params::defaults(shape);
        let mut e = Engine {
            shape,
            sample_rate: usable_rate(sample_rate),
            values,
            node: Node::new(&target_of(shape, &values)),
            globals: Globals::NEUTRAL,
            out: 1.0,
            ctl_coef: 0.0,
            sample_coef: 0.0,
            fade_step: 0.0,
            phase: 0,
            mono: false,
            rings: Rings::new(),
            buf_l: [0.0; SUB],
            buf_r: [0.0; SUB],
        };
        e.rates();
        e.reset();
        Some(e)
    }

    fn rates(&mut self) {
        let fs = self.sample_rate;
        self.ctl_coef = coef(fs / SUB as f64);
        self.sample_coef = coef(fs);
        self.fade_step = 1000.0 / (FADE_MS * fs);
    }

    pub fn shape(&self) -> Shape {
        self.shape
    }

    pub fn sample_rate(&self) -> f64 {
        self.sample_rate
    }

    /// The host's rate: the design again, and what was held lets go.
    pub fn set_sample_rate(&mut self, sample_rate: f64) {
        let fs = usable_rate(sample_rate);
        if fs == self.sample_rate {
            return;
        }
        self.sample_rate = fs;
        self.rates();
        self.reset();
    }

    /// The panic: every lane empties, every fade lands, every glide arrives.
    /// The settings stay.
    pub fn reset(&mut self) {
        self.globals = Globals { scale: 1.0, adaptive: adaptive_of(&self.values) };
        self.node.reset(&self.target(), self.globals, self.sample_rate);
        self.out = self.out_target();
    }

    /// One MIDI message. Only the panic -- CC 120 and CC 123, on any
    /// channel -- does anything: it resets.
    pub fn on_midi(&mut self, msg: &[u8]) {
        use wmidi::{ControlFunction, MidiMessage};
        if let Ok(MidiMessage::ControlChange(_, ControlFunction::ALL_SOUND_OFF | ControlFunction::ALL_NOTES_OFF, _)) =
            MidiMessage::try_from(msg)
        {
            self.reset();
        }
    }

    /// `v` in the field's own unit, clamped to its range; it takes effect at
    /// the next control step. NaN, and a field this product does not have,
    /// change nothing.
    pub fn set(&mut self, field: Field, v: f64) {
        if !v.is_nan() && params::index_of(self.shape, field).is_some() {
            self.values[field.slot()] = params::info(self.shape, field).kind.clamp(v);
        }
    }

    /// A field's value as set (its default for one the product lacks).
    pub fn get(&self, field: Field) -> f64 {
        self.values[field.slot()]
    }

    /// Every value as set, by [`Field::slot`].
    pub fn values(&self) -> &[f64; FIELDS] {
        &self.values
    }

    /// The text door, for the Move's `set_param`: a key and the value as
    /// text. `panic` is the host's panic. False for a key it does not own.
    pub fn set_param(&mut self, key: &str, val: &str) -> bool {
        if key == "panic" {
            self.reset();
            return true;
        }
        let Some(field) = params::from_key(self.shape, key) else { return false };
        if let Some(v) = params::parse_value(self.shape, field, val) {
            self.set(field, v);
        }
        true
    }

    /// A parameter's value by its key, as a number at full precision, into
    /// `out` (NUL-terminated); its length, or -1 for a key it does not serve.
    pub fn get_param(&self, key: &str, out: &mut [u8]) -> i32 {
        let Some(field) = params::from_key(self.shape, key) else { return -1 };
        let mut b = Buf::new(out);
        if write!(b, "{}", self.get(field)).is_err() {
            return -1;
        }
        b.finish()
    }

    fn target(&self) -> Target {
        target_of(self.shape, &self.values)
    }

    fn out_target(&self) -> f64 {
        10f64.powf(self.values[Field::Output.slot()] / 20.0)
    }

    /// Whether a window shows the analyzer: the rings are kept only then.
    pub fn set_watching(&mut self, on: bool) {
        self.rings.set_watching(on);
    }

    /// Which lane the analyzer's rings keep. A new lane starts from empty
    /// rings.
    pub fn set_lane(&mut self, lane: Lane) {
        self.rings.set_lane(lane);
    }

    /// One control tick: Adaptive Q glides, the filter steps toward what
    /// the values ask for. True when its design waited for budget, which a
    /// single filter -- one node against a tick's budget for four -- never
    /// does.
    fn control(&mut self) -> bool {
        let c = self.ctl_coef;
        self.globals.adaptive = glide(self.globals.adaptive, adaptive_of(&self.values), c);
        let mut budget = Budget::TICK;
        self.node.control(&self.target(), c, self.globals, self.sample_rate, &mut budget)
    }

    /// One block of host floats, in place: `left`, and `right` for a stereo
    /// track (a mono one is the left lane, worked out once: the right and
    /// the side are what it does not play).
    pub fn process_f32(&mut self, left: &mut [f32], mut right: Option<&mut [f32]>) {
        let n = left.len();
        self.mono = right.is_none();
        let mut i = 0;
        while i < n {
            if self.phase == 0 {
                self.control();
            }
            let m = (SUB - self.phase).min(n - i);
            for k in 0..m {
                let l = f64::from(left[i + k]);
                let r = right.as_deref().and_then(|r| r.get(i + k)).map_or(l, |&r| f64::from(r));
                self.buf_l[k] = if l.is_finite() { l } else { 0.0 };
                self.buf_r[k] = if r.is_finite() { r } else { 0.0 };
            }
            self.block(m);
            for k in 0..m {
                left[i + k] = self.buf_l[k] as f32;
                if let Some(r) = right.as_deref_mut().and_then(|r| r.get_mut(i + k)) {
                    *r = self.buf_r[k] as f32;
                }
            }
            i += m;
            self.phase = (self.phase + m) % SUB;
        }
        self.node.flush();
    }

    /// One block heard and left alone: what plays is the input, so the rings
    /// keep it as both what came in and what went out, and the control grid
    /// keeps its time. What a bypassed plugin runs once its engine idles.
    /// Nothing is filtered or glided; a caller that filters again afterwards
    /// resets first.
    pub fn listen_f32(&mut self, left: &[f32], right: Option<&[f32]>) {
        self.rings.listen(left, right);
        self.phase = (self.phase + left.len()) % SUB;
    }

    /// One block of the Move's interleaved 16-bit stereo, in place.
    pub fn process_i16(&mut self, buf: &mut [i16], frames: usize) {
        const SCALE: f64 = 32_768.0;
        let to_i16 = |y: f64| (y * SCALE).round().clamp(-SCALE, SCALE - 1.0) as i16;
        let frames = frames.min(buf.len() / 2);
        self.mono = false;
        let mut i = 0;
        while i < frames {
            if self.phase == 0 {
                self.control();
            }
            let m = (SUB - self.phase).min(frames - i);
            for k in 0..m {
                self.buf_l[k] = f64::from(buf[2 * (i + k)]) / SCALE;
                self.buf_r[k] = f64::from(buf[2 * (i + k) + 1]) / SCALE;
            }
            self.block(m);
            for k in 0..m {
                buf[2 * (i + k)] = to_i16(self.buf_l[k]);
                buf[2 * (i + k) + 1] = to_i16(self.buf_r[k]);
            }
            i += m;
            self.phase = (self.phase + m) % SUB;
        }
        self.node.flush();
    }

    /// `m` samples of the buffers through the filter and Output.
    fn block(&mut self, m: usize) {
        self.rings.input(&self.buf_l[..m], &self.buf_r[..m]);
        let target = self.out_target();
        let (l, r) = (&mut self.buf_l[..m], &mut self.buf_r[..m]);
        self.node.run(l, r, self.phase, self.fade_step, self.mono);
        for (x, y) in l.iter_mut().zip(r.iter_mut()) {
            self.out = glide(self.out, target, self.sample_coef);
            *x *= self.out;
            *y *= self.out;
        }
        self.rings.output(&self.buf_l[..m], &self.buf_r[..m]);
    }

    /// The analyzer's rings, oldest first, into `pre` and `post` (as many as
    /// they hold, up to [`RING`]).
    pub fn rings(&self, pre: &mut [f32], post: &mut [f32]) {
        self.rings.copy_out(pre, post);
    }

    /// The filter.
    pub fn node(&self) -> &Node {
        &self.node
    }

    /// Whether every lane is empty: what a test of the panic asks.
    pub fn is_silent(&self) -> bool {
        self.node.is_silent()
    }
}

#[cfg(test)]
mod tests;
