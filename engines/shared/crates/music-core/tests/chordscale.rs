// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! A chord against a scale: guide tones, avoid notes and the reduction of a
//! chord on any degree to a chord on a mode's root.

use music_core::{ChordQuality, ChordScale, Pitch, Scale, ToneRole};

fn pair(scale: Scale, quality: ChordQuality) -> ChordScale {
    ChordScale::from_parts(scale, quality).expect("the chord fits the scale")
}

fn roles(pair: ChordScale) -> String {
    Pitch::ALL.iter().map(|&p| pair.role(p).code()).collect()
}

#[test]
fn guide_tones_name_each_quality() {
    let guide =
        |q: ChordQuality| -> Vec<u8> { q.guide_tones().iter().map(|p| p.value()).collect() };
    assert_eq!(guide(ChordQuality::Major), [0, 4]);
    assert_eq!(guide(ChordQuality::Minor7), [0, 3, 10]);
    assert_eq!(guide(ChordQuality::Major7), [0, 4, 11]);
    assert_eq!(guide(ChordQuality::HalfDiminished7), [0, 3, 6, 10]);
    assert_eq!(guide(ChordQuality::Diminished7), [0, 3, 6, 9]);
    assert_eq!(guide(ChordQuality::Augmented), [0, 4, 8]);
    assert_eq!(guide(ChordQuality::Sus2), [0, 2]);
    assert_eq!(guide(ChordQuality::Dominant7Sus4), [0, 5, 10]);
    assert_eq!(guide(ChordQuality::Sixth), [0, 4, 9]);
    assert_eq!(guide(ChordQuality::SixNine), [0, 4, 9]);
    assert_eq!(guide(ChordQuality::Dominant13), [0, 4, 10]);
    assert_eq!(guide(ChordQuality::SevenSharpNine), [0, 4, 10]);
    assert_eq!(guide(ChordQuality::SevenSharpEleven), [0, 4, 10]);
    assert_eq!(guide(ChordQuality::SevenFlatFive), [0, 4, 6, 10]);
    assert_eq!(guide(ChordQuality::Fifth), [0]);
    for quality in ChordQuality::ALL {
        assert!(
            quality.guide_tones().is_subset(quality.interval_set()),
            "{quality:?}"
        );
    }
}

#[test]
fn roles_of_ionian_over_major_seven() {
    let ionian = pair(Scale::MAJOR, ChordQuality::Major7);
    // C . D . E F . G . A . B
    assert_eq!(roles(ionian), "G.t.Gx.c.t.G");
    assert_eq!(ionian.colour_tones().len(), 1);
    assert_eq!(ionian.outside().len(), 3);
    assert_eq!(ionian.tensions().len(), 2);
}

#[test]
fn clash_counts_avoid_notes() {
    let close = |a: f32, b: f32| (a - b).abs() < 1e-6;
    let mode = |degree| Scale::MAJOR.mode(degree);

    assert!(close(pair(mode(3), ChordQuality::Major7).clash(), 0.0)); // Lydian
    assert!(close(
        pair(mode(0), ChordQuality::Major7).clash(),
        1.0 / 3.0
    )); // Ionian
    assert!(close(pair(mode(1), ChordQuality::Minor7).clash(), 0.0)); // Dorian
    assert!(close(
        pair(mode(5), ChordQuality::Minor7).clash(),
        1.0 / 3.0
    )); // Aeolian
    assert!(close(
        pair(mode(2), ChordQuality::Minor7).clash(),
        2.0 / 3.0
    )); // Phrygian
    assert!(close(
        pair(mode(4), ChordQuality::Dominant7).clash(),
        1.0 / 3.0
    )); // Mixolydian

    // A chord that is the whole scale adds nothing and clashes with nothing.
    let triad = Scale::new(&[0, 4, 7]);
    assert_eq!(pair(triad, ChordQuality::Major).clash(), 0.0);
}

#[test]
fn a_chord_must_fit() {
    assert_eq!(
        ChordScale::from_parts(Scale::MAJOR, ChordQuality::Dominant7),
        None
    );
    assert_eq!(
        ChordScale::from_parts(Scale::MAJOR.mode(1), ChordQuality::Major),
        None
    );
    let p = pair(Scale::MAJOR, ChordQuality::Major);
    assert_eq!(p.scale(), Scale::MAJOR);
    assert_eq!(p.quality(), ChordQuality::Major);
}

#[test]
fn every_role_partitions_the_twelve_notes() {
    for rest in 0u16..2048 {
        let scale = Scale::from_bits((rest << 1) | 1).unwrap();
        for quality in scale.chords() {
            let p = pair(scale, quality);
            let mut counts = [0usize; 5];
            for pitch in Pitch::ALL {
                counts[p.role(pitch) as usize] += 1;
            }
            assert_eq!(counts[0] + counts[1], quality.size(), "{scale} {quality:?}");
            assert_eq!(counts[2] + counts[3], p.outside().len() as usize);
            assert_eq!(counts[4] as u32, 12 - scale.len());
            let clash = p.clash();
            assert!((0.0..=1.0).contains(&clash));
        }
    }
}

#[test]
fn a_chord_on_a_degree_is_a_chord_on_a_modes_root() {
    // D minor 7 in C major: transpose the chord down to C and it fits the
    // mode on the second degree, D Dorian, exactly when it fits C major on D.
    for rest in 0u16..2048 {
        let scale = Scale::from_bits((rest << 1) | 1).unwrap();
        for degree in 0..scale.len() {
            let mode = scale.mode(degree);
            let offset = scale.semitones_of(degree);
            for quality in ChordQuality::ALL {
                let on_degree = quality
                    .interval_set()
                    .transpose(music_core::Interval::new(i16::from(offset)))
                    .is_subset(scale.pitch_set());
                assert_eq!(on_degree, ChordScale::from_parts(mode, quality).is_some());
            }
        }
    }
}

#[test]
fn role_codes_are_distinct() {
    let codes = [
        ToneRole::Guide.code(),
        ToneRole::Colour.code(),
        ToneRole::Tension.code(),
        ToneRole::Avoid.code(),
        ToneRole::Absent.code(),
    ];
    for (i, a) in codes.iter().enumerate() {
        for b in &codes[i + 1..] {
            assert_ne!(a, b);
        }
    }
}
