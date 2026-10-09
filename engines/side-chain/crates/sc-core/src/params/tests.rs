// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! The parameter clamps, both doors, and the click-free curve change.

use crate::params::{amp_to_db, db_to_amp, Param, PARAM_COUNT, STATE_TAG};
use crate::shape::{Curve, Stage};
use crate::tests::note_on;
use crate::{rates, Instance, Source, Transport};

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
fn a_parameters_index_is_its_place_in_the_list_and_its_key_finds_it() {
    /* APPEND ONLY (see `Param`): the index is what a host stores. */
    assert_eq!(Param::ALL.len(), PARAM_COUNT as usize);
    for (i, p) in Param::ALL.into_iter().enumerate() {
        assert_eq!(p as i32, i as i32, "{p:?} is out of place");
        assert_eq!(Param::from_i32(i as i32), Some(p));
        assert_eq!(Param::from_key(p.key()), Some(p), "{}", p.key());
    }
    assert_eq!(Param::from_key("panic"), None, "a command, not a parameter");
}

/* ---------------------------------------------------------------- the wire */

/*
 * WHAT A SCHWUNG HOST AND A SAVED PATCH SEE, BYTE FOR BYTE.
 *
 * The round trips above prove the two doors agree with each other; these pin
 * what they SAY. Every string below is the engine's own output, recorded, so
 * a change to the parsing or the formatting -- here, or in ni-dsp's
 * C-compatible fmt underneath -- that a host would notice fails here first.
 */

/// A state whose every readout is plain arithmetic, so the bytes are the same
/// on every platform's maths library and not only on this one's: a Linear
/// curve, so no `exp` in the envelope; the threshold at its floor, so no
/// `log10` in its readout; and one block of a transport that has just started,
/// so the phase is the host's own number rather than a tracked one.
fn a_state_on_the_wire() -> Instance {
    let mut p = Instance::new(48000.0);
    for (param, v) in [
        (Param::Source, 1.0),
        (Param::Rate, 6.0),
        (Param::TimeMode, 1.0),
        (Param::Delay, -12.5),
        (Param::Attack, 5.0),
        (Param::Hold, 10.0),
        (Param::Release, 25.0),
        (Param::Depth, 0.625),
        (Param::Curve, 0.0),
        (Param::Channel, 7.0),
        (Param::Note, 60.0),
        (Param::MidiMode, 1.0),
        (Param::VelSens, 0.5),
        (Param::Threshold, -60.0),
        (Param::Lockout, 33.0),
    ] {
        p.set_num(param, v);
    }
    p.on_midi(&note_on(7, 60, 100), 10);
    let t = Transport { running: true, beats: 1.25, bpm: 120.0 };
    let mut buf = vec![0.5f32; 512];
    p.process_f32(&mut buf, 256, Some(&t));
    p
}

#[test]
fn every_readout_says_the_same_bytes() {
    const WIRE: [(&str, &str); 30] = [
        (
            "state",
            "sc1;source=1;rate=6;time_mode=1;delay=-12.5;attack=5;hold=10;release=25;depth=0.625;\
             curve=0;channel=7;trigger_note=60;midi_mode=1;vel_sens=0.5;threshold=-60;lockout=33",
        ),
        ("ui", "1:6:250.000:0.020500:1:1:0.3649:0.00000:0:2:0.500000"),
        ("params", "1:6:1:-12.5:5:10:25:0.625:0:7:60:1:0.5:-60:33"),
        ("stage_ms", "-31.250:12.500:25.000:62.500"),
        ("phase", "0.500000"),
        ("sweep", "0.020500"),
        ("ms_per_cycle", "250.000"),
        ("fires", "1"),
        ("duck", "0.3649"),
        ("key_level", "0.00000"),
        ("advancing", "1"),
        ("dropped", "0"),
        ("rate_label", "1/8"),
        ("curve_label", "Linear"),
        ("source_label", "MIDI"),
        ("source", "1"),
        ("rate", "6"),
        ("time_mode", "1"),
        ("delay", "-12.5"),
        ("attack", "5"),
        ("hold", "10"),
        ("release", "25"),
        ("depth", "0.625"),
        ("curve", "0"),
        ("channel", "7"),
        ("trigger_note", "60"),
        ("midi_mode", "1"),
        ("vel_sens", "0.5"),
        ("threshold", "-60"),
        ("lockout", "33"),
    ];
    let p = a_state_on_the_wire();
    let mut out = [0u8; 256];
    for (key, want) in WIRE {
        let n = p.get_param(key, &mut out);
        assert!(n >= 0, "{key} is not served");
        assert_eq!(std::str::from_utf8(&out[..n as usize]).unwrap(), want, "{key}");
    }
    /* A panic is something to do, not a value to read. */
    assert_eq!(p.get_param("panic", &mut out), -1);
}

#[test]
fn every_written_word_reads_back_as_it_always_has() {
    /*
     * C's leniency IS the protocol: a label or an index for an enum, `atof`
     * for a number ("4abc" is 4, "abc" is 0), a note's name or its number, and
     * a clamp rather than a refusal. Each word is written to a fresh engine
     * and read back through the same key.
     */
    const WRITES: [(&str, &str, &str); 63] = [
        ("source", "MIDI", "1"),
        ("source", "Sidechain", "2"),
        ("source", "2", "2"),
        ("source", " 1 junk", "1"),
        ("source", "junk", "0"),
        ("source", "7", "0"),
        ("rate", "1/8T", "7"),
        ("rate", " 7 junk", "7"),
        ("rate", "11", "11"),
        ("rate", "12", "4"),
        ("rate", "-1", "4"),
        ("rate", "1/64", "1"),
        ("time_mode", "% of cycle", "1"),
        ("time_mode", "ms", "0"),
        ("time_mode", "1", "1"),
        ("time_mode", "5", "1"),
        ("curve", "S-Curve", "2"),
        ("curve", "Exponential", "1"),
        ("curve", "Linear", "0"),
        ("curve", "2", "2"),
        ("curve", "9", "0"),
        ("midi_mode", "Gate", "1"),
        ("midi_mode", "Trigger", "0"),
        ("midi_mode", "1", "1"),
        ("midi_mode", "gate", "0"),
        ("channel", "Omni", "0"),
        ("channel", "16", "16"),
        ("channel", "17", "16"),
        ("channel", "-3", "0"),
        ("channel", "3.9", "3"),
        ("delay", "-12.5", "-12.5"),
        ("delay", "-250", "-100"),
        ("delay", "0.125abc", "0.125"),
        ("attack", "0.750", "0.75"),
        ("attack", "4abc", "4"),
        ("attack", "1e1", "10"),
        ("attack", "1e", "1"),
        ("attack", ".5", "0.5"),
        ("attack", "999", "200"),
        ("hold", " +40", "40"),
        ("hold", "-3", "0"),
        ("hold", "abc", "0"),
        ("hold", "", "0"),
        ("release", "111.5", "111.5"),
        ("release", "1e400", "200"),
        ("depth", "0.7", "0.7"),
        ("depth", "1.5", "1"),
        ("depth", "nan", "0"),
        ("trigger_note", "F#3", "66"),
        ("trigger_note", "C-2", "0"),
        ("trigger_note", "G8", "127"),
        ("trigger_note", "36", "36"),
        ("trigger_note", "60.9", "60"),
        ("trigger_note", "G#8", "0"),
        ("trigger_note", "200", "127"),
        ("vel_sens", "0.25", "0.25"),
        ("vel_sens", "-1", "0"),
        ("threshold", "-60", "-60"),
        ("threshold", "-90", "-60"),
        ("threshold", "5", "0"),
        ("threshold", "0", "0"),
        ("lockout", "30", "30"),
        ("lockout", "1000", "200"),
    ];
    let mut out = [0u8; 64];
    for (key, val, want) in WRITES {
        let mut p = Instance::new(48000.0);
        assert!(p.set_param(key, val), "{key} refused {val:?}");
        let n = p.get_param(key, &mut out);
        assert!(n >= 0, "{key} is not served");
        assert_eq!(std::str::from_utf8(&out[..n as usize]).unwrap(), want, "{key} = {val:?}");
    }
    /* Not a parameter, and still the string door's: see midi.rs. */
    assert!(Instance::new(48000.0).set_param("panic", "1"));
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

/* ---------------------------------------------------------------- state */

fn state_of(p: &Instance) -> String {
    let mut out = [0u8; 512];
    let n = p.get_param("state", &mut out);
    assert!(n >= 0, "state is not served");
    std::str::from_utf8(&out[..n as usize]).unwrap().to_string()
}

fn values(p: &Instance) -> Vec<f64> {
    Param::ALL.into_iter().map(|q| p.num(q)).collect()
}

#[test]
fn a_saved_state_restores_every_parameter_exactly() {
    /*
     * Schwung saves a module as ONE blob and restores it into a fresh
     * instance; without one, its autosave abandoned the whole slot. Every
     * parameter at a non-default value, compared bit for bit, so a value
     * that rounds on the way out is caught as surely as one left behind.
     */
    let saved = a_state_on_the_wire();
    let mut fresh = Instance::new(48000.0);
    assert_ne!(values(&fresh), values(&saved), "the fixture must differ from the defaults");
    assert!(fresh.set_param("state", &state_of(&saved)));
    for (q, (got, want)) in Param::ALL.into_iter().zip(values(&fresh).into_iter().zip(values(&saved))) {
        assert_eq!(got.to_bits(), want.to_bits(), "{}: {got} != {want}", q.key());
    }
    assert_eq!(state_of(&fresh), state_of(&saved));
}

#[test]
fn a_value_that_needs_every_digit_survives_the_round_trip() {
    let mut p = Instance::new(48000.0);
    p.set_num(Param::Depth, 1.0 / 3.0);
    p.set_num(Param::Threshold, -17.123456789012345);
    let mut q = Instance::new(48000.0);
    q.set_param("state", &state_of(&p));
    assert_eq!(q.num(Param::Depth).to_bits(), p.num(Param::Depth).to_bits());
    assert_eq!(q.num(Param::Threshold).to_bits(), p.num(Param::Threshold).to_bits());
}

#[test]
fn a_key_the_blob_lacks_keeps_its_value_and_one_it_does_not_know_is_passed_over() {
    let mut p = Instance::new(48000.0);
    p.set_num(Param::Hold, 44.0);
    /* An older build's blob (no hold), a newer build's key, a field with no
     * value and one that is not a number: each costs only itself. */
    p.set_param("state", &format!("{STATE_TAG};depth=0.25;wobble=3;attack;release=fast;lockout=12"));
    assert_eq!(p.num(Param::Depth), 0.25);
    assert_eq!(p.num(Param::Lockout), 12.0);
    assert_eq!(p.num(Param::Hold), 44.0, "a key the blob lacks keeps what the engine held");
    let defaults = Instance::new(48000.0);
    assert_eq!(p.num(Param::Release), defaults.num(Param::Release), "a word is not a number");
    assert_eq!(p.num(Param::Attack), defaults.num(Param::Attack));
}

#[test]
fn a_text_without_the_tag_loads_nothing() {
    for text in ["", "depth=0.25", "{\"depth\":0.25}", "sc2;depth=0.25", "SC1;depth=0.25"] {
        let mut p = Instance::new(48000.0);
        let before = values(&p);
        assert!(p.set_param("state", text), "state is this engine's key, blob or not");
        assert_eq!(values(&p), before, "{text:?} loaded something");
    }
}

#[test]
fn a_loaded_value_is_clamped_like_any_other() {
    let mut p = Instance::new(48000.0);
    p.set_param("state", &format!("{STATE_TAG};depth=7;attack=-3;lockout=NaN"));
    assert_eq!(p.num(Param::Depth), 1.0);
    assert_eq!(p.num(Param::Attack), 0.0);
    assert_eq!(p.num(Param::Lockout), Instance::new(48000.0).num(Param::Lockout), "NaN is dropped");
}
