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
fn every_default_sits_inside_its_range_and_bipolar_ranges_are_symmetric() {
    for p in Param::ALL {
        let d = p.default_value();
        assert_eq!(p.clamp(d), d, "{p:?}");
        if let Kind::Float { min, max, bipolar, .. } = p.info().kind {
            assert!(min < max, "{p:?}");
            if bipolar {
                assert_eq!(min, -max, "{p:?}");
            }
        }
    }
}

#[test]
fn the_defaults_are_a_vowel_that_is_heard() {
    assert_eq!(Param::Mode.default_value(), 0.0);
    assert_eq!(Param::Vocal.default_value(), 80.0);
    assert_eq!(Param::Vowel.default_value(), 0.0);
    assert_eq!(Param::Mix.default_value(), 100.0);
    assert_eq!(Param::Excite.default_value(), 0.0);
    /* A captured voice is heard whole as soon as there is one. */
    assert_eq!(Param::Voice.default_value(), 100.0);
    /* The ceiling is on, and a host may switch it. */
    assert_eq!(Param::Ceiling.default_value(), 1.0);
    assert!(matches!(Param::Ceiling.info().kind, Kind::Choice { automatable: true, .. }));
    assert!(matches!(Param::Mode.info().kind, Kind::Choice { automatable: false, .. }));
    assert_eq!(text(Param::Ceiling, 0.0), "Off");
    assert_eq!(Param::Ceiling.parse_value("on"), Some(1.0));
}

#[test]
fn a_choice_is_its_nearest_index() {
    let m = Param::Mode;
    assert_eq!(m.clamp(0.49), 0.0);
    assert_eq!(m.clamp(0.5), 1.0);
    assert_eq!(m.clamp(7.0), 1.0);
    assert_eq!(m.clamp(-2.0), 0.0);
    assert_eq!(m.snap(0.7), 1.0);
    assert_eq!(m.normalise(1.0), 1.0);
    assert_eq!(m.denormalise(0.2), 0.0);
    assert_eq!(m.denormalise(0.8), 1.0);
    assert_eq!(text(m, 0.0), "Time");
    assert_eq!(text(m, 1.0), "Spectral");
    assert_eq!(m.parse_value("spectral"), Some(1.0));
    assert_eq!(m.parse_value("TIME"), Some(0.0));
    assert_eq!(m.parse_value("1"), Some(1.0));
    assert_eq!(m.parse_value("fast"), None);
}

#[test]
fn every_float_travels_linearly() {
    let p = Param::Size;
    assert_eq!(p.normalise(-100.0), 0.0);
    assert_eq!(p.normalise(0.0), 0.5);
    assert_eq!(p.denormalise(1.0), 100.0);
    assert_eq!(p.denormalise(f64::NAN), -100.0);
    assert_eq!(p.denormalise(-3.0), -100.0);
}

#[test]
fn the_vowel_prints_as_a_letter_or_a_way_between_two() {
    let v = Param::Vowel;
    assert_eq!(text(v, 0.0), "A");
    assert_eq!(text(v, 1.0), "E");
    assert_eq!(text(v, 4.0), "U");
    assert_eq!(text(v, 1.3), "E>I 30 %");
    assert_eq!(text(v, 3.5), "O>U 50 %");
    /* Within half a percent of a vowel, it is that vowel. */
    assert_eq!(text(v, 2.004), "I");
    assert_eq!(text(v, 2.996), "O");
    assert_eq!(text(v, f64::NAN), "A");
}

#[test]
fn the_vowel_reads_a_letter_a_way_between_or_a_number() {
    let v = Param::Vowel;
    assert_eq!(v.parse_value("i"), Some(2.0));
    assert_eq!(v.parse_value(" O "), Some(3.0));
    assert_eq!(v.parse_value("E 30"), Some(1.3));
    assert_eq!(v.parse_value("E>I 30 %"), Some(1.3));
    assert_eq!(v.parse_value("U 50"), Some(4.0));
    assert_eq!(v.parse_value("2.5"), Some(2.5));
    assert_eq!(v.parse_value(".5"), Some(0.5));
    assert_eq!(v.parse_value("9"), Some(4.0));
    assert_eq!(v.parse_value("x"), None);
    assert_eq!(v.parse_value(""), None);
}

#[test]
fn levels_and_percentages_read_as_every_shell_shows_them() {
    assert_eq!(text(Param::Output, 3.0), "+3.0 dB");
    assert_eq!(text(Param::Output, -0.04), "0.0 dB");
    assert_eq!(text(Param::Output, -6.0), "-6.0 dB");
    assert_eq!(text(Param::Vocal, 80.0), "80 %");
    assert_eq!(text(Param::Size, 40.0), "+40 %");
    assert_eq!(text(Param::Size, -40.0), "-40 %");
    assert_eq!(text(Param::Size, -0.2), "0 %");
    assert_eq!(Param::Output.parse_value("-6 dB"), Some(-6.0));
    assert_eq!(Param::Vocal.parse_value("250"), Some(100.0));
    assert_eq!(Param::Vocal.parse_value("loud"), None);
    assert_eq!(Unit::Vowel.label(), "");
    assert_eq!(Unit::Percent.label(), "%");
    assert_eq!(Unit::Db.label(), "dB");
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
    fn every_printed_vowel_reads_back_within_a_percent(v in 0.0f64..=4.0) {
        let back = Param::Vowel.parse_value(&text(Param::Vowel, v)).unwrap();
        prop_assert!((back - v).abs() <= 0.005 + 1e-9, "{} -> {}", v, back);
    }

    #[test]
    fn a_snapped_value_snaps_to_itself(i in 0usize..PARAM_COUNT, n in 0.0f64..=1.0) {
        let p = Param::ALL[i];
        let s = p.snap(p.denormalise(n));
        prop_assert_eq!(p.snap(s), s);
    }
}
