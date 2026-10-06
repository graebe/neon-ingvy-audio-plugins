// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! The parameter clamps, both doors, and the click-free curve change.

use crate::params::{amp_to_db, db_to_amp, Param, PARAM_COUNT};
use crate::shape::{Curve, Stage};
use crate::tests::note_on;
use crate::{rates, Instance, Source};

/* ----------------------------------------------------------- parameters */

#[test]
fn db_and_amp_round_trip() {
    for db in [-59.0, -48.0, -24.0, -12.0, -6.0, -0.5, 0.0] {
        let back = amp_to_db(db_to_amp(db));
        assert!((back - db).abs() < 1e-9, "{db} -> {back}");
    }
    /* The bottom of the range means "anything triggers", so it is zero
     * amplitude rather than -60 dB of it. */
    assert_eq!(db_to_amp(-60.0), 0.0);
    /* And the inverse floors rather than returning -inf, so no readout can
     * ever print "-inf dB". */
    assert_eq!(amp_to_db(0.0), -60.0);
    assert_eq!(amp_to_db(-1.0), -60.0);
}

#[test]
fn every_parameter_round_trips_through_set_num_and_num() {
    let mut p = Instance::new(48000.0);
    /* A value inside each range that is not the default, so a setter that
     * silently does nothing is caught. */
    let cases: [(Param, f64); 15] = [
        (Param::Source, 2.0),
        (Param::Rate, 6.0),
        (Param::TimeMode, 1.0),
        (Param::Delay, 12.5),
        (Param::Attack, 3.25),
        (Param::Hold, 44.0),
        (Param::Release, 111.5),
        (Param::Depth, 0.625),
        (Param::Curve, 2.0),
        (Param::Channel, 7.0),
        (Param::Note, 60.0),
        (Param::MidiMode, 1.0),
        (Param::VelSens, 0.5),
        (Param::Threshold, -18.0),
        (Param::Lockout, 33.0),
    ];
    for (param, v) in cases {
        p.set_num(param, v);
        let back = p.num(param);
        assert!(
            (back - v).abs() < 1e-9,
            "{}: set {v}, read {back}",
            param.key()
        );
    }
}

#[test]
fn every_parameter_round_trips_through_the_string_door() {
    /* The string door must agree with the numeric one, because a Schwung shell
     * and a host automation lane are two views of one state. */
    let mut a = Instance::new(48000.0);
    let mut b = Instance::new(48000.0);
    let cases: [(Param, f64); 15] = [
        (Param::Source, 1.0),
        (Param::Rate, 2.0),
        (Param::TimeMode, 1.0),
        (Param::Delay, 5.5),
        (Param::Attack, 7.75),
        (Param::Hold, 20.0),
        (Param::Release, 90.0),
        (Param::Depth, 0.8),
        (Param::Curve, 2.0),
        (Param::Channel, 0.0),
        (Param::Note, 48.0),
        (Param::MidiMode, 0.0),
        (Param::VelSens, 0.25),
        (Param::Threshold, -30.0),
        (Param::Lockout, 10.0),
    ];
    let mut buf = [0u8; 256];
    for (param, v) in cases {
        a.set_num(param, v);
        let n = a.get_param(param.key(), &mut buf);
        assert!(n > 0, "{} has no readout", param.key());
        let text = std::str::from_utf8(&buf[..n as usize]).unwrap().to_owned();
        assert!(
            b.set_param(param.key(), &text),
            "{} not accepted by the string door",
            param.key()
        );
        let back = b.num(param);
        assert!(
            (back - v).abs() < 1e-6,
            "{}: {v} -> \"{text}\" -> {back}",
            param.key()
        );
    }
}

#[test]
fn the_string_door_takes_a_label_as_well_as_an_index() {
    let mut p = Instance::new(48000.0);
    p.set_param("rate", "1/8");
    assert_eq!(rates::RATES[p.num(Param::Rate) as usize].label, "1/8");
    p.set_param("curve", "S-Curve");
    assert_eq!(p.num(Param::Curve), Curve::SCurve as i32 as f64);
    p.set_param("source", "Sidechain");
    assert_eq!(p.num(Param::Source), Source::Sidechain as i32 as f64);
    p.set_param("midi_mode", "Gate");
    assert_eq!(p.num(Param::MidiMode), 1.0);
    p.set_param("channel", "Omni");
    assert_eq!(p.num(Param::Channel), 0.0);
}

#[test]
fn out_of_range_is_clamped_and_nan_is_dropped() {
    let mut p = Instance::new(48000.0);
    p.set_num(Param::Depth, 5.0);
    assert_eq!(p.num(Param::Depth), 1.0);
    p.set_num(Param::Depth, -5.0);
    assert_eq!(p.num(Param::Depth), 0.0);

    p.set_num(Param::Attack, 12.0);
    /* A NaN from a host is not a value, and clamping would propagate it. */
    p.set_num(Param::Attack, f64::NAN);
    assert_eq!(p.num(Param::Attack), 12.0, "a NaN overwrote a good value");

    p.set_num(Param::Attack, 9_999.0);
    assert_eq!(p.num(Param::Attack), crate::STAGE_MAX_PCT);
    p.set_num(Param::Delay, 9_999.0);
    assert_eq!(p.num(Param::Delay), crate::DELAY_RANGE_PCT);
}

#[test]
fn an_unknown_key_is_refused_by_both_doors() {
    let mut p = Instance::new(48000.0);
    assert!(!p.set_param("wobble", "1"));
    let mut buf = [0u8; 64];
    assert_eq!(p.get_param("wobble", &mut buf), -1);
    assert!(Param::from_i32(PARAM_COUNT).is_none());
    assert!(Param::from_i32(-1).is_none());
}

#[test]
fn changing_the_curve_mid_duck_does_not_move_the_gain() {
    /* THE CLICK THIS PREVENTS: swapping the curve and leaving `pos` alone jumps
     * the level to wherever the new curve is at that time. */
    for (from, to) in [
        (Curve::Linear, Curve::Exp),
        (Curve::Exp, Curve::SCurve),
        (Curve::SCurve, Curve::Linear),
    ] {
        for stage_under_test in [Stage::Attack, Stage::Release] {
            let mut p = Instance::new(48000.0);
            p.set_num(Param::Curve, from as i32 as f64);
            p.set_num(Param::Source, Source::Midi as i32 as f64);
            /* The stage lengths are PERCENTAGES OF THE CYCLE, and the cycle
             * here is a 1/4 at 120 bpm and 48 kHz -- 24 000 samples. So 5% is
             * 1200 samples of attack, 1% is 240 of hold, and 20% is 4800 of
             * release. Reading these as milliseconds is what made the first
             * version of this test run 3000 samples into a 24 000-sample
             * attack and then assert it was in the release. */
            p.set_num(Param::Attack, 5.0);
            p.set_num(Param::Hold, 1.0);
            p.set_num(Param::Release, 20.0);

            let mut buf = vec![0.0f32; 8192];
            p.on_midi(&note_on(1, 36, 127), 0);
            let n = if stage_under_test == Stage::Attack {
                600 /* half way up the 1200-sample attack */
            } else {
                1600 /* past attack + hold, into the release */
            };
            p.process_f32(&mut buf[..n * 2], n, None);
            assert_eq!(p.stage(), stage_under_test, "{from:?}->{to:?} setup");

            let before = p.duck_now();
            p.set_num(Param::Curve, to as i32 as f64);
            /* The level is a state, not a computation: the curve change must
             * not touch it at all. */
            assert_eq!(
                p.duck_now(),
                before,
                "{from:?} -> {to:?} in {stage_under_test:?} moved the gain"
            );
            /* And the next sample must continue from there, not jump. */
            p.process_f32(&mut buf[..2], 1, None);
            let after = p.duck_now();
            assert!(
                (after - before).abs() < 0.05,
                "{from:?} -> {to:?} in {stage_under_test:?}: {before} -> {after}"
            );
        }
    }
}
