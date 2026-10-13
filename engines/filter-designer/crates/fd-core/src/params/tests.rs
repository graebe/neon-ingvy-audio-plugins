// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

use super::*;

fn text(p: Param, v: f64) -> String {
    let mut s = String::new();
    p.write_value(&mut s, v).unwrap();
    s
}

#[test]
fn the_table_has_its_shape() {
    assert_eq!(PARAM_COUNT, 174);
    for i in 0..PARAM_COUNT {
        let p = Param::from_index(i).unwrap();
        assert_eq!(p.index(), i);
        assert_eq!(Param::from_key(p.info().key), Some(p));
    }
    assert_eq!(Param::from_index(PARAM_COUNT), None);
    assert_eq!(Param::from_i32(-1), None);
    let keys: std::collections::HashSet<_> = (0..PARAM_COUNT).map(|i| Param::from_index(i).unwrap().info().key).collect();
    assert_eq!(keys.len(), PARAM_COUNT);
    assert_eq!(Param::Node(0, Field::Type).info().key, "n01_type");
    assert_eq!(Param::Node(15, Field::Channel).info().key, "n16_channel");
    assert_eq!(Param::Node(9, Field::Freq).info().name, "Filter 10 Freq");
    assert_eq!(Param::Node(15, Field::Channel).index(), 173);
}

#[test]
fn the_labels_are_frozen() {
    /* A host keeps a choice as a fraction of its labels: these counts may
     * never change. */
    assert_eq!(TYPE_LABELS.len(), 10);
    assert_eq!(FAMILY_LABELS.len(), 6);
    assert_eq!(ORDER_LABELS.len(), 8);
    assert_eq!(CHANNEL_LABELS.len(), 5);
    assert_eq!(ANALYZER_LABELS.len(), 4);
    assert_eq!(SLOPE_LABELS.len(), 2);
    assert_eq!(TYPE_LABELS[0], "Empty");
}

#[test]
fn a_new_instance_is_empty_and_its_nodes_are_spread() {
    for slot in 0..NODES as u8 {
        assert_eq!(Param::Node(slot, Field::Type).default_value(), 0.0);
        assert_eq!(Param::Node(slot, Field::On).default_value(), 1.0);
        assert_eq!(Param::Node(slot, Field::Freq).default_value(), FREQ_DEFAULTS[slot as usize]);
        assert_eq!(Param::Node(slot, Field::Order).default_value(), 1.0);
    }
    assert_eq!(Param::IsoOn.default_value(), 0.0);
    assert_eq!(Param::Output.default_value(), 0.0);
    assert_eq!(Param::Analyzer.default_value(), 3.0);
    assert!(matches!(Param::Analyzer.info().kind, Kind::Choice { automatable: false, .. }));
}

#[test]
fn values_print_in_the_engines_words() {
    assert_eq!(text(Param::Output, 3.04), "+3.0 dB");
    assert_eq!(text(Param::Output, -0.04), "0.0 dB");
    assert_eq!(text(Param::IsoLow, -60.0), "-inf dB");
    assert_eq!(text(Param::IsoLow, -12.0), "-12.0 dB");
    let f = Param::Node(2, Field::Freq);
    assert_eq!(text(f, 1234.0), "1.23 kHz");
    assert_eq!(text(f, 12_345.0), "12.3 kHz");
    assert_eq!(text(f, 250.0), "250 Hz");
    assert_eq!(text(f, 31.5), "31.5 Hz");
    assert_eq!(text(Param::Node(0, Field::Q), core::f64::consts::FRAC_1_SQRT_2), "0.71");
    assert_eq!(text(Param::Node(0, Field::Q), 12.0), "12.0");
    assert_eq!(text(Param::Node(0, Field::Type), 7.0), "Notch");
    assert_eq!(text(Param::Node(0, Field::Order), 3.0), "24 dB/oct");
    assert_eq!(text(Param::Scale, 100.0), "+100 %");
    assert_eq!(text(Param::Node(0, Field::Gain), f64::NAN), "0.0 dB");
}

#[test]
fn typing_is_read_in_the_parameters_units() {
    let f = Param::Node(0, Field::Freq);
    assert_eq!(f.parse_value("1.2k"), Some(1200.0));
    assert_eq!(f.parse_value("1.2 kHz"), Some(1200.0));
    assert_eq!(f.parse_value("440"), Some(440.0));
    assert_eq!(f.parse_value("99999"), Some(22_000.0));
    assert_eq!(Param::IsoMid.parse_value("-inf"), Some(-60.0));
    assert_eq!(Param::Node(0, Field::Type).parse_value("notch"), Some(7.0));
    assert_eq!(Param::Node(0, Field::Family).parse_value("Elliptic"), Some(4.0));
    assert_eq!(Param::Node(0, Field::Gain).parse_value("words"), None);
    assert_eq!(Param::Node(0, Field::Gain).parse_value("-6 dB"), Some(-6.0));
}

#[test]
fn ranges_travel_as_declared() {
    let q = Param::Node(0, Field::Q);
    assert!((q.normalise((0.1f64 * 40.0).sqrt()) - 0.5).abs() < 1e-12);
    assert_eq!(q.denormalise(1.0), 40.0);
    assert_eq!(Param::Output.normalise(0.0), 0.5);
    assert_eq!(Param::Node(0, Field::Freq).snap(1000.004), 1000.0);
    assert_eq!(Param::IsoSlope.clamp(7.0), 1.0);
}
