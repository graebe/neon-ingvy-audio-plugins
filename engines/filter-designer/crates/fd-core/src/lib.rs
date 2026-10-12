// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
NI Filter Designer's engine: a filter design tool you play as an equaliser.

```text
  in ── isolator ── node 1 ── node 2 ── … ── node 16 ── Output ── out
        (optional)  each in Stereo, Left, Right, Mid or Side
```

SIXTEEN NODES, each a Bell, a shelf, a cut, a band pass, a notch, an all-pass
or a tilt -- the cuts, bands and notches in six families at orders 1 to 8,
with resonance, ripple and stopband (ni-filter; design.rs says what each
type uses). In front of them an ISOLATOR, a DJ mixer's three bands with
their kills (isolator.rs). Minimum phase, no latency.

THE CONTROL GRID. Every [`SUB`] samples, counted from the engine's first,
the nodes' and the crossovers' glides move and their cascades are designed
again where anything moved (node.rs). Counted from the first sample, not
from the block, so a render is the same bits at any block size.

NOTHING CLICKS. Numbers glide; what a node or the isolator IS fades over 20
ms between two designs; the isolator's levels and Output glide every sample.

AN EMPTY INSTANCE IS BIT-EXACT: every node Empty or off, the isolator off,
Output at 0 dB -- the block is not touched.

THE PICTURE is the shell's (fd-capi): it designs the curves from the host's
values with [`design::design`], the function the engine runs, and reads the
analyzer from the two rings kept here while a window shows them.

THE AUDIO THREAD ALLOCATES NOTHING: every buffer is made in `new`
(tests/no_alloc.rs holds it to that).
*/

pub mod design;
pub mod isolator;
pub mod node;
pub mod params;

use core::fmt::Write;
use design::Globals;
use isolator::Isolator;
use ni_dsp::fmt::Buf;
use ni_dsp::smooth::{coef, glide};
use node::{Node, FADE_MS};
pub use params::{Field, Info, Kind, Param, Unit, NODES, NODE_BASE, NODE_FIELDS, PARAM_COUNT};

/// The sample rate when none is given.
pub const DEFAULT_SAMPLE_RATE: f64 = 44_100.0;

/// Samples between control steps.
pub const SUB: usize = 16;

/// Samples each analyzer ring keeps: a power of two.
pub const RING: usize = 8192;

fn usable_rate(sample_rate: f64) -> f64 {
    if sample_rate.is_finite() && sample_rate > 0.0 {
        sample_rate.clamp(8_000.0, 768_000.0)
    } else {
        DEFAULT_SAMPLE_RATE
    }
}

/// The default of every parameter, in table order.
pub fn defaults() -> [f64; PARAM_COUNT] {
    core::array::from_fn(|i| Param::from_index(i).map_or(0.0, Param::default_value))
}

/// The engine.
pub struct Engine {
    sample_rate: f64,
    values: [f64; PARAM_COUNT],
    nodes: [Node; NODES],
    iso: Isolator,
    /// Scale (a factor) and Adaptive Q (0..1), gliding on the control grid.
    globals: Globals,
    /// Output, linear, gliding every sample.
    out: f64,
    ctl_coef: f64,
    sample_coef: f64,
    fade_step: f64,
    /// Where in the control grid the next sample is.
    phase: usize,
    /// The input and output, mono, while a window shows the analyzer.
    watching: bool,
    pre: Box<[f32; RING]>,
    post: Box<[f32; RING]>,
    ring_pos: usize,
    buf_l: [f64; SUB],
    buf_r: [f64; SUB],
}

impl Engine {
    /// A new engine at `sample_rate` (44.1 kHz if that is not positive),
    /// every parameter at its default. Allocates: not the audio thread.
    pub fn new(sample_rate: f64) -> Engine {
        let fs = usable_rate(sample_rate);
        let values = defaults();
        let mut e = Engine {
            sample_rate: fs,
            values,
            nodes: core::array::from_fn(|i| Node::new(i as u8)),
            iso: Isolator::new(&values),
            globals: Globals::NEUTRAL,
            out: 1.0,
            ctl_coef: 0.0,
            sample_coef: 0.0,
            fade_step: 0.0,
            phase: 0,
            watching: false,
            pre: Box::new([0.0; RING]),
            post: Box::new([0.0; RING]),
            ring_pos: 0,
            buf_l: [0.0; SUB],
            buf_r: [0.0; SUB],
        };
        e.rates();
        e.reset();
        e
    }

    fn rates(&mut self) {
        let fs = self.sample_rate;
        self.ctl_coef = coef(fs / SUB as f64);
        self.sample_coef = coef(fs);
        self.fade_step = 1000.0 / (FADE_MS * fs);
    }

    pub fn sample_rate(&self) -> f64 {
        self.sample_rate
    }

    /// The host's rate: every design again, and what was held lets go.
    pub fn set_sample_rate(&mut self, sample_rate: f64) {
        let fs = usable_rate(sample_rate);
        if fs == self.sample_rate {
            return;
        }
        self.sample_rate = fs;
        self.rates();
        self.reset();
    }

    /// The panic: every filter empties, every fade lands, every glide
    /// arrives. The settings stay.
    pub fn reset(&mut self) {
        self.globals = self.global_targets();
        for n in &mut self.nodes {
            n.reset(&self.values, self.globals, self.sample_rate);
        }
        self.iso.reset(&self.values, self.sample_rate);
        self.out = self.out_target();
    }

    /// One MIDI message. Only the panic -- CC 120 (All Sound Off) and CC 123
    /// (All Notes Off), on any channel -- does anything: it resets.
    pub fn on_midi(&mut self, msg: &[u8]) {
        use wmidi::{ControlFunction, MidiMessage};
        if let Ok(MidiMessage::ControlChange(_, ControlFunction::ALL_SOUND_OFF | ControlFunction::ALL_NOTES_OFF, _)) =
            MidiMessage::try_from(msg)
        {
            self.reset();
        }
    }

    /// `v` in the parameter's own unit, clamped to its range; it takes
    /// effect at the next control step. NaN changes nothing.
    pub fn set_num(&mut self, p: Param, v: f64) {
        if !v.is_nan() {
            self.values[p.index()] = p.clamp(v);
        }
    }

    pub fn num(&self, p: Param) -> f64 {
        self.values[p.index()]
    }

    /// A parameter's value by its key, as set; `None` for a key it does not
    /// own.
    pub fn num_by_key(&self, key: &str) -> Option<f64> {
        Param::from_key(key).map(|p| self.num(p))
    }

    /// Every value as set, in table order.
    pub fn values(&self) -> &[f64; PARAM_COUNT] {
        &self.values
    }

    /// The text door, for the Move's `set_param`: a key and the value as
    /// text. `panic` is the host's panic. False for a key it does not own.
    pub fn set_param(&mut self, key: &str, val: &str) -> bool {
        if key == "panic" {
            self.reset();
            return true;
        }
        let Some(p) = Param::from_key(key) else { return false };
        if let Some(v) = p.parse_value(val) {
            self.set_num(p, v);
        }
        true
    }

    /// A parameter's value by its key, as a number at full precision, into
    /// `out` (NUL-terminated); its length, or -1 for a key it does not serve.
    pub fn get_param(&self, key: &str, out: &mut [u8]) -> i32 {
        let Some(p) = Param::from_key(key) else { return -1 };
        let mut b = Buf::new(out);
        if write!(b, "{}", self.num(p)).is_err() {
            return -1;
        }
        b.finish()
    }

    fn global_targets(&self) -> Globals {
        design::globals_of(&self.values)
    }

    fn out_target(&self) -> f64 {
        10f64.powf(self.values[Param::Output.index()] / 20.0)
    }

    /// Whether a window shows the analyzer: the rings are kept only then.
    pub fn set_watching(&mut self, on: bool) {
        self.watching = on;
    }

    /// Nothing to do: every node idle, the isolator off and faded out, and
    /// Output at unity and staying there.
    pub fn is_transparent(&self) -> bool {
        self.out == 1.0
            && self.values[Param::Output.index()] == 0.0
            && self.iso.is_idle(&self.values)
            && self.nodes.iter().all(|n| n.is_idle(&self.values))
    }

    fn control(&mut self) {
        let t = self.global_targets();
        let c = self.ctl_coef;
        self.globals.scale = glide(self.globals.scale, t.scale, c);
        self.globals.adaptive = glide(self.globals.adaptive, t.adaptive, c);
        for n in &mut self.nodes {
            n.control(&self.values, c, self.globals, self.sample_rate);
        }
        self.iso.control(&self.values, c, self.sample_rate);
    }

    /// One block of host floats, in place: `left`, and `right` for a stereo
    /// track (a mono one is the left lane twice, the left kept).
    pub fn process_f32(&mut self, left: &mut [f32], mut right: Option<&mut [f32]>) {
        let n = left.len();
        if self.is_transparent() {
            if self.watching {
                for i in 0..n {
                    let x = match right.as_deref() {
                        Some(r) if i < r.len() => 0.5 * (left[i] + r[i]),
                        _ => left[i],
                    };
                    self.keep(x, x);
                }
            }
            self.phase = (self.phase + n) % SUB;
            return;
        }
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
        self.flush();
    }

    /// One block of the Move's interleaved 16-bit stereo, in place.
    pub fn process_i16(&mut self, buf: &mut [i16], frames: usize) {
        const SCALE: f64 = 32_768.0;
        let to_i16 = |y: f64| (y * SCALE).round().clamp(-SCALE, SCALE - 1.0) as i16;
        let frames = frames.min(buf.len() / 2);
        if self.is_transparent() {
            self.phase = (self.phase + frames) % SUB;
            return;
        }
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
        self.flush();
    }

    /// `m` samples of the buffers through everything.
    fn block(&mut self, m: usize) {
        if self.watching {
            for k in 0..m {
                self.pre[(self.ring_pos + k) & (RING - 1)] = (0.5 * (self.buf_l[k] + self.buf_r[k])) as f32;
            }
        }
        let target = self.out_target();
        let (l, r) = (&mut self.buf_l[..m], &mut self.buf_r[..m]);
        self.iso.run(l, r, self.sample_coef, self.fade_step);
        for node in &mut self.nodes {
            node.run(l, r, self.fade_step);
        }
        for (x, y) in l.iter_mut().zip(r.iter_mut()) {
            self.out = glide(self.out, target, self.sample_coef);
            *x *= self.out;
            *y *= self.out;
        }
        if self.watching {
            for k in 0..m {
                self.post[(self.ring_pos + k) & (RING - 1)] = (0.5 * (self.buf_l[k] + self.buf_r[k])) as f32;
            }
            self.ring_pos = (self.ring_pos + m) & (RING - 1);
        }
    }

    fn keep(&mut self, pre: f32, post: f32) {
        self.pre[self.ring_pos] = pre;
        self.post[self.ring_pos] = post;
        self.ring_pos = (self.ring_pos + 1) & (RING - 1);
    }

    fn flush(&mut self) {
        for n in &mut self.nodes {
            n.flush();
        }
        self.iso.flush();
    }

    /// The analyzer's rings, oldest first, into `pre` and `post` (as many as
    /// they hold, up to [`RING`]).
    pub fn rings(&self, pre: &mut [f32], post: &mut [f32]) {
        for (k, (a, b)) in pre.iter_mut().zip(post.iter_mut()).take(RING).enumerate() {
            let at = (self.ring_pos + k) & (RING - 1);
            *a = self.pre[at];
            *b = self.post[at];
        }
    }

    /// Node `slot`.
    pub fn node(&self, slot: usize) -> &Node {
        &self.nodes[slot]
    }

    pub fn isolator(&self) -> &Isolator {
        &self.iso
    }

    /// Whether every filter's state is empty: what a test of the panic asks.
    pub fn is_silent(&self) -> bool {
        self.nodes.iter().all(Node::is_silent) && self.iso.is_silent()
    }
}

#[cfg(test)]
mod tests;
