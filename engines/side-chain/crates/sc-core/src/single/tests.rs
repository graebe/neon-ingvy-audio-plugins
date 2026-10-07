// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! The single shot the editor draws: against the running envelope it idealises,
//! and its handles against the curve they sit on.

use crate::shape::{shape, Curve, Env, Stages};
use crate::single::{Marks, Single};
use crate::tests::CURVES;

fn shot(curve: Curve) -> Single {
    Single { curve, delay: 10.0, attack: 6.0, hold: 11.0, release: 42.0, depth: 0.85, cycle: true }
}

#[test]
fn the_shot_is_the_envelope_one_uninterrupted_trigger_runs() {
    /* The engine's stage machine at one sample a 1/1000 of the cycle, from a
     * trigger at the start, against the drawing from the same start. */
    for c in CURVES {
        let s = Single { delay: 0.0, ..shot(c) };
        let k = 1000.0 / 100.0; /* samples per percent */
        let stages = Stages { delay: 0.0, attack: s.attack * k, hold: s.hold * k, release: s.release * k };
        let mut env = Env::default();
        env.trigger(1.0, &stages);
        for i in 0..1000 {
            let ran = env.next(c, &stages, false);
            let drawn = s.duck_at(i as f64 / k);
            assert!((ran - drawn).abs() < 1e-12, "{c:?} at sample {i}: ran {ran}, drawn {drawn}");
        }
    }
}

#[test]
fn the_corners_are_where_the_curve_turns() {
    for c in CURVES {
        let s = shot(c);
        let m = s.marks();
        assert_eq!(m, Marks { start: 10.0, bottom: 16.0, hold_end: 27.0, end: 69.0, span: 59.0, floor: 1.0 - 0.85 });
        assert_eq!(s.duck_at(m.start), 0.0);
        assert_eq!(s.duck_at(m.bottom), 1.0);
        assert_eq!(s.duck_at(m.hold_end - 1e-9), 1.0);
        assert_eq!(s.duck_at(m.end), 0.0);
        assert!((s.gain_at(m.bottom) - m.floor).abs() < 1e-15);
        /* Halfway down the attack is the curve's own halfway. */
        assert_eq!(s.duck_at(13.0), shape(c, 0.5));
    }
}

#[test]
fn a_negative_delay_wraps_on_cycle_and_is_no_wait_elsewhere() {
    let early = Single { delay: -20.0, ..shot(Curve::Linear) };
    assert_eq!(early.start(), 80.0);
    assert_eq!(early.marks().bottom, 86.0);
    /* The duck that belongs to the next beat is already down at this one's
     * end, and still recovering at its start. */
    assert_eq!(early.duck_at(90.0), 1.0);
    assert!(early.duck_at(5.0) > 0.0);

    let midi = Single { cycle: false, ..early };
    assert_eq!(midi.start(), 0.0);
    assert_eq!(midi.marks().start, 0.0);
    assert_eq!(midi.duck_at(3.0), 0.5);
    /* Unwrapped: a shot longer than the cycle ends past 100. */
    let long = Single { release: 150.0, ..midi };
    assert_eq!(long.marks().end, 167.0);
    assert_eq!(long.marks().span, 167.0);
}

#[test]
fn the_render_spans_the_cycle_ends_included_and_applies_depth() {
    let s = shot(Curve::Exp);
    let mut out = [0.0f32; 101];
    s.render(&mut out);
    for (i, g) in out.iter().enumerate() {
        assert_eq!(*g, s.gain_at(i as f64) as f32, "column {i}");
    }
    assert_eq!(out[0], 1.0);
    assert_eq!(out[20], 0.15);
    let mut one = [7.0f32; 1];
    s.render(&mut one);
    assert_eq!(one[0], s.gain_at(0.0) as f32);
    s.render(&mut []);
}

#[test]
fn nothing_a_host_can_send_breaks_it() {
    let s = Single {
        curve: Curve::SCurve,
        delay: f64::NAN,
        attack: f64::NAN,
        hold: -5.0,
        release: f64::INFINITY,
        depth: 7.0,
        cycle: true,
    };
    for i in 0..=100 {
        let g = s.gain_at(i as f64);
        assert!((0.0..=1.0).contains(&g), "gain {g} at {i}");
    }
    assert_eq!(s.duck_at(f64::NAN), 0.0);
    assert_eq!(s.marks().floor, 0.0);
    assert_eq!(s.start(), 0.0);
}
