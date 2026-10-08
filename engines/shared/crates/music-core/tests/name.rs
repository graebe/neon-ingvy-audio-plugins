// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! Note names: letters, accidentals, and how a key chooses them.

use music_core::{Key, Letter, Mode, NoteName, Pitch, Spelling};
use proptest::prelude::*;
use rstest::rstest;

#[test]
fn the_plain_spellings_name_every_pitch_once_each_way() {
    for pitch in Pitch::ALL {
        for spelling in [Spelling::Sharps, Spelling::Flats] {
            let name = NoteName::of(pitch, spelling);
            assert_eq!(name.pitch(), pitch);
            assert_eq!(
                name.to_string(),
                pitch.name(spelling),
                "the same text as Pitch::name"
            );
            assert!(name.accidental().abs() <= 1);
        }
    }
}

#[rstest]
#[case(Letter::E, 1, "E#", Pitch::F)]
#[case(Letter::C, -1, "Cb", Pitch::B)]
#[case(Letter::F, 2, "F##", Pitch::G)]
#[case(Letter::B, -2, "Bbb", Pitch::A)]
#[case(Letter::G, 0, "G", Pitch::G)]
fn names_print_and_resolve(
    #[case] letter: Letter,
    #[case] accidental: i8,
    #[case] text: &str,
    #[case] pitch: Pitch,
) {
    let name = NoteName::new(letter, accidental);
    assert_eq!(name.to_string(), text);
    assert_eq!(format!("{name:?}"), text);
    assert_eq!(name.pitch(), pitch);
}

#[test]
fn letters_walk_and_wrap() {
    assert_eq!(Letter::B.up(1), Letter::C);
    assert_eq!(Letter::C.up(-1), Letter::B);
    assert_eq!(Letter::D.up(14), Letter::D);
    assert_eq!(Letter::A.natural(), Pitch::A);
    assert_eq!(Letter::F.index(), 3);
    assert_eq!(Letter::F.name(), "F");
}

#[rstest]
#[case(Key::new(Pitch::C, Mode::Ionian), Pitch::B_FLAT, "Bb")]
#[case(Key::new(Pitch::C, Mode::Ionian), Pitch::E_FLAT, "Eb")]
#[case(Key::new(Pitch::C, Mode::Ionian), Pitch::A_FLAT, "Ab")]
#[case(Key::new(Pitch::C, Mode::Ionian), Pitch::D_FLAT, "Db")]
#[case(Key::new(Pitch::C, Mode::Ionian), Pitch::F_SHARP, "F#")]
#[case(Key::new(Pitch::A, Mode::Aeolian), Pitch::G_SHARP, "G#")]
#[case(Key::new(Pitch::A, Mode::Aeolian), Pitch::C_SHARP, "C#")]
#[case(Key::new(Pitch::F_SHARP, Mode::Ionian), Pitch::F, "E#")]
#[case(Key::new(Pitch::F_SHARP, Mode::Ionian), Pitch::G, "G")]
#[case(Key::new(Pitch::D_FLAT, Mode::Ionian), Pitch::G_FLAT, "Gb")]
#[case(Key::new(Pitch::D_FLAT, Mode::Ionian), Pitch::C, "C")]
#[case(Key::new(Pitch::E_FLAT, Mode::Aeolian), Pitch::F, "E#")]
#[case(Key::new(Pitch::D, Mode::Dorian), Pitch::B_FLAT, "Bb")]
#[case(Key::new(Pitch::F, Mode::Lydian), Pitch::B, "B")]
#[case(Key::new(Pitch::F, Mode::Ionian), Pitch::B_FLAT, "Bb")]
fn keys_name_their_notes_as_a_score_would(
    #[case] key: Key,
    #[case] pitch: Pitch,
    #[case] written: &str,
) {
    assert_eq!(key.name_of(pitch).to_string(), written, "{pitch} in {key}");
}

#[test]
fn every_key_spells_its_scale_with_seven_letters_in_order() {
    for mode in Mode::ALL {
        for tonic in Pitch::ALL {
            let key = Key::new(tonic, mode);
            let first = key.name_of(tonic).letter();
            for (i, step) in mode.steps().iter().enumerate() {
                let pitch = tonic.transpose(music_core::Interval::new(*step as i16));
                let name = key.name_of(pitch);
                assert_eq!(name.letter(), first.up(i as i32), "{pitch} in {key}");
            }
        }
    }
}

#[test]
fn octaves_follow_the_letter_across_the_b_c_seam() {
    let c_flat = NoteName::new(Letter::C, -1);
    assert_eq!(c_flat.octave_of(Pitch::B.at(3)), 4);
    let b_sharp = NoteName::new(Letter::B, 1);
    assert_eq!(b_sharp.octave_of(Pitch::C.at(4)), 3);
    let middle_c = NoteName::new(Letter::C, 0);
    assert_eq!(middle_c.octave_of(Pitch::C.at(4)), 4);
    assert_eq!(middle_c.staff_step(Pitch::C.at(4)), 28);
    assert_eq!(NoteName::new(Letter::E, 0).staff_step(Pitch::E.at(4)), 30);
    assert_eq!(
        b_sharp.staff_step(Pitch::C.at(4)),
        27,
        "a line below middle C's"
    );
}

fn any_pitch() -> impl Strategy<Value = Pitch> {
    (0i32..12).prop_map(Pitch::new)
}

proptest! {
    #[test]
    fn a_keys_name_always_names_the_pitch_with_at_most_two_accidentals(
        tonic in any_pitch(), mode in 0u8..7, pitch in any_pitch(),
    ) {
        let key = Key::new(tonic, Mode::from_index(mode).unwrap());
        let name = key.name_of(pitch);
        prop_assert_eq!(name.pitch(), pitch);
        prop_assert!(name.accidental().abs() <= 2);
        if !key.pitch_set().contains(pitch) {
            prop_assert!(name.accidental() != 0 || key.name_of(pitch).letter().natural() == pitch);
        }
    }

    #[test]
    fn the_written_octave_puts_the_note_back_where_it_sounds(
        midi in 0i16..128, letter in 0i32..7, accidental in -2i8..3,
    ) {
        let note = music_core::Note::from_midi(midi);
        let name = NoteName::new(Letter::C.up(letter), accidental);
        if name.pitch() == note.pitch() {
            let octave = name.octave_of(note) as i32;
            let back = (octave + 1) * 12 + name.letter().natural().value() as i32 + accidental as i32;
            prop_assert_eq!(back, midi as i32);
        }
    }
}
