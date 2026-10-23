// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! Scales, their modes and families: the counts a periodic table of scales
//! is built from, checked over the whole universe of 2,048 rooted sets.

use music_core::{Interval, PitchSet, Scale, ScaleDegree, ScaleFamily};
use proptest::prelude::*;

/// Every rooted set: the root plus any choice of the other eleven notes.
fn every_scale() -> impl Iterator<Item = Scale> {
    (0u16..2048).filter_map(|rest| Scale::from_bits((rest << 1) | 1))
}

fn degrees(scale: Scale) -> String {
    scale.to_string()
}

#[test]
fn the_universe_has_2036_scales_in_344_families() {
    let families: Vec<ScaleFamily> = ScaleFamily::all(3).collect();
    assert_eq!(families.len(), 344);
    let rooted: u32 = families.iter().map(|f| f.mode_count()).sum();
    assert_eq!(rooted, 2036);

    // With single notes and intervals, every rooted set belongs somewhere.
    let all: Vec<ScaleFamily> = ScaleFamily::all(1).collect();
    assert_eq!(all.len(), 351);
    assert_eq!(all.iter().map(|f| f.mode_count()).sum::<u32>(), 2048);
    assert_eq!(every_scale().count(), 2048);
}

#[test]
fn families_per_mode_count_match_the_table() {
    let mut per_m = [0u32; 13];
    for family in ScaleFamily::all(3) {
        per_m[family.mode_count() as usize] += 1;
    }
    assert_eq!(per_m, [0, 4, 4, 22, 42, 67, 75, 66, 40, 18, 5, 1, 0]);
}

#[test]
fn families_per_note_count_are_the_binary_necklaces() {
    let mut per_n = [0u32; 13];
    for family in ScaleFamily::all(1) {
        per_n[family.len() as usize] += 1;
    }
    assert_eq!(per_n, [0, 1, 6, 19, 43, 66, 80, 66, 43, 19, 6, 1, 1]);
}

#[test]
fn fifteen_families_are_symmetric() {
    let symmetric: Vec<ScaleFamily> = ScaleFamily::all(3).filter(|f| f.symmetry() > 1).collect();
    assert_eq!(symmetric.len(), 15);
    for family in symmetric {
        assert!(family.name().is_some(), "{family} has no name");
    }
}

#[test]
fn counts_agree_for_every_scale() {
    for scale in every_scale() {
        let (n, m, s, t) = (
            scale.len(),
            scale.mode_count(),
            scale.symmetry(),
            scale.transpositions(),
        );
        assert_eq!(n, m * s, "{scale}");
        assert_eq!(12, t * s, "{scale}");
        assert_eq!(scale.modes().count() as u32, m, "{scale}");
        // The modes are all different, and all in one family.
        let modes: Vec<Scale> = scale.modes().collect();
        for (i, a) in modes.iter().enumerate() {
            assert_eq!(a.family(), scale.family());
            for b in &modes[i + 1..] {
                assert_ne!(a, b, "{scale}");
            }
        }
        // Rotating through every degree visits only those modes.
        for degree in 0..n {
            assert!(
                modes.contains(&scale.mode(degree)),
                "{scale} degree {degree}"
            );
        }
        assert_eq!(scale.mode(n), scale);
    }
}

#[test]
fn the_canonical_mode_is_the_smallest() {
    for family in ScaleFamily::all(1) {
        let smallest = family.modes().map(Scale::bits).min();
        assert_eq!(Some(family.canonical().bits()), smallest);
    }
    // Rahn's normal order of the diatonic set is Locrian.
    assert_eq!(Scale::MAJOR.family().canonical(), Scale::MAJOR.mode(6));
}

#[test]
fn known_families_have_known_portraits() {
    let diatonic = Scale::MAJOR.family();
    assert_eq!(
        (
            diatonic.len(),
            diatonic.mode_count(),
            diatonic.symmetry(),
            diatonic.transpositions()
        ),
        (7, 7, 1, 12)
    );
    assert_eq!(diatonic.interval_vector(), [2, 5, 4, 3, 6, 1]);

    let whole_tone = Scale::WHOLE_TONE.family();
    assert_eq!(
        (
            whole_tone.len(),
            whole_tone.mode_count(),
            whole_tone.symmetry(),
            whole_tone.transpositions()
        ),
        (6, 1, 6, 2)
    );
    assert_eq!(whole_tone.interval_vector(), [0, 6, 0, 6, 0, 3]);

    let octatonic = Scale::OCTATONIC.family();
    assert_eq!(
        (
            octatonic.len(),
            octatonic.mode_count(),
            octatonic.symmetry(),
            octatonic.transpositions()
        ),
        (8, 2, 4, 3)
    );
    assert_eq!(octatonic.interval_vector(), [4, 4, 8, 4, 4, 4]);

    let chromatic = Scale::CHROMATIC.family();
    assert_eq!((chromatic.mode_count(), chromatic.symmetry()), (1, 12));
    assert_eq!(chromatic.interval_vector(), [12, 12, 12, 12, 12, 6]);
}

#[test]
fn interval_vectors_count_every_pair() {
    for scale in every_scale() {
        let n = scale.len();
        let total: u32 = scale
            .pitch_set()
            .interval_vector()
            .iter()
            .map(|&c| u32::from(c))
            .sum();
        assert_eq!(total, n * (n - 1) / 2, "{scale}");
    }
}

#[test]
fn steps_add_up_to_an_octave() {
    for scale in every_scale() {
        assert_eq!(scale.steps().map(u32::from).sum::<u32>(), 12, "{scale}");
        assert_eq!(scale.steps().len() as u32, scale.len());
    }
    let steps: Vec<u8> = Scale::MAJOR.steps().collect();
    assert_eq!(steps, [2, 2, 1, 2, 2, 2, 1]);
}

#[test]
fn constants_are_what_they_say() {
    assert_eq!(Scale::MAJOR.name(), Some("Ionian"));
    assert_eq!(Scale::NATURAL_MINOR.name(), Some("Aeolian"));
    assert_eq!(Scale::HARMONIC_MINOR.name(), Some("Harmonic minor"));
    assert_eq!(Scale::MELODIC_MINOR.name(), Some("Melodic minor"));
    assert_eq!(Scale::MAJOR_PENTATONIC.name(), Some("Major pentatonic"));
    assert_eq!(Scale::MINOR_PENTATONIC.name(), Some("Minor pentatonic"));
    assert_eq!(Scale::WHOLE_TONE.name(), Some("Whole tone"));
    assert_eq!(Scale::OCTATONIC.name(), Some("Half-whole diminished"));
    assert_eq!(Scale::CHROMATIC.name(), Some("Chromatic"));
    assert_eq!(Scale::default(), Scale::MAJOR);
}

#[test]
fn construction_requires_the_root() {
    assert_eq!(Scale::from_bits(0b10), None);
    assert_eq!(Scale::from_bits(0x1001), None);
    assert_eq!(Scale::from_pitch_set(PitchSet::EMPTY), None);
    assert_eq!(Scale::new(&[]), Scale::from_bits(1).unwrap());
    assert_eq!(Scale::new(&[14, 4, 7]).to_string(), "1 2 3 5");
}

#[test]
fn degrees_are_spelled_as_scores_write_them() {
    let cases: [(&[u8], &str); 12] = [
        (&[0, 2, 4, 5, 7, 9, 11], "1 2 3 4 5 6 7"),
        (&[0, 2, 4, 6, 7, 9, 11], "1 2 3 #4 5 6 7"),
        (&[0, 1, 3, 5, 6, 8, 10], "1 b2 b3 4 b5 b6 b7"),
        (&[0, 2, 4, 6, 8, 10], "1 2 3 b5 b6 b7"),
        (&[0, 3, 4, 7, 10], "1 #2 3 5 b7"),
        (&[0, 4, 7, 10], "1 3 5 b7"),
        (&[0, 3, 5, 6, 7, 10], "1 b3 4 b5 5 b7"),
        (&[0, 1, 3, 4, 7, 8, 10], "1 b2 b3 3 5 b6 b7"),
        (&[0, 1, 3, 4, 6, 7, 9, 10], "1 b2 b3 3 #4 5 6 b7"),
        (&[0, 2, 3, 5, 7, 8, 11], "1 2 b3 4 5 b6 7"),
        (&[0, 2, 4, 5, 7, 9, 10, 11], "1 2 3 4 5 6 b7 7"),
        (
            &[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11],
            "1 b2 2 b3 3 4 b5 5 b6 6 b7 7",
        ),
    ];
    for (semitones, spelled) in cases {
        assert_eq!(degrees(Scale::new(semitones)), spelled);
    }
}

#[test]
fn every_spelling_names_its_own_notes() {
    for scale in every_scale() {
        let mut previous = 0;
        for (degree, spelled) in scale.degrees().enumerate() {
            assert_eq!(
                spelled.semitones(),
                scale.semitones_of(degree as u32),
                "{scale}"
            );
            assert!(spelled.number() >= previous, "{scale}");
            assert!(spelled.accidental().abs() <= 1, "{scale}");
            previous = spelled.number();
        }
        assert_eq!(scale.degrees().next(), Some(ScaleDegree::new(1, 0)));
    }
}

#[test]
fn scale_degrees_wrap_and_print() {
    assert_eq!(ScaleDegree::new(9, 1), ScaleDegree::new(2, 1));
    assert_eq!(ScaleDegree::new(9, 1).to_string(), "#2");
    assert_eq!(ScaleDegree::new(7, -1).semitones(), 10);
    assert_eq!(format!("{:?}", ScaleDegree::new(3, -1)), "b3");
    assert_eq!(format!("{:>4}", ScaleDegree::new(5, 0)), "   5");
}

#[test]
fn brightness_orders_the_church_modes() {
    let lydian = Scale::MAJOR.mode(3);
    let ionian = Scale::MAJOR;
    let mixolydian = Scale::MAJOR.mode(4);
    let dorian = Scale::MAJOR.mode(1);
    let aeolian = Scale::MAJOR.mode(5);
    let phrygian = Scale::MAJOR.mode(2);
    let locrian = Scale::MAJOR.mode(6);

    let close = |a: f32, b: f32| (a - b).abs() < 1e-3;
    assert!(close(lydian.brightness(), 1.0));
    assert!(close(ionian.brightness(), 14.0 / 15.0));
    assert!(close(mixolydian.brightness(), 7.0 / 15.0));
    assert!(close(dorian.brightness(), 0.0));
    assert!(close(aeolian.brightness(), -7.0 / 15.0));
    assert!(close(phrygian.brightness(), -14.0 / 15.0));
    assert!(close(locrian.brightness(), -1.0));

    assert!(close(Scale::MAJOR_PENTATONIC.brightness(), 10.0 / 14.0));
    assert!(close(Scale::MINOR_PENTATONIC.brightness(), -5.0 / 14.0));
    assert!(close(Scale::WHOLE_TONE.brightness(), 0.0));
    assert!(close(Scale::CHROMATIC.brightness(), 0.0));
}

#[test]
fn brightness_flips_under_inversion_and_stays_in_range() {
    for scale in every_scale() {
        let b = scale.brightness();
        assert!((-1.0..=1.0).contains(&b), "{scale}: {b}");
        assert_eq!(
            scale.inverted().fifths_sum(),
            -scale.fifths_sum(),
            "{scale}"
        );
        assert_eq!(scale.inverted().inverted(), scale);
    }
    // Every size reaches its extremes.
    for n in 2..12 {
        let sums: Vec<i8> = every_scale()
            .filter(|s| s.len() == n)
            .map(Scale::fifths_sum)
            .collect();
        let most = sums.iter().copied().max().unwrap();
        let best = every_scale()
            .find(|s| s.len() == n && s.fifths_sum() == most)
            .unwrap();
        assert_eq!(best.brightness(), 1.0, "size {n}");
    }
}

#[test]
fn whole_tone_splits_into_two_augmented_triads() {
    let [first, second] = Scale::WHOLE_TONE.component_triads().unwrap();
    assert_eq!(first.to_string(), "Caug");
    assert_eq!(second.to_string(), "Daug");
    assert_eq!(
        first.pitches() | second.pitches(),
        Scale::WHOLE_TONE.pitch_set()
    );

    // The augmented scale: C major and A flat minor (G sharp minor).
    let augmented = Scale::new(&[0, 3, 4, 7, 8, 11]);
    let [a, b] = augmented.component_triads().unwrap();
    assert_eq!((a.pitches() | b.pitches()), augmented.pitch_set());

    assert_eq!(Scale::MAJOR.component_triads(), None);
    assert_eq!(Scale::new(&[0, 1, 2, 3, 4, 5]).component_triads(), None);
}

#[test]
fn chords_on_the_root_fit_inside() {
    let qualities: Vec<String> = Scale::MAJOR
        .chords()
        .map(|q| q.symbol().to_string())
        .collect();
    assert!(qualities.contains(&"maj7".to_string()));
    assert!(!qualities.contains(&"7".to_string()));
    assert_eq!(
        Scale::CHROMATIC.chords().count(),
        music_core::ChordQuality::ALL.len()
    );
}

#[test]
fn a_scale_sits_on_any_tonic() {
    let g_major = Scale::MAJOR.on(music_core::Pitch::G);
    assert_eq!(
        g_major,
        Scale::MAJOR.pitch_set().transpose(Interval::new(7))
    );
    assert!(Scale::MAJOR.is_mode_of(Scale::MAJOR.mode(3)));
    assert!(!Scale::MAJOR.is_mode_of(Scale::HARMONIC_MINOR));
}

#[test]
fn families_print_their_name_or_degrees() {
    assert_eq!(Scale::MAJOR.family().to_string(), "Diatonic");
    assert_eq!(Scale::MAJOR.family().abbreviation(), Some("Dia"));
    let unnamed = Scale::new(&[0, 1, 2, 4, 7]).family();
    assert_eq!(unnamed.name(), None);
    assert_eq!(unnamed.reference(), unnamed.canonical());
    assert_eq!(format!("{unnamed:?}"), unnamed.canonical().to_string());
}

proptest! {
    #[test]
    fn modes_and_families_agree(rest in 0u16..2048, degree in 0u32..24) {
        let scale = Scale::from_bits((rest << 1) | 1).unwrap();
        let mode = scale.mode(degree);
        prop_assert_eq!(mode.family(), scale.family());
        prop_assert_eq!(mode.symmetry(), scale.symmetry());
        prop_assert_eq!(mode.pitch_set().interval_vector(), scale.pitch_set().interval_vector());
        prop_assert!(mode.is_mode_of(scale));
    }
}
