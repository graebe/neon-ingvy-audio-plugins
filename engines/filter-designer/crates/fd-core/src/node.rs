// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
One node as it runs: its settings gliding, the cascade they design, and two
instances of it to fade between.

WHAT GLIDES, AND HOW. Freq in octaves, Q in its logarithm, Gain, Ripple and
Stopband in dB -- every [`crate::SUB`] samples, on the engine's control grid,
with the house's 5 ms one-pole (ni_dsp::smooth at the control rate). A
change of any of them designs the cascade again, in place; ni-filter's
sections stay stable while their coefficients step.

WHAT FADES. Type, Family, Order, Channel and On change what the node IS --
how many sections, which outputs, which lanes -- and no glide joins two such
things. So the node has two instances: the one playing (`a`) and, while a
change lands, the one it becomes (`b`), designed from nothing and faded in
over 20 ms on cos² and sin², which sum to one in amplitude as two filters of
one input must. A change that arrives during a fade waits for its end.

A node that is Empty, or switched off, is an instance with no sections: it
costs nothing, and is skipped outright once no fade is running.
*/

use crate::design::{design, prototype, Channel, Globals, Settings, Shape};
use crate::params::{Field, Param, FREQ_DEFAULTS};
use ni_dsp::smooth::glide;
use ni_filter::{Cascade, Family, Prototype, State};

/// How long a change of what a node is takes to fade in, in ms.
pub const FADE_MS: f64 = 20.0;

/// What a node is: what fades rather than glides.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Discrete {
    /// Empty when the node is switched off.
    pub shape: Shape,
    pub family: Family,
    pub order: usize,
    pub channel: Channel,
}

impl Discrete {
    const EMPTY: Discrete = Discrete { shape: Shape::Empty, family: Family::Butterworth, order: 2, channel: Channel::Stereo };

    /// What node `slot`'s values ask for.
    pub fn of(values: &[f64], slot: u8) -> Discrete {
        let v = |f: Field| values[Param::Node(slot, f).index()];
        let on = v(Field::On) >= 0.5;
        let shape = if on { Shape::from_value(v(Field::Type)) } else { Shape::Empty };
        Discrete {
            shape,
            family: Family::from_index(v(Field::Family).round().max(0.0) as usize),
            order: v(Field::Order).round().max(0.0) as usize + 1,
            channel: Channel::from_value(v(Field::Channel)),
        }
    }
}

/// What glides, in the units it glides in.
#[derive(Clone, Copy, Debug, PartialEq)]
struct Glided {
    octaves: f64,
    gain_db: f64,
    ln_q: f64,
    ripple_db: f64,
    stopband_db: f64,
}

impl Glided {
    fn of(values: &[f64], slot: u8) -> Glided {
        let v = |f: Field| values[Param::Node(slot, f).index()];
        Glided {
            octaves: v(Field::Freq).log2(),
            gain_db: v(Field::Gain),
            ln_q: v(Field::Q).ln(),
            ripple_db: v(Field::Ripple),
            stopband_db: v(Field::Stopband),
        }
    }

    fn toward(&mut self, t: &Glided, c: f64) {
        self.octaves = glide(self.octaves, t.octaves, c);
        self.gain_db = glide(self.gain_db, t.gain_db, c);
        self.ln_q = glide(self.ln_q, t.ln_q, c);
        self.ripple_db = glide(self.ripple_db, t.ripple_db, c);
        self.stopband_db = glide(self.stopband_db, t.stopband_db, c);
    }

    fn settings(&self, d: &Discrete) -> Settings {
        Settings {
            shape: d.shape,
            family: d.family,
            order: d.order,
            freq: self.octaves.exp2(),
            gain_db: self.gain_db,
            q: self.ln_q.exp(),
            ripple_db: self.ripple_db,
            stopband_db: self.stopband_db,
        }
    }
}

/// One design running: its cascade and its two lanes' state.
#[derive(Clone, Copy, Debug)]
struct Instance {
    what: Discrete,
    proto: Option<Prototype>,
    /// The ripple and stopband `proto` was made with.
    proto_for: (f64, f64),
    /// What the cascade was designed from, and at which rate.
    designed: Option<(Settings, Globals, f64)>,
    cascade: Cascade,
    lanes: [State; 2],
    clamped: bool,
}

impl Instance {
    fn new(what: Discrete) -> Instance {
        Instance {
            what,
            proto: None,
            proto_for: (f64::NAN, f64::NAN),
            designed: None,
            cascade: Cascade::identity(),
            lanes: [State::default(); 2],
            clamped: false,
        }
    }

    /// Designed again when anything it is made of moved.
    fn update(&mut self, now: &Glided, g: Globals, fs: f64) {
        if self.what.shape == Shape::Empty {
            return;
        }
        let s = now.settings(&self.what);
        if self.designed == Some((s, g, fs)) {
            return;
        }
        if s.shape.uses_family() {
            let uses = (
                if s.family.uses_ripple() { s.ripple_db } else { 0.0 },
                if s.family.uses_stopband() { s.stopband_db } else { 0.0 },
            );
            if self.proto.is_none() || self.proto_for != uses {
                self.proto = prototype(&s);
                self.proto_for = uses;
            }
        }
        let (cascade, clamped) = design(&s, self.proto.as_ref(), g, fs);
        self.cascade = cascade;
        self.clamped = clamped;
        self.designed = Some((s, g, fs));
    }

    #[inline]
    fn run(&mut self, l: f64, r: f64) -> (f64, f64) {
        let c = &self.cascade;
        let [a, b] = &mut self.lanes;
        match self.what.channel {
            Channel::Stereo => (c.run(a, l), c.run(b, r)),
            Channel::Left => (c.run(a, l), r),
            Channel::Right => (l, c.run(b, r)),
            Channel::Mid => {
                let (m, s) = (0.5 * (l + r), 0.5 * (l - r));
                let m = c.run(a, m);
                (m + s, m - s)
            }
            Channel::Side => {
                let (m, s) = (0.5 * (l + r), 0.5 * (l - r));
                let s = c.run(b, s);
                (m + s, m - s)
            }
        }
    }

    fn reset(&mut self) {
        for l in &mut self.lanes {
            l.reset();
        }
    }

    fn flush(&mut self) {
        for l in &mut self.lanes {
            l.flush(1e-30);
        }
    }
}

/// One node.
#[derive(Clone, Copy, Debug)]
pub struct Node {
    slot: u8,
    target: Glided,
    now: Glided,
    a: Instance,
    b: Instance,
    /// How far `b` has faded in, 0..1, while `fading`.
    t: f64,
    fading: bool,
}

impl Node {
    pub fn new(slot: u8) -> Node {
        let mut values = [0.0; crate::PARAM_COUNT];
        for (i, v) in values.iter_mut().enumerate() {
            *v = Param::from_index(i).map_or(0.0, Param::default_value);
        }
        values[Param::Node(slot, Field::Freq).index()] = FREQ_DEFAULTS[slot as usize];
        let g = Glided::of(&values, slot);
        Node {
            slot,
            target: g,
            now: g,
            a: Instance::new(Discrete::EMPTY),
            b: Instance::new(Discrete::EMPTY),
            t: 0.0,
            fading: false,
        }
    }

    /// Nothing to do: Empty or off, no fade running, and none asked for.
    pub fn is_idle(&self, values: &[f64]) -> bool {
        !self.fading && self.a.what.shape == Shape::Empty && Discrete::of(values, self.slot).shape == Shape::Empty
    }

    /// One control step: the targets read, the glides moved, a fade started
    /// when what the node is changed, and the instances designed again.
    pub fn control(&mut self, values: &[f64], c: f64, g: Globals, fs: f64) {
        self.target = Glided::of(values, self.slot);
        let want = Discrete::of(values, self.slot);
        if !self.fading && want != self.a.what {
            /* From nothing, nothing was heard of the glides: they start where
             * they are going. */
            if self.a.what.shape == Shape::Empty {
                self.now = self.target;
            }
            self.b = Instance::new(want);
            self.t = 0.0;
            self.fading = true;
        } else {
            self.now.toward(&self.target, c);
        }
        self.a.update(&self.now, g, fs);
        if self.fading {
            self.b.update(&self.now, g, fs);
        }
    }

    /// `l` and `r` through the node, in place; `step` is a fade's progress a
    /// sample.
    pub fn run(&mut self, l: &mut [f64], r: &mut [f64], step: f64) {
        let n = l.len().min(r.len());
        let mut i = 0;
        while self.fading && i < n {
            let (al, ar) = self.a.run(l[i], r[i]);
            let (bl, br) = self.b.run(l[i], r[i]);
            let s = (core::f64::consts::FRAC_PI_2 * self.t).sin();
            let (wa, wb) = (1.0 - s * s, s * s);
            l[i] = wa * al + wb * bl;
            r[i] = wa * ar + wb * br;
            self.t += step;
            if self.t >= 1.0 {
                self.land();
            }
            i += 1;
        }
        /* Steady, or a fade that landed part way: the rest on `a` alone. */
        if self.a.what.shape == Shape::Empty {
            return;
        }
        for (x, y) in l[i..n].iter_mut().zip(r[i..n].iter_mut()) {
            (*x, *y) = self.a.run(*x, *y);
        }
    }

    fn land(&mut self) {
        self.a = self.b;
        self.b = Instance::new(Discrete::EMPTY);
        self.fading = false;
        self.t = 0.0;
    }

    /// The panic: a fade lands, every lane empties, every glide arrives.
    pub fn reset(&mut self, values: &[f64], g: Globals, fs: f64) {
        if self.fading {
            self.land();
        }
        self.target = Glided::of(values, self.slot);
        self.now = self.target;
        let want = Discrete::of(values, self.slot);
        if want != self.a.what {
            self.a = Instance::new(want);
        }
        self.a.reset();
        self.a.designed = None;
        self.a.update(&self.now, g, fs);
    }

    pub fn flush(&mut self) {
        self.a.flush();
        if self.fading {
            self.b.flush();
        }
    }

    /// Whether the rate clamps one of the playing design's edges.
    pub fn clamped(&self) -> bool {
        if self.fading {
            self.b.clamped
        } else {
            self.a.clamped
        }
    }

    pub fn is_fading(&self) -> bool {
        self.fading
    }

    /// Whether every lane is empty: what a test of the panic asks.
    pub fn is_silent(&self) -> bool {
        self.a.lanes.iter().all(State::is_empty) && self.b.lanes.iter().all(State::is_empty)
    }

    /// The design playing (or, during a fade, the one fading in).
    pub fn cascade(&self) -> &Cascade {
        if self.fading {
            &self.b.cascade
        } else {
            &self.a.cascade
        }
    }
}
