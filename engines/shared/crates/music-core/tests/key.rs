// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! Keys and modes: their notes, their signatures, how they spell, and what a
//! chord is called in one.

use music_core::{Chord, ChordQuality, Interval, Key, Mode, Notes, Pitch, PitchSet, Spelling};
use proptest::prelude::*;
use rstest::rstest;

// --- modes ------------------------------------------------------------------

#[rstest]
#[case(Mode::Ionian, [0, 2, 4, 5, 7, 9, 11])]
#[case(Mode::Dorian, [0, 2, 3, 5, 7, 9, 10])]
#[case(Mode::Phrygian, [0, 1, 3, 5, 7, 8, 10])]
#[case(Mode::Lydian, [0, 2, 4, 6, 7, 9, 11])]
#[case(Mode::Mixolydian, [0, 2, 4, 5, 7, 9, 10])]
#[case(Mode::Aeolian, [0, 2, 3, 5, 7, 8, 10])]
#[case(Mode::Locrian, [0, 1, 3, 5, 6, 8, 10])]
fn every_mode_has_its_textbook_steps(#[case] mode: Mode, #[case] steps: [u8; 7]) {
    assert_eq!(mode.steps(), steps);
}

#[test]
fn modes_round_trip_through_their_index() {
    for mode in Mode::ALL {
        assert_eq!(Mode::from_index(mode.index()), Some(mode));
    }
    assert_eq!(Mode::from_index(7), None);
}

#[test]
fn every_mode_is_the_major_scale_from_another_note() {
    for mode in Mode::ALL {
        let key = Key::new(Pitch::C, mode);
        let parent = Key::new(key.parent(), Mode::Ionian);
        assert_eq!(key.pitch_set(), parent.pitch_set(), "{mode}");
        assert_eq!(key.pitch_set().len(), 7);
    }
}

// --- signatures and spelling ------------------------------------------------

#[rstest]
#[case(Key::new(Pitch::C, Mode::Ionian), 0, Spelling::Sharps)]
#[case(Key::new(Pitch::G, Mode::Ionian), 1, Spelling::Sharps)]
#[case(Key::new(Pitch::F_SHARP, Mode::Ionian), 6, Spelling::Sharps)]
#[case(Key::new(Pitch::F, Mode::Ionian), -1, Spelling::Flats)]
#[case(Key::new(Pitch::D_FLAT, Mode::Ionian), -5, Spelling::Flats)]
#[case(Key::new(Pitch::A, Mode::Aeolian), 0, Spelling::Sharps)]
#[case(Key::new(Pitch::C, Mode::Aeolian), -3, Spelling::Flats)]
#[case(Key::new(Pitch::D, Mode::Dorian), 0, Spelling::Sharps)]
#[case(Key::new(Pitch::E, Mode::Phrygian), 0, Spelling::Sharps)]
#[case(Key::new(Pitch::F, Mode::Lydian), 0, Spelling::Sharps)]
#[case(Key::new(Pitch::C, Mode::Mixolydian), -1, Spelling::Flats)]
#[case(Key::new(Pitch::B, Mode::Locrian), 0, Spelling::Sharps)]
#[case(Key::new(Pitch::C, Mode::Locrian), -5, Spelling::Flats)]
fn the_signature_is_the_parent_majors(
    #[case] key: Key,
    #[case] signature: i8,
    #[case] spelling: Spelling,
) {
    assert_eq!(key.signature(), signature, "{key}");
    assert_eq!(key.spelling(), spelling, "{key}");
}

#[test]
fn signatures_cover_the_circle_once_per_mode() {
    for mode in Mode::ALL {
        let mut seen = [false; 12];
        for tonic in Pitch::ALL {
            let signature = Key::new(tonic, mode).signature();
            assert!((-5..=6).contains(&signature));
            seen[(signature + 5) as usize] = true;
        }
        assert!(seen.iter().all(|s| *s), "{mode}");
    }
}

#[test]
fn keys_print_their_tonic_in_their_own_spelling() {
    assert_eq!(
        Key::new(Pitch::E_FLAT, Mode::Dorian).to_string(),
        "Eb Dorian"
    );
    assert_eq!(
        Key::new(Pitch::F_SHARP, Mode::Aeolian).to_string(),
        "F# Aeolian"
    );
    assert_eq!(format!("{:?}", Key::default()), "C Ionian");
}

// --- the circle of fifths ---------------------------------------------------

#[test]
fn fifths_walk_the_circle() {
    let clockwise = [
        Pitch::C,
        Pitch::G,
        Pitch::D,
        Pitch::A,
        Pitch::E,
        Pitch::B,
        Pitch::F_SHARP,
        Pitch::C_SHARP,
        Pitch::G_SHARP,
        Pitch::D_SHARP,
        Pitch::A_SHARP,
        Pitch::F,
    ];
    for (position, pitch) in clockwise.iter().enumerate() {
        assert_eq!(pitch.fifths() as usize, position, "{pitch}");
        assert_eq!(Pitch::from_fifths(position as i32), *pitch);
    }
    assert_eq!(Pitch::from_fifths(-1), Pitch::F);
}

// --- degrees ----------------------------------------------------------------

#[rstest]
#[case(Key::new(Pitch::C, Mode::Ionian), Chord::major(Pitch::C), "I")]
#[case(Key::new(Pitch::C, Mode::Ionian), Chord::min7(Pitch::D), "ii7")]
#[case(Key::new(Pitch::C, Mode::Ionian), Chord::dom7(Pitch::G), "V7")]
#[case(Key::new(Pitch::C, Mode::Ionian), Chord::maj7(Pitch::F), "IVmaj7")]
#[case(Key::new(Pitch::C, Mode::Ionian), Chord::min7(Pitch::A), "vi7")]
#[case(Key::new(Pitch::C, Mode::Ionian), Chord::dim(Pitch::B), "vii°")]
#[case(
    Key::new(Pitch::C, Mode::Ionian),
    Chord::from_quality(Pitch::B, ChordQuality::HalfDiminished7),
    "viiø7"
)]
#[case(Key::new(Pitch::C, Mode::Ionian), Chord::major(Pitch::B_FLAT), "bVII")]
#[case(Key::new(Pitch::C, Mode::Ionian), Chord::major(Pitch::A_FLAT), "bVI")]
#[case(Key::new(Pitch::C, Mode::Ionian), Chord::major(Pitch::E_FLAT), "bIII")]
#[case(Key::new(Pitch::C, Mode::Ionian), Chord::major(Pitch::D_FLAT), "bII")]
#[case(
    Key::new(Pitch::C, Mode::Ionian),
    Chord::from_quality(Pitch::F_SHARP, ChordQuality::HalfDiminished7),
    "#ivø7"
)]
#[case(Key::new(Pitch::C, Mode::Ionian), Chord::aug(Pitch::E), "III+")]
#[case(Key::new(Pitch::A, Mode::Aeolian), Chord::major(Pitch::C), "III")]
#[case(Key::new(Pitch::A, Mode::Aeolian), Chord::dom7(Pitch::E), "V7")]
#[case(
    Key::new(Pitch::A, Mode::Aeolian),
    Chord::dim7(Pitch::G_SHARP),
    "#vii°7"
)]
#[case(
    Key::new(Pitch::A, Mode::Aeolian),
    Chord::major(Pitch::C_SHARP),
    "#III"
)]
#[case(Key::new(Pitch::D, Mode::Dorian), Chord::major(Pitch::G), "IV")]
#[case(Key::new(Pitch::D, Mode::Dorian), Chord::major(Pitch::B_FLAT), "bVI")]
#[case(Key::new(Pitch::C, Mode::Lydian), Chord::major(Pitch::F), "bIV")]
#[case(Key::new(Pitch::C, Mode::Lydian), Chord::major(Pitch::D), "II")]
#[case(Key::new(Pitch::E, Mode::Phrygian), Chord::major(Pitch::F), "II")]
#[case(Key::new(Pitch::B, Mode::Locrian), Chord::major(Pitch::F), "V")]
#[case(Key::new(Pitch::B, Mode::Locrian), Chord::major(Pitch::F_SHARP), "#V")]
#[case(Key::new(Pitch::C, Mode::Ionian), Chord::sus4(Pitch::G), "Vsus4")]
#[case(Key::new(Pitch::C, Mode::Ionian), Chord::fifth(Pitch::C), "I5")]
fn degrees_read_as_charts_write_them(
    #[case] key: Key,
    #[case] chord: Chord,
    #[case] written: &str,
) {
    assert_eq!(
        key.degree_of(chord).to_string(),
        written,
        "{chord} in {key}"
    );
}

#[test]
fn a_slash_chord_is_the_degree_of_its_root() {
    let c = Key::new(Pitch::C, Mode::Ionian);
    let inverted = Chord::min7(Pitch::A).over(Pitch::C);
    assert_eq!(c.degree_of(inverted).to_string(), "vi7");
    assert_eq!(c.degree_of(inverted).chord(), inverted);
}

#[test]
fn an_unnamed_shape_takes_its_case_from_its_third() {
    let c = Key::new(Pitch::C, Mode::Ionian);
    let minorish = Chord::new(
        Pitch::D,
        PitchSet::from_pitches(&[Pitch::F, Pitch::A_SHARP]),
    );
    let majorish = Chord::new(
        Pitch::D,
        PitchSet::from_pitches(&[Pitch::F_SHARP, Pitch::G_SHARP]),
    );
    assert_eq!(minorish.quality(), None);
    assert_eq!(majorish.quality(), None);
    assert_eq!(c.degree_of(minorish).to_string(), "ii");
    assert_eq!(c.degree_of(majorish).to_string(), "II");
}

#[test]
fn every_root_in_every_key_has_a_degree_altered_at_most_once() {
    for mode in Mode::ALL {
        for tonic in Pitch::ALL {
            let key = Key::new(tonic, mode);
            for root in Pitch::ALL {
                let degree = key.degree_of(Chord::major(root));
                assert!((1..=7).contains(&degree.step()));
                assert!(degree.accidental().abs() <= 1, "{root} in {key}");
                let in_key = key.pitch_set().contains(root);
                assert_eq!(degree.accidental() == 0, in_key, "{root} in {key}");
            }
        }
    }
}

// --- spelling and names -----------------------------------------------------

#[test]
fn chords_and_notes_spell_either_way() {
    let chord = Chord::min7(Pitch::E_FLAT).over(Pitch::G_FLAT);
    assert_eq!(
        chord.spelled(Spelling::Sharps).to_string(),
        chord.to_string()
    );
    assert_eq!(chord.spelled(Spelling::Flats).to_string(), "Ebm7/Gb");
    assert_eq!(format!("{:?}", chord.spelled(Spelling::Flats)), "Ebm7/Gb");
    assert_eq!(chord.spelled(Spelling::Flats).value(), chord);

    let note = Pitch::A_FLAT.at(2);
    assert_eq!(note.spelled(Spelling::Flats).to_string(), "Ab2");
    assert_eq!(format!("{:>5}", note.spelled(Spelling::Sharps)), "  G#2");
}

#[test]
fn unnamed_chords_spell_their_root_and_bass() {
    let shape = Chord::new(Pitch::D_FLAT, PitchSet::from_pitches(&[Pitch::D])).over(Pitch::E_FLAT);
    assert_eq!(shape.spelled(Spelling::Flats).to_string(), "Db[0,1,2]/Eb");
}

#[test]
fn every_quality_has_a_distinct_name() {
    for (i, a) in ChordQuality::ALL.iter().enumerate() {
        assert!(!a.name().is_empty());
        for b in &ChordQuality::ALL[i + 1..] {
            assert_ne!(a.name(), b.name());
        }
    }
}

#[rstest]
#[case(0, "unison", 0)]
#[case(4, "major 3rd", 0)]
#[case(-4, "major 3rd", 0)]
#[case(6, "tritone", 0)]
#[case(12, "octave", 0)]
#[case(13, "minor 2nd", 1)]
#[case(16, "major 3rd", 1)]
#[case(24, "octave", 1)]
#[case(31, "perfect 5th", 2)]
fn intervals_are_named_by_their_simple_form(
    #[case] semitones: i16,
    #[case] name: &str,
    #[case] octaves: u16,
) {
    let interval = Interval::new(semitones);
    assert_eq!(interval.name(), name);
    assert_eq!(interval.compound_octaves(), octaves);
}

// --- properties -------------------------------------------------------------

fn any_pitch() -> impl Strategy<Value = Pitch> {
    (0i32..12).prop_map(Pitch::new)
}

fn any_mode() -> impl Strategy<Value = Mode> {
    (0u8..7).prop_map(|i| Mode::from_index(i).unwrap())
}

fn any_quality() -> impl Strategy<Value = ChordQuality> {
    (0usize..ChordQuality::ALL.len()).prop_map(|i| ChordQuality::ALL[i])
}

proptest! {
    #[test]
    fn a_degree_survives_transposing_key_and_chord_together(
        tonic in any_pitch(), mode in any_mode(), root in any_pitch(),
        quality in any_quality(), by in -24i16..24,
    ) {
        let key = Key::new(tonic, mode);
        let chord = Chord::from_quality(root, quality);
        let moved = Key::new(tonic.transpose(Interval::new(by)), mode);
        let moved_chord = Chord::from_quality(root.transpose(Interval::new(by)), quality);
        prop_assert_eq!(
            key.degree_of(chord).to_string(),
            moved.degree_of(moved_chord).to_string()
        );
    }

    #[test]
    fn a_keys_pitch_set_contains_its_tonic_and_seven_notes(tonic in any_pitch(), mode in any_mode()) {
        let key = Key::new(tonic, mode);
        prop_assert!(key.pitch_set().contains(tonic));
        prop_assert_eq!(key.pitch_set().len(), 7);
    }

    #[test]
    fn naming_a_voicing_is_unchanged_by_spelling_but_its_text(
        midi in proptest::collection::vec(24i16..108, 1..8),
    ) {
        let notes: Vec<_> = midi.iter().map(|m| music_core::Note::from_midi(*m)).collect();
        let notes = Notes::from_slice(&notes).unwrap();
        if let Some(chord) = notes.identify() {
            prop_assert_eq!(chord.spelled(Spelling::Flats).value(), chord);
            prop_assert_eq!(chord.spelled(Spelling::Sharps).to_string(), chord.to_string());
        }
    }
}

#[test]
fn the_new_types_stay_small() {
    use core::mem::size_of;
    assert_eq!(size_of::<Mode>(), 1);
    assert_eq!(size_of::<Key>(), 2);
    assert_eq!(size_of::<music_core::Degree>(), 6);
}

#[test]
fn a_note_name_is_two_bytes() {
    assert_eq!(core::mem::size_of::<music_core::NoteName>(), 2);
}
