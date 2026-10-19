// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! A node on its own: what it plays is its design, a change of what it is
//! fades, a change of how it does it morphs, a tick's work is budgeted and
//! goes round, and the panic lands everything. NI Filter Designer's engine
//! tests hold the same machinery inside sixteen nodes (fd-core).

use crate::design::*;
use crate::node::*;
use ni_dsp::smooth::coef;
use ni_filter::Family;

const FS: f64 = 48_000.0;

fn settings(shape: Shape) -> Settings {
    Settings {
        shape,
        family: Family::Butterworth,
        order: 2,
        freq: 1000.0,
        gain_db: 6.0,
        q: core::f64::consts::FRAC_1_SQRT_2,
        ripple_db: 1.0,
        stopband_db: 60.0,
        matched: false,
    }
}

fn target(s: Settings) -> Target {
    Target { settings: s, channel: Channel::Stereo, on: true }
}

/// A node and the control grid it runs on, as an engine drives one.
struct Driver {
    node: Node,
    phase: usize,
}

impl Driver {
    fn new(t: &Target) -> Driver {
        Driver { node: Node::new(t), phase: 0 }
    }

    /// `l` and `r` through the node toward `t`, in blocks of `block`.
    fn run(&mut self, t: &Target, l: &mut [f64], r: &mut [f64], block: usize, mono: bool) {
        let c = coef(FS / SUB as f64);
        let step = 1000.0 / (FADE_MS * FS);
        let mut i = 0;
        while i < l.len() {
            let n = block.min(l.len() - i);
            let mut k = 0;
            while k < n {
                if self.phase == 0 {
                    let mut b = Budget::TICK;
                    self.node.control(t, c, Globals::NEUTRAL, FS, &mut b);
                }
                let m = (SUB - self.phase).min(n - k);
                let (a, b) = (i + k, i + k + m);
                self.node.run(&mut l[a..b], &mut r[a..b], self.phase, step, mono);
                self.phase = (self.phase + m) % SUB;
                k += m;
            }
            self.node.flush();
            i += n;
        }
    }
}

fn sine(hz: f64, n: usize) -> Vec<f64> {
    (0..n).map(|i| (2.0 * core::f64::consts::PI * hz * i as f64 / FS).sin()).collect()
}

fn peak(x: &[f64]) -> f64 {
    x.iter().fold(0.0, |m, v| m.max(v.abs()))
}

/// The amplitude of `hz` in `x`, by projection: `x` holds whole periods.
fn amplitude(x: &[f64], hz: f64) -> f64 {
    let w = 2.0 * core::f64::consts::PI * hz / FS;
    let (s, c) = x.iter().enumerate().fold((0.0, 0.0), |(s, c), (i, v)| (s + v * (w * i as f64).sin(), c + v * (w * i as f64).cos()));
    2.0 * s.hypot(c) / x.len() as f64
}

#[test]
fn a_settled_node_plays_its_design() {
    for shape in Shape::ALL.into_iter().filter(|&s| s != Shape::Empty) {
        let s = Settings { order: 4, q: 1.5, ..settings(shape) };
        let t = target(s);
        let (c, _) = design(&s, prototype(&s).as_ref(), Globals::NEUTRAL, FS);
        let mut d = Driver::new(&t);
        for hz in [300.0, 1000.0, 3000.0] {
            let mut l = sine(hz, 48_000);
            let mut r = l.clone();
            d.run(&t, &mut l, &mut r, 512, false);
            /* The last tenth of a second: whole periods of every test tone. */
            let got = 20.0 * amplitude(&l[43_200..], hz).log10();
            let want = c.db(hz, FS);
            assert!((got - want).abs() < 0.01 || want < -60.0, "{shape:?} at {hz} Hz: {got} dB, the design {want} dB");
            assert_eq!(l, r, "{shape:?}: a stereo node filters both halves alike");
        }
    }
}

#[test]
fn what_a_node_is_fades_and_how_it_does_it_morphs() {
    let s = settings(Shape::HighPass);
    let mut d = Driver::new(&target(s));
    let (mut l, mut r) = (vec![0.0; 4800], vec![0.0; 4800]);
    d.run(&target(s), &mut l, &mut r, 64, false);
    assert!(!d.node.is_fading() && !d.node.is_morphing());

    let lp = target(Settings { shape: Shape::LowPass, ..s });
    d.run(&lp, &mut l[..SUB], &mut r[..SUB], SUB, false);
    assert!(d.node.is_fading(), "another type fades");
    d.run(&lp, &mut l, &mut r, 64, false);
    assert!(!d.node.is_fading(), "and lands within a tenth of a second");

    let cheby = target(Settings { shape: Shape::LowPass, family: Family::ChebyshevI, ..s });
    d.run(&cheby, &mut l[..SUB], &mut r[..SUB], SUB, false);
    assert!(d.node.is_morphing() && !d.node.is_fading(), "another family morphs");
    d.run(&cheby, &mut l, &mut r, 64, false);
    assert!(!d.node.is_morphing());
}

#[test]
fn an_unheard_node_is_idle_and_costs_nothing() {
    let off = Target { on: false, ..target(settings(Shape::Bell)) };
    let mut d = Driver::new(&off);
    assert!(d.node.is_idle(&off));
    let mut l = sine(440.0, 1000);
    let want = l.clone();
    let mut r = l.clone();
    d.run(&off, &mut l, &mut r, 100, false);
    assert_eq!(l, want, "a node switched off passes the signal untouched");
    assert!(!d.node.is_idle(&target(settings(Shape::Bell))), "a node asked to play is not idle");
}

#[test]
fn a_render_is_the_same_bits_at_any_block_size() {
    let sweep = |block: usize| {
        let mut d = Driver::new(&target(settings(Shape::Bell)));
        let (mut l, mut r) = (sine(500.0, 9600), sine(700.0, 9600));
        for (k, chunk) in (0..9600).step_by(960).enumerate() {
            let t = target(Settings { freq: 200.0 * (k + 1) as f64, family: Family::Elliptic, shape: Shape::Notch, ..settings(Shape::Notch) });
            d.run(&t, &mut l[chunk..chunk + 960], &mut r[chunk..chunk + 960], block, false);
        }
        (l, r)
    };
    let a = sweep(960);
    for block in [1, 7, 64, 333] {
        assert!(sweep(block) == a, "block {block}");
    }
}

#[test]
fn each_channel_filters_its_own_half() {
    let s = Settings { order: 4, freq: 4000.0, ..settings(Shape::LowPass) };
    let (l0, r0) = (sine(10_000.0, 9600), sine(10_000.0, 9600).iter().map(|x| -x).collect::<Vec<_>>());
    let play = |channel: Channel| {
        let t = Target { channel, ..target(s) };
        let mut d = Driver::new(&t);
        let (mut l, mut r) = (l0.clone(), r0.clone());
        d.run(&t, &mut l, &mut r, 256, false);
        (peak(&l[8000..]), peak(&r[8000..]))
    };
    let (ll, rr) = play(Channel::Left);
    assert!(ll < 0.1 && (rr - 1.0).abs() < 1e-9, "Left: {ll} {rr}");
    let (ll, rr) = play(Channel::Right);
    assert!((ll - 1.0).abs() < 1e-9 && rr < 0.1, "Right: {ll} {rr}");
    /* The pair is all side (L = -R): a Mid filter leaves it, a Side one cuts it. */
    let (ll, rr) = play(Channel::Mid);
    assert!((ll - 1.0).abs() < 1e-6 && (rr - 1.0).abs() < 1e-6, "Mid: {ll} {rr}");
    let (ll, rr) = play(Channel::Side);
    assert!(ll < 0.1 && rr < 0.1, "Side: {ll} {rr}");
    /* Mono: the left lane is the track; Right and Side are what it lacks. */
    for (channel, heard) in [(Channel::Stereo, true), (Channel::Mid, true), (Channel::Right, false), (Channel::Side, false)] {
        let t = Target { channel, ..target(s) };
        let mut d = Driver::new(&t);
        let (mut l, mut r) = (l0.clone(), l0.clone());
        d.run(&t, &mut l, &mut r, 256, true);
        assert_eq!(peak(&l[8000..]) < 0.1, heard, "{channel:?} on a mono track");
    }
}

#[test]
fn the_panic_lands_everything_and_empties_every_lane() {
    let t = target(Settings { order: 8, family: Family::Elliptic, ..settings(Shape::LowPass) });
    let mut d = Driver::new(&t);
    let (mut l, mut r) = (sine(300.0, 4800), sine(300.0, 4800));
    d.run(&t, &mut l, &mut r, 128, false);
    d.run(&target(settings(Shape::HighShelf)), &mut l[..SUB], &mut r[..SUB], SUB, false);
    assert!(d.node.is_fading());
    d.node.reset(&t, Globals::NEUTRAL, FS);
    assert!(!d.node.is_fading() && d.node.is_silent());
    let (c, _) = design(&t.settings, prototype(&t.settings).as_ref(), Globals::NEUTRAL, FS);
    assert_eq!(d.node.cascade().len(), c.second_order_form().len(), "the panic lands on the target's design");
}

#[test]
fn a_ticks_work_goes_round_a_chain() {
    /* Six elliptic notches, each a prototype to design, under a budget of
     * four a tick: the first tick serves four, the next the two it could not
     * start with, so every node is playing after two. */
    let t = target(Settings { family: Family::Elliptic, order: 6, ..settings(Shape::Notch) });
    let mut nodes: Vec<Node> = (0..6).map(|_| Node::new(&t)).collect();
    let mut first = 0;
    let c = coef(FS / SUB as f64);
    let left = control_in_turn(&mut nodes, &mut first, |_| t, c, Globals::NEUTRAL, FS);
    assert_eq!(left.protos, 0);
    assert_eq!(first, 1, "with none waiting, the next tick starts one on");
    control_in_turn(&mut nodes, &mut first, |_| t, c, Globals::NEUTRAL, FS);
    assert!(nodes.iter().all(Node::is_fading), "every node started within two ticks");
    let mut none: [Node; 0] = [];
    let mut first = 0;
    assert_eq!(control_in_turn(&mut none, &mut first, |_| t, c, Globals::NEUTRAL, FS), Budget::TICK);
}

#[test]
fn a_weighed_design_waits_for_budget_and_is_spread_over_its_wait() {
    /* A matched low-pass high in the band weighs its realisations: with no
     * weighs left in the tick it waits, and says so. */
    let s = Settings { matched: true, freq: 9000.0, ..settings(Shape::LowPass) };
    let mut d = Driver::new(&target(s));
    let (mut l, mut r) = (vec![0.0; 4800], vec![0.0; 4800]);
    d.run(&target(s), &mut l, &mut r, 64, false);
    let c = coef(FS / SUB as f64);
    let moved = target(Settings { freq: 12_000.0, ..s });
    let mut empty = Budget { protos: 4, weighs: 0 };
    assert!(d.node.control(&moved, c, Globals::NEUTRAL, FS, &mut empty), "no budget: it waits");
    let mut tick = Budget::TICK;
    assert!(!d.node.control(&moved, c, Globals::NEUTRAL, FS, &mut tick));
    assert_eq!(tick.weighs, WEIGHS_PER_TICK - 1);
}

#[test]
fn the_design_helpers_say_what_each_type_uses() {
    assert!(Shape::HighPass.uses_matched() && Shape::LowPass.uses_matched() && !Shape::Bell.uses_matched());
    assert_eq!(order_played(Shape::HighPass, Family::LinkwitzRiley, 2), 3, "Linkwitz-Riley cuts are even");
    assert_eq!(order_played(Shape::LowShelf, Family::Butterworth, 7), 3, "a shelf stops at order 4");
    assert_eq!(order_played(Shape::Bell, Family::Butterworth, 5), 0);

    let bell = settings(Shape::Bell);
    let (lo, hi) = edges(&bell).unwrap();
    assert!((q_for_edge(bell.freq, hi) - bell.q).abs() < 1e-9 && (q_for_edge(bell.freq, lo) - bell.q).abs() < 1e-9);
    assert!(edges(&settings(Shape::HighPass)).is_none());

    let g = Globals { scale: 0.5, adaptive: 0.0 };
    for shape in Shape::ALL {
        let s = settings(shape);
        let (c, clamped) = design(&s, None, g, FS);
        assert!(!clamped);
        let at = handle(&s, g, &c, FS);
        match shape {
            Shape::Empty => assert_eq!(at, None),
            Shape::Bell | Shape::LowShelf | Shape::HighShelf => assert_eq!(at, Some((1000.0, 3.0))),
            Shape::HighPass | Shape::LowPass => assert!((at.unwrap().1 + 3.0103).abs() < 0.01, "{shape:?} at its -3 dB point"),
            _ => assert_eq!(at, Some((1000.0, 0.0))),
        }
    }
    assert!(decay_samples(&Settings { freq: 20.0, ..settings(Shape::HighPass) }, g, FS, 60.0) > 1000.0);
    assert!(weighs(&Settings { freq: 4000.0, ..settings(Shape::HighShelf) }, FS));
    assert!(!weighs(&settings(Shape::HighShelf), FS));
    assert!(!Target { on: false, ..target(bell) }.is_heard() && target(bell).is_heard());
    assert!(!target(settings(Shape::Empty)).is_heard());
    assert_eq!(Shape::from_value(42.0), Shape::Tilt);
    assert_eq!(Channel::from_value(-3.0), Channel::Stereo);
    assert!(design(&Settings { freq: 30_000.0, ..bell }, None, Globals::NEUTRAL, FS).1, "Freq past the top clamps");
}

#[test]
fn the_picture_conversions() {
    use crate::picture::{angle, decibels};
    assert!((angle(12_000.0, FS) - core::f64::consts::FRAC_PI_2).abs() < 1e-12);
    assert!(angle(30_000.0, FS) < core::f64::consts::PI);
    assert_eq!(decibels(ni_filter::Complex64::new(0.0, 0.0)), -300.0);
    assert!((decibels(ni_filter::Complex64::new(0.0, 10.0)) - 20.0).abs() < 1e-6);
}
