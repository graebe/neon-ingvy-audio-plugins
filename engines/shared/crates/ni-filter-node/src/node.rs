// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
One node as it runs: its settings gliding, the cascade they design, and two
instances of it to fade between. A node reads what it is asked to play as a
[`Target`]; how an engine's parameters spell that is the engine's business.

WHAT GLIDES, AND HOW. Freq in octaves, Q in its logarithm, Gain, Ripple and
Stopband in dB -- every [`SUB`] samples, on the engine's control grid,
with the house's 5 ms one-pole (ni_dsp::smooth at the control rate). A
change of any of them designs the cascade again, and across the next
sub-block the sections move from the last design to the new one a sample at
a time (Cascade::run_toward), arriving on its last sample: no coefficient
steps, so a fast sweep of a narrow filter puts no spurs at multiples of
fs/16.

WHAT MORPHS. Family and Matched Cuts change how a node does what it does,
section for section, and a design that crosses from one realisation of a
section to another (ni-filter's matched designs, Section::form) changes it
too: no glide of one sub-block joins two such designs, and summing them
would dip where they disagree in phase. So the playing instance MORPHS: its
cascade, in second-order form, moves from the design it was to the one it
becomes over FADE_MS (Cascade::morph), the filters' state carried through, a
sample at a time.

WHAT FADES. Type, Order, Channel and On change what the node IS -- another
kind of filter, another number of sections, other lanes: a morph would have
to grow or drop a section, and a section turned from a wire into a cut
passes a notch on its way -- so the node has two instances: the one playing
(`a`) and, while a change lands, the one it becomes (`b`), faded in over 20
ms on cos² and sin², which sum to one in amplitude as two filters of one
input must. `b` first runs on the input UNHEARD until its filters have
settled -- its 40 dB decay, at most WARM_MS -- so it fades in as it sounds
rather than from silence: a narrow, low design rings far longer than any
fade, and from nothing it would dip and swell back over that time. A change
that arrives during a fade waits for its end.

A node that is Empty, or switched off, is an instance with no sections: it
costs nothing, and is skipped outright once no fade is running.

WHAT A TICK MAY COST. Mapping a prototype to sections is cheap; designing the
prototype (an elliptic one, a Chebyshev's -3 dB point) is not, and neither
is a design high in the band that weighs its realisations (a shelf's, a
matched cut's: up to four candidates a section, each held to the analog
curve on a grid). So the engine hands every control tick a [`Budget`]: of
[`PROTOS_PER_TICK`] prototypes and [`WEIGHS_PER_TICK`] weighed designs. A
fade or a morph that needs a new prototype starts on a tick with budget
left. While Ripple or Stopband glide, the prototype follows every
[`PROTO_EVERY`]th tick and on the tick they land, budget allowing; between
them the sections follow Freq and Q on the prototype they have. A design
that would weigh waits for a tick with budget for it, and when it comes, the
move to it is spread over as many sub-blocks as it waited: a glide that is
designed every fourth tick moves a sample at a time all the same, and puts no
lines at a quarter of the control rate. An engine with several nodes serves
them in turn ([`control_in_turn`]), so a tick's budget goes round.
*/

use crate::design::{design, prototype, weighs, Channel, Globals, Settings, Shape, Target};
use ni_dsp::smooth::glide;
use ni_filter::{Cascade, Family, Prototype, State};

/// Samples between control steps: the grid every engine counts from its
/// first sample, not from the block, so a render is the same bits at any
/// block size.
pub const SUB: usize = 16;

/// How long a change of what a node is takes to fade in, in ms.
pub const FADE_MS: f64 = 20.0;

/// How far a design fading in has settled before it is heard: its free
/// response this many dB down.
pub const WARM_DB: f64 = 40.0;

/// The longest a design runs unheard before it fades in, in ms.
pub const WARM_MS: f64 = 500.0;

/// Samples a design whose response takes `decay` samples to settle runs
/// unheard, at `fs`: at most WARM_MS.
pub fn warm_samples(decay: f64, fs: f64) -> usize {
    decay.clamp(0.0, WARM_MS * fs / 1000.0).ceil() as usize
}

/// 0 to 1 and back without a corner: 3x² - 2x³.
fn ease(x: f64) -> f64 {
    x * x * (3.0 - 2.0 * x)
}

/// The prototype designs one control tick may make, across all of an
/// engine's nodes.
pub const PROTOS_PER_TICK: usize = 4;

/// The designs that weigh their realisations one control tick may make,
/// across all of an engine's nodes (design::weighs).
pub const WEIGHS_PER_TICK: usize = 4;

/// What one control tick may still spend (see the module notes).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Budget {
    pub protos: usize,
    pub weighs: usize,
}

impl Budget {
    /// A tick's.
    pub const TICK: Budget = Budget { protos: PROTOS_PER_TICK, weighs: WEIGHS_PER_TICK };
    /// The panic's: everything at once.
    pub const UNBOUNDED: Budget = Budget { protos: usize::MAX, weighs: usize::MAX };
}

/// While Ripple or Stopband glide, the prototype is designed again every this
/// many control ticks (64 samples), and on the tick they land.
pub const PROTO_EVERY: u8 = 4;

/// What a node is: what fades rather than glides.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Discrete {
    /// Empty when the node is switched off.
    pub shape: Shape,
    pub family: Family,
    pub order: usize,
    pub channel: Channel,
    /// A matched high-pass or low-pass (Matched Cuts): another design, so a
    /// flip fades.
    pub matched: bool,
}

impl Discrete {
    const EMPTY: Discrete = Discrete { shape: Shape::Empty, family: Family::Butterworth, order: 2, channel: Channel::Stereo, matched: false };

    /// What `t` asks for.
    pub fn of(t: &Target) -> Discrete {
        let s = &t.settings;
        Discrete {
            shape: if t.is_heard() { s.shape } else { Shape::Empty },
            family: s.family,
            order: s.order,
            channel: t.channel,
            matched: s.matched,
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
    fn of(t: &Target) -> Glided {
        let s = &t.settings;
        Glided {
            octaves: s.freq.log2(),
            gain_db: s.gain_db,
            ln_q: s.q.ln(),
            ripple_db: s.ripple_db,
            stopband_db: s.stopband_db,
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
            matched: d.matched,
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
    /// Control ticks since `proto` was made.
    proto_age: u8,
    /// What the cascade was designed from, and at which rate.
    designed: Option<(Settings, Globals, f64)>,
    cascade: Cascade,
    /// The design the move starts from, while `moving` toward `cascade`.
    from: Cascade,
    moving: bool,
    /// The sub-blocks the move spans -- one, or as many as a weighed design
    /// waited -- and how many of them have passed.
    span: u32,
    into: u32,
    /// Ticks a weighed design has waited for budget.
    waited: u32,
    /// The settings' design, in second-order form: where a morph goes, and
    /// what a realisation switch is told by.
    goal: Cascade,
    /// A morph under way: the cascade it left and how far it has come, 0..1.
    morph: Option<(Cascade, f64)>,
    lanes: [State; 2],
    clamped: bool,
}

impl Instance {
    fn new(what: Discrete) -> Instance {
        Instance {
            what,
            proto: None,
            proto_for: (f64::NAN, f64::NAN),
            proto_age: 0,
            designed: None,
            cascade: Cascade::identity(),
            from: Cascade::identity(),
            moving: false,
            span: 1,
            into: 0,
            waited: 0,
            goal: Cascade::identity(),
            morph: None,
            lanes: [State::default(); 2],
            clamped: false,
        }
    }

    /// From what it plays now to `what`, by a morph (see the module notes).
    fn become_(&mut self, what: Discrete) {
        if what.family != self.what.family || what.order != self.what.order {
            self.proto = None;
        }
        self.what = what;
        self.morph = Some((self.cascade, 0.0));
    }

    /// Designed again when anything it is made of moved; its prototype as
    /// the budget and the throttle allow (a first one always: a fade or a
    /// morph starts only with budget for it). `step` is how far a morph
    /// comes in one tick. True when a weighed design waited for budget.
    fn update(&mut self, now: &Glided, target: &Glided, g: Globals, fs: f64, budget: &mut Budget, step: f64) -> bool {
        if self.what.shape == Shape::Empty {
            return false;
        }
        self.proto_age = self.proto_age.saturating_add(1);
        /* Where the move under way has got to: a sub-block of it passed. */
        if self.moving {
            self.into += 1;
            if self.into >= self.span {
                self.moving = false;
            }
        }
        let reached = if self.moving {
            self.from.morph(&self.cascade, f64::from(self.into) / f64::from(self.span))
        } else {
            self.cascade
        };
        let s = now.settings(&self.what);
        if self.designed == Some((s, g, fs)) && self.morph.is_none() {
            return false;
        }
        let first = self.designed.is_none();
        if !first && weighs(&s, fs) {
            if budget.weighs == 0 {
                self.waited += 1;
                return true;
            }
            budget.weighs -= 1;
        }
        if s.shape.uses_family() {
            let uses = |s: &Settings| {
                (
                    if s.family.uses_ripple() { s.ripple_db } else { 0.0 },
                    if s.family.uses_stopband() { s.stopband_db } else { 0.0 },
                )
            };
            let wanted = uses(&s);
            let landed = wanted == uses(&target.settings(&self.what));
            let due = self.proto_for != wanted && (landed || self.proto_age >= PROTO_EVERY) && budget.protos > 0;
            if self.proto.is_none() || due {
                self.proto = prototype(&s);
                self.proto_for = wanted;
                self.proto_age = 0;
                budget.protos = budget.protos.saturating_sub(1);
            }
        }
        let (designed, clamped) = design(&s, self.proto.as_ref(), g, fs);
        let goal = designed.second_order_form();
        self.clamped = clamped;
        /* A glide that crossed into another realisation morphs there, from
         * where it is -- a morph under way included. */
        if self.designed.is_some() && !goal.same_realisation(&self.goal) && self.morph.is_none_or(|(_, p)| p > 0.0) {
            self.morph = Some((self.cascade, 0.0));
        }
        self.goal = goal;
        self.designed = Some((s, g, fs));
        self.from = reached;
        (self.span, self.into, self.waited) = (self.waited + 1, 0, 0);
        self.cascade = match self.morph {
            None => goal,
            Some((_, p)) if p >= 1.0 => {
                /* Landed: the wires it was padded with pass the signal
                 * exactly, and go. */
                self.morph = None;
                for l in &mut self.lanes {
                    l.clear_from(goal.len());
                }
                goal
            }
            Some((start, p)) => {
                /* Where it starts, the cascade as it was, padded: the same
                 * structure as every step after it. */
                if p == 0.0 {
                    self.from = start.morph(&goal, 0.0);
                }
                let p = (p + step).min(1.0);
                self.morph = Some((start, p));
                start.morph(&goal, ease(p))
            }
        };
        self.moving = !first && self.from != self.cascade && self.from.same_structure(&self.cascade);
        false
    }

    /// One sample, `t` (0..1] of the way through the sub-block. `mono`: `r`
    /// is `l`, and only the left lane is worked out -- the right and the
    /// side are what a mono track does not play.
    #[inline]
    fn run(&mut self, l: f64, r: f64, t: f64, mono: bool) -> (f64, f64) {
        let t = if self.span == 1 { t } else { (f64::from(self.into) + t) / f64::from(self.span) };
        let (c, from, moving) = (&self.cascade, &self.from, self.moving);
        let run = |st: &mut State, x: f64| if moving { from.run_toward(c, t, st, x) } else { c.run(st, x) };
        let [a, b] = &mut self.lanes;
        if mono {
            let y = match self.what.channel {
                Channel::Stereo | Channel::Left | Channel::Mid => run(a, l),
                Channel::Right | Channel::Side => l,
            };
            return (y, y);
        }
        match self.what.channel {
            Channel::Stereo => (run(a, l), run(b, r)),
            Channel::Left => (run(a, l), r),
            Channel::Right => (l, run(b, r)),
            Channel::Mid => {
                let (m, s) = (0.5 * (l + r), 0.5 * (l - r));
                let m = run(a, m);
                (m + s, m - s)
            }
            Channel::Side => {
                let (m, s) = (0.5 * (l + r), 0.5 * (l - r));
                let s = run(b, s);
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
    target: Glided,
    now: Glided,
    a: Instance,
    b: Instance,
    /// How far `b` has faded in, 0..1, while `fading`.
    t: f64,
    fading: bool,
    /// Samples `b` still runs unheard before its fade moves.
    warm: usize,
}

impl Node {
    /// A node that plays nothing yet, its glides at `t`'s values: the first
    /// control step fades in what `t` asks for.
    pub fn new(t: &Target) -> Node {
        let g = Glided::of(t);
        Node {
            target: g,
            now: g,
            a: Instance::new(Discrete::EMPTY),
            b: Instance::new(Discrete::EMPTY),
            t: 0.0,
            fading: false,
            warm: 0,
        }
    }

    /// Nothing to do: Empty or off, no fade running, and none asked for.
    pub fn is_idle(&self, t: &Target) -> bool {
        !self.fading && self.a.what.shape == Shape::Empty && Discrete::of(t).shape == Shape::Empty
    }

    /// One control step: the targets read, the glides moved, a fade started
    /// when what the node is changed and `budget` allows, and the instances
    /// designed again, each prototype and weighed design taken from `budget`.
    /// True when a design waited for budget, so the next tick serves this
    /// node first.
    pub fn control(&mut self, t: &Target, c: f64, g: Globals, fs: f64, budget: &mut Budget) -> bool {
        self.target = Glided::of(t);
        let want = Discrete::of(t);
        let step = SUB as f64 * 1000.0 / (FADE_MS * fs);
        let changed = !self.fading && want != self.a.what && (!want.shape.uses_family() || budget.protos > 0);
        let a = self.a.what;
        if changed && want.shape == a.shape && want.channel == a.channel && want.order == a.order && a.shape != Shape::Empty {
            /* How it does it: a morph of the playing instance. */
            self.now.toward(&self.target, c);
            self.a.become_(want);
        } else if changed {
            /* From nothing, nothing was heard of the glides: they start where
             * they are going. */
            if a.shape == Shape::Empty {
                self.now = self.target;
            }
            self.b = Instance::new(want);
            self.t = 0.0;
            self.fading = true;
            self.warm = usize::MAX;
        } else {
            self.now.toward(&self.target, c);
        }
        /* The instance fading in first: its prototype is the one the
         * budget was checked for. */
        let mut waited = false;
        if self.fading {
            waited |= self.b.update(&self.now, &self.target, g, fs, budget, step);
            if self.warm == usize::MAX {
                self.warm = warm_samples(self.b.cascade.decay_samples(WARM_DB), fs);
            }
        }
        waited | self.a.update(&self.now, &self.target, g, fs, budget, step)
    }

    /// `l` and `r` through the node, in place, their first sample at
    /// `phase` on the control grid (the sub-block's samples to come are
    /// `phase..SUB`); `step` is a fade's progress a sample; `mono`, `r` is
    /// `l`'s copy and one lane is worked out.
    pub fn run(&mut self, l: &mut [f64], r: &mut [f64], phase: usize, step: f64, mono: bool) {
        let n = l.len().min(r.len());
        let t = |i: usize| (phase + i + 1) as f64 / SUB as f64;
        let mut i = 0;
        while self.fading && i < n {
            let (al, ar) = self.a.run(l[i], r[i], t(i), mono);
            let (bl, br) = self.b.run(l[i], r[i], t(i), mono);
            if self.warm > 0 {
                /* Settling, unheard. */
                self.warm -= 1;
                (l[i], r[i]) = (al, ar);
            } else {
                let s = (core::f64::consts::FRAC_PI_2 * self.t).sin();
                let (wa, wb) = (1.0 - s * s, s * s);
                l[i] = wa * al + wb * bl;
                r[i] = wa * ar + wb * br;
                self.t += step;
                if self.t >= 1.0 {
                    self.land();
                }
            }
            i += 1;
        }
        /* Steady, or a fade that landed part way: the rest on `a` alone. */
        if self.a.what.shape == Shape::Empty {
            return;
        }
        for j in i..n {
            (l[j], r[j]) = self.a.run(l[j], r[j], t(j), mono);
        }
    }

    fn land(&mut self) {
        self.a = self.b;
        self.b = Instance::new(Discrete::EMPTY);
        self.fading = false;
        self.t = 0.0;
        self.warm = 0;
    }

    /// The panic: a fade lands, every lane empties, every glide arrives.
    pub fn reset(&mut self, t: &Target, g: Globals, fs: f64) {
        if self.fading {
            self.land();
        }
        self.target = Glided::of(t);
        self.now = self.target;
        let want = Discrete::of(t);
        if want != self.a.what {
            self.a = Instance::new(want);
        }
        self.a.reset();
        self.a.designed = None;
        self.a.morph = None;
        /* The panic lands everything at once, outside any tick's budget. */
        let mut unbounded = Budget::UNBOUNDED;
        self.a.update(&self.now, &self.target, g, fs, &mut unbounded, 1.0);
        self.a.moving = false;
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

    /// Whether the playing design is morphing into another (see the module
    /// notes).
    pub fn is_morphing(&self) -> bool {
        self.a.morph.is_some()
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

/// ONE CONTROL TICK OF A CHAIN: every node of `nodes` stepped toward its
/// target (`target(i)` is node `i`'s), from node `first` on and round, under
/// one tick's [`Budget`]; what the tick left of it. The next tick serves
/// first the first node this one could not design, or else the one after
/// `first`, so the budget goes round evenly: with sixteen weighing, each is
/// designed every fourth tick, and a glide that waits puts no lines at a
/// fraction of the control rate.
pub fn control_in_turn(nodes: &mut [Node], first: &mut usize, target: impl Fn(usize) -> Target, c: f64, g: Globals, fs: f64) -> Budget {
    let n = nodes.len();
    let mut budget = Budget::TICK;
    let mut next = None;
    for k in 0..n {
        let at = (*first + k) % n;
        if nodes[at].control(&target(at), c, g, fs, &mut budget) && next.is_none() {
            next = Some(at);
        }
    }
    *first = next.unwrap_or((*first + 1) % n.max(1));
    budget
}
