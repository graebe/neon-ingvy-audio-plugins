// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

use super::*;
use proptest::prelude::*;

fn text(p: Param, v: f64) -> String {
    let mut s = String::new();
    p.write_value(&mut s, v).unwrap();
    s
}

#[test]
fn every_index_is_its_parameter_and_every_key_is_unique() {
    for (i, p) in Param::ALL.iter().enumerate() {
        assert_eq!(*p as usize, i);
        assert_eq!(Param::from_i32(i as i32), Some(*p));
        assert_eq!(Param::from_key(p.info().key), Some(*p));
    }
    assert_eq!(Param::from_i32(-1), None);
    assert_eq!(Param::from_i32(PARAM_COUNT as i32), None);
    assert_eq!(Param::from_key("nope"), None);
    let mut keys: Vec<_> = Param::ALL.iter().map(|p| p.info().key).collect();
    keys.sort_unstable();
    keys.dedup();
    assert_eq!(keys.len(), PARAM_COUNT);
}

#[test]
fn every_default_sits_inside_its_range() {
    for p in Param::ALL {
        let d = p.default_value();
        assert_eq!(p.clamp(d), d, "{p:?}");
        if let Kind::Float { min, max, .. } = p.info().kind {
            assert!(min < max, "{p:?}");
        }
    }
}

#[test]
fn the_defaults_are_a_crusher_that_is_heard() {
    assert_eq!(Param::Bits.default_value(), 8.0);
    assert_eq!(Param::Rate.default_value(), 12_000.0);
    assert_eq!(Param::Mix.default_value(), 100.0);
    assert_eq!(Param::MidRise.default_value(), 0.0);
}

#[test]
fn a_toggle_is_on_from_one_half_up() {
    assert_eq!(Param::AntiAlias.clamp(0.49), 0.0);
    assert_eq!(Param::AntiAlias.clamp(0.5), 1.0);
    assert_eq!(Param::AntiAlias.clamp(7.0), 1.0);
    assert_eq!(Param::AntiAlias.normalise(1.0), 1.0);
    assert_eq!(Param::AntiAlias.denormalise(0.2), 0.0);
}

#[test]
fn rate_travels_in_equal_ratios() {
    let p = Param::Rate;
    assert_eq!(p.normalise(50.0), 0.0);
    assert!((p.normalise(96_000.0) - 1.0).abs() < 1e-12);
    /* The geometric middle of the range is the middle of the turn. */
    let mid = (50.0f64 * 96_000.0).sqrt();
    assert!((p.normalise(mid) - 0.5).abs() < 1e-12);
    assert!((p.denormalise(0.5) - mid).abs() < 1e-6);
    assert_eq!(p.denormalise(-3.0), 50.0);
    assert_eq!(p.denormalise(f64::NAN), 50.0);
}

proptest! {
    #[test]
    fn normalise_and_denormalise_are_inverses(i in 0usize..PARAM_COUNT, n in 0.0f64..=1.0) {
        let p = Param::ALL[i];
        let v = p.denormalise(n);
        prop_assert_eq!(p.clamp(v), v);
        if let Kind::Float { .. } = p.info().kind {
            prop_assert!((p.normalise(v) - n).abs() < 1e-9);
        }
    }

    #[test]
    fn whatever_is_typed_lands_inside_the_range(i in 0usize..PARAM_COUNT, s in "\\PC{0,12}") {
        let p = Param::ALL[i];
        if let Some(v) = p.parse_value(&s) {
            prop_assert_eq!(p.clamp(v), v);
        }
    }

    #[test]
    fn a_printed_value_reads_back_close_to_itself(i in 0usize..PARAM_COUNT, n in 0.0f64..=1.0) {
        let p = Param::ALL[i];
        let v = p.denormalise(n);
        let back = p.parse_value(&text(p, v)).expect("a printed value reads back");
        /* To the printed precision: a tenth, a whole percent, three figures
         * of a rate. */
        let tolerance = match p.info().kind {
            Kind::Float { unit: Unit::Percent, .. } => 0.5,
            Kind::Float { unit: Unit::Hz, .. } => (v * 0.006).max(0.5),
            Kind::Float { .. } => 0.05,
            Kind::Toggle { .. } => 0.0,
        };
        prop_assert!((back - v).abs() <= tolerance + 1e-9, "{} -> {}", v, back);
    }
}

#[test]
fn values_print_in_their_units() {
    assert_eq!(text(Param::Drive, 3.0), "+3.0 dB");
    assert_eq!(text(Param::Drive, -0.04), "0.0 dB");
    assert_eq!(text(Param::Output, -6.0), "-6.0 dB");
    assert_eq!(text(Param::Bits, 7.25), "7.2 bits");
    assert_eq!(text(Param::Shape, 40.4), "40 %");
    assert_eq!(text(Param::Rate, 440.0), "440 Hz");
    assert_eq!(text(Param::Rate, 4_410.0), "4.41 kHz");
    assert_eq!(text(Param::Rate, 12_000.0), "12.0 kHz");
    assert_eq!(text(Param::PostOctave, -1.5), "-1.5 oct");
    assert_eq!(text(Param::PreFilter, 1.0), "On");
    assert_eq!(text(Param::PreFilter, 0.0), "Off");
    assert_eq!(text(Param::Bits, f64::NAN), "8.0 bits");
    assert_eq!(text(Param::Bits, 99.0), "16.0 bits");
}

#[test]
fn typing_is_read_as_c_reads_numbers_with_a_rate_in_kilohertz() {
    assert_eq!(Param::Rate.parse_value("12k"), Some(12_000.0));
    assert_eq!(Param::Rate.parse_value(" 12.5 kHz"), Some(12_500.0));
    assert_eq!(Param::Rate.parse_value("8000"), Some(8_000.0));
    assert_eq!(Param::Rate.parse_value("1"), Some(50.0));
    assert_eq!(Param::Bits.parse_value("7.5 bits"), Some(7.5));
    assert_eq!(Param::Drive.parse_value("-3dB"), Some(-3.0));
    assert_eq!(Param::Drive.parse_value("+30"), Some(24.0));
    assert_eq!(Param::Drive.parse_value("loud"), None);
    assert_eq!(Param::PreFilter.parse_value("on"), Some(1.0));
    assert_eq!(Param::PreFilter.parse_value("Off"), Some(0.0));
    assert_eq!(Param::PreFilter.parse_value("1"), Some(1.0));
    assert_eq!(Param::PreFilter.parse_value("maybe"), None);
}

#[test]
fn units_and_flags_are_what_a_shell_shows() {
    assert_eq!(Unit::Db.label(), "dB");
    assert_eq!(Unit::Bits.label(), "bits");
    assert_eq!(Unit::Percent.label(), "%");
    assert_eq!(Unit::Hz.label(), "Hz");
    assert_eq!(Unit::Octaves.label(), "oct");
    let bipolar = |p: Param| matches!(p.info().kind, Kind::Float { bipolar: true, .. });
    assert!(bipolar(Param::Drive) && bipolar(Param::PostOctave) && bipolar(Param::Output));
    assert!(!bipolar(Param::Bits) && !bipolar(Param::Rate));
}

#[test]
fn a_value_on_the_resolution_survives_a_float_round_trip() {
    /* What a JUCE host does with a value: normalise it, keep it as an f32,
     * and denormalise it -- then the shell snaps it back. */
    for (p, v) in [
        (Param::Bits, 5.5),
        (Param::Rate, 7_000.0),
        (Param::Rate, 12_345.6),
        (Param::Shape, 30.0),
        (Param::Mix, 80.0),
        (Param::Output, -2.0),
        (Param::PostOctave, 0.5),
        (Param::Drive, 6.25),
    ] {
        let back = p.denormalise(f64::from(p.normalise(v) as f32));
        assert_eq!(p.snap(back), v, "{p:?}");
    }
    assert_eq!(Param::AntiAlias.snap(0.7), 1.0);
    assert_eq!(Param::Rate.snap(1e9), 96_000.0);
}

#[test]
fn a_bipolar_range_is_symmetric_so_its_centre_is_zero() {
    /* A bipolar knob draws its arc from 12 o'clock, the middle of its
     * travel: that has to be zero, or zero would show an arc. */
    for p in Param::ALL {
        if let Kind::Float { min, max, bipolar: true, travel, .. } = p.info().kind {
            assert_eq!(min, -max, "{p:?}");
            assert_eq!(travel, Travel::Linear, "{p:?}");
            assert_eq!(p.denormalise(0.5), 0.0, "{p:?}");
        }
    }
}
