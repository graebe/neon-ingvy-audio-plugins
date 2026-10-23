// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! The name table: one name per mode, one entry per family, and the
//! reference mode the family is known by.

use std::collections::HashMap;

use music_core::{Scale, ScaleFamily};

#[test]
fn no_two_modes_share_a_name() {
    let mut seen: HashMap<&str, Scale> = HashMap::new();
    for rest in 0u16..2048 {
        let scale = Scale::from_bits((rest << 1) | 1).unwrap();
        if let Some(name) = scale.name() {
            if let Some(other) = seen.insert(name, scale) {
                panic!("{name} names both {other} and {scale}");
            }
            assert!(
                scale.family().name().is_some(),
                "{name}'s family has no name"
            );
        }
    }
    assert!(seen.len() > 80, "only {} named modes", seen.len());
}

#[test]
fn every_named_family_is_a_real_family_once() {
    let named: Vec<ScaleFamily> = ScaleFamily::named().collect();
    for (i, a) in named.iter().enumerate() {
        assert_eq!(a.reference().family(), *a);
        assert!(a.name().is_some());
        assert_eq!(a.abbreviation().map(str::len), Some(3));
        for b in &named[i + 1..] {
            assert_ne!(a, b, "{a} is listed twice");
        }
    }
}

#[test]
fn reference_modes_are_the_familiar_ones() {
    let reference = |scale: Scale| scale.family().reference().name();
    assert_eq!(reference(Scale::MAJOR.mode(5)), Some("Ionian"));
    assert_eq!(
        reference(Scale::MELODIC_MINOR.mode(6)),
        Some("Melodic minor")
    );
    assert_eq!(
        reference(Scale::HARMONIC_MINOR.mode(4)),
        Some("Harmonic minor")
    );
    assert_eq!(reference(Scale::MINOR_PENTATONIC), Some("Major pentatonic"));
    assert_eq!(
        reference(Scale::OCTATONIC.mode(1)),
        Some("Half-whole diminished")
    );
}

#[test]
fn the_church_modes_agree_with_key_modes() {
    for mode in music_core::Mode::ALL {
        let scale = Scale::new(&mode.steps());
        assert_eq!(scale.name(), Some(mode.name()));
    }
}
