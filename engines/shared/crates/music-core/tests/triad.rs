// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! Consonant triads and voicings, as music-core alone holds them.
//!
//! neo-riemann builds its transformation group on these types and tests them
//! through it; since music-core moved to its own repository its own suite has
//! to hold them too, without the group: construction, the 24 indices, sets
//! and parsing, the smoothest voice map, and registers.

use music_core::{
    Chord, ChordQuality, Harmony, Interval, Note, Pitch, PitchSet, Triad, TriadQuality, Voiced,
};
use proptest::prelude::*;
use rstest::rstest;

// --- triads -------------------------------------------------------------

#[test]
fn a_triad_is_a_root_a_third_and_a_fifth() {
    let c = Triad::major(Pitch::C);
    assert!(c.is_major() && !c.is_minor());
    assert_eq!(c.pitches(), [Pitch::C, Pitch::E, Pitch::G]);
    assert_eq!(c.third(), Pitch::E);
    assert_eq!(c.fifth(), Pitch::G);

    let a = Triad::minor(Pitch::A);
    assert!(a.is_minor());
    assert_eq!(a.pitches(), [Pitch::A, Pitch::C, Pitch::E]);
    assert_eq!(Triad::new(Pitch::A, TriadQuality::Minor), a);
}

#[test]
fn the_qualities_mirror_each_other() {
    assert_eq!(TriadQuality::Major.flip(), TriadQuality::Minor);
    assert_eq!(TriadQuality::Minor.flip(), TriadQuality::Major);
    assert_eq!(TriadQuality::Major.third_interval(), Interval::MAJOR_THIRD);
    assert_eq!(TriadQuality::Minor.third_interval(), Interval::MINOR_THIRD);
}

#[test]
fn the_twenty_four_round_trip_through_their_index() {
    for i in 0..24u8 {
        let t = Triad::from_index(i).unwrap();
        assert_eq!(t.index(), i);
    }
    assert_eq!(Triad::from_index(24), None);
    assert_eq!(Triad::major(Pitch::C).index(), 0);
    assert_eq!(Triad::minor(Pitch::C).index(), 1);
}

#[test]
fn a_set_is_a_triad_only_when_it_is_one() {
    let c = Triad::major(Pitch::C);
    assert_eq!(
        c.pitch_set(),
        PitchSet::from_pitches(&[Pitch::C, Pitch::E, Pitch::G])
    );
    assert_eq!(Triad::from_pitch_set(c.pitch_set()), Some(c));
    assert_eq!(
        Triad::from_pitch_set(Triad::minor(Pitch::A).pitch_set()),
        Some(Triad::minor(Pitch::A))
    );
    assert_eq!(Triad::from_pitch_set(Chord::dim(Pitch::B).pitches()), None);
    assert_eq!(Triad::from_pitch_set(PitchSet::EMPTY), None);
    assert!(c.contains(Pitch::E));
    assert!(!c.contains(Pitch::F));
    assert_eq!(Harmony::pitch_set(&c), c.pitch_set());
}

#[test]
fn transposing_moves_the_root_and_inverting_flips_the_quality() {
    let c = Triad::major(Pitch::C);
    assert_eq!(c.transpose(Interval::PERFECT_FIFTH), Triad::major(Pitch::G));
    assert_eq!(c.transpose(Interval::new(-12)), c);
    let inverted = c.invert(0);
    assert!(inverted.is_minor());
    assert_eq!(
        inverted.pitch_set(),
        PitchSet::from_pitches(&[Pitch::C, Pitch::A_FLAT, Pitch::F])
    );
    assert_eq!(inverted.invert(0), c);
}

#[rstest]
#[case(Triad::minor(Pitch::C), 2, 1)] // P
#[case(Triad::minor(Pitch::E), 2, 1)] // L
#[case(Triad::minor(Pitch::A), 2, 2)] // R
#[case(Triad::major(Pitch::G), 1, 3)]
#[case(Triad::major(Pitch::C_SHARP), 0, 3)]
fn voice_leading_from_c_major(#[case] to: Triad, #[case] common: u32, #[case] distance: u32) {
    let c = Triad::major(Pitch::C);
    assert_eq!(c.common_tones(to), common);
    assert_eq!(c.voice_leading_distance(to), distance);
    assert_eq!(c.is_parsimonious_to(to), common == 2 && distance <= 2);
}

#[test]
fn a_triad_is_not_its_own_neighbour() {
    let c = Triad::major(Pitch::C);
    assert!(!c.is_parsimonious_to(c));
    assert_eq!(c.voice_leading_distance(c), 0);
}

#[rstest]
#[case("C", Triad::major(Pitch::C))]
#[case("Am", Triad::minor(Pitch::A))]
#[case("F#m", Triad::minor(Pitch::F_SHARP))]
#[case("Eb", Triad::major(Pitch::E_FLAT))]
fn triads_print_and_parse(#[case] text: &str, #[case] triad: Triad) {
    assert_eq!(text.parse::<Triad>().unwrap(), triad);
    assert_eq!(triad.to_string().parse::<Triad>().unwrap(), triad);
    assert_eq!(format!("{triad:?}"), triad.to_string());
}

#[rstest]
#[case("")]
#[case("H")]
#[case("Cmaj7")]
#[case("Cdim")]
fn what_is_not_a_triad_does_not_parse(#[case] text: &str) {
    assert!(text.parse::<Triad>().is_err());
}

#[test]
fn a_triad_and_a_chord_convert_both_ways() {
    let c = Triad::major(Pitch::C);
    let chord = Chord::from(c);
    assert_eq!(chord, Chord::major(Pitch::C));
    assert_eq!(Triad::try_from(chord), Ok(c));
    assert_eq!(Triad::try_from(Chord::min7(Pitch::A)).is_err(), true);
    assert_eq!(
        Chord::from(Triad::minor(Pitch::D)).quality(),
        Some(ChordQuality::Minor)
    );
}

// --- voicings ---------------------------------------------------------------

#[test]
fn a_close_voicing_stacks_upward_from_its_octave() {
    let v = Triad::major(Pitch::C).voice(4);
    assert_eq!(v.midi(), [60, 64, 67]);
    assert_eq!(v.triad(), Triad::major(Pitch::C));
    assert_eq!(v.notes(), [Pitch::C.at(4), Pitch::E.at(4), Pitch::G.at(4)]);
    assert_eq!(v.voices(), 3);
    assert_eq!(v.bass(), Pitch::C.at(4));
    assert_eq!(v.to_string(), "C [C4 E4 G4]");

    let a = Triad::minor(Pitch::A).voice(3);
    assert_eq!(
        a.midi(),
        [57, 60, 64],
        "the third climbs into the next octave"
    );
    assert_eq!(a.midi_at(1), 60);
    assert_eq!(a.note_at(2), Pitch::E.at(4));
}

#[test]
fn a_voicing_with_its_own_octaves_finds_its_lowest_note() {
    let spread = Voiced::new(Triad::major(Pitch::C), [5, 4, 3]);
    assert_eq!(spread.octaves(), [5, 4, 3]);
    assert_eq!(spread.harmony(), Triad::major(Pitch::C));
    assert_eq!(spread.bass(), Pitch::G.at(3));
}

#[test]
fn displacement_pairs_voices_by_the_cheapest_assignment() {
    let c = Triad::major(Pitch::C).voice(4);
    let e_minor_low = Voiced::new(Triad::minor(Pitch::E), [4, 4, 3]); // E4 G4 B3
    assert_eq!(
        c.displacement(e_minor_low),
        1,
        "C4 down to B3, E and G stay"
    );
    let e_minor_high = Voiced::new(Triad::minor(Pitch::E), [4, 4, 4]); // E4 G4 B4
    assert_eq!(
        c.displacement(e_minor_high),
        11,
        "the B above leaves C4 eleven away"
    );
    assert_eq!(c.displacement(c), 0);
    assert_eq!(Pitch::C.at(4).displacement(Pitch::C.at(5)), 12);
}

#[test]
fn transposing_a_voicing_moves_every_voice() {
    let c = Triad::major(Pitch::C).voice(4);
    let d = c.transpose(Interval::MAJOR_SECOND);
    assert_eq!(d.midi(), [62, 66, 69]);
    assert_eq!(c.transpose(Interval::OCTAVE).midi(), [72, 76, 79]);
    assert_eq!(c.transpose(Interval::new(-1)).midi(), [59, 63, 66]);
}

// --- notes ----------------------------------------------------------------

#[test]
fn notes_order_add_and_subtract_by_pitch_height() {
    let c4 = Pitch::C.at(4);
    let e4 = Pitch::E.at(4);
    assert!(c4 < e4);
    assert_eq!(c4 + Interval::MAJOR_THIRD, e4);
    assert_eq!(e4 - Interval::MAJOR_THIRD, c4);
    assert_eq!(e4 - c4, Interval::MAJOR_THIRD);
    assert_eq!(Note::from_midi(60), c4);
    assert_eq!(c4.octave(), 4);
    assert_eq!(c4.pitch(), Pitch::C);
    assert_eq!(Note::from_parts(Pitch::B, 3).midi(), 59);
}

#[rstest]
#[case("C4", 60)]
#[case("A#3", 58)]
#[case("Bb3", 58)]
#[case("C-1", 0)]
fn notes_parse_with_their_octave(#[case] text: &str, #[case] midi: i16) {
    assert_eq!(text.parse::<Note>().unwrap().midi(), midi);
}

#[rstest]
#[case("C")]
#[case("C#")]
#[case("Cx")]
#[case("H4")]
fn a_note_without_a_whole_octave_does_not_parse(#[case] text: &str) {
    assert!(text.parse::<Note>().is_err());
}

#[cfg(feature = "std")]
#[test]
fn a_note_has_a_frequency() {
    assert!((Pitch::A.at(4).frequency_hz() - 440.0).abs() < 1e-9);
    assert!((Pitch::A.at(5).frequency_hz() - 880.0).abs() < 1e-9);
    assert!((Pitch::A.at(4).frequency_hz_at(432.0) - 432.0).abs() < 1e-9);
}

proptest! {
    #[test]
    fn transposing_a_voicing_there_and_back_is_identity(index in 0u8..24, octave in 1i16..7, by in -24i16..24) {
        let v = Triad::from_index(index).unwrap().voice(octave);
        prop_assert_eq!(v.transpose(Interval::new(by)).transpose(Interval::new(-by)), v);
    }

    #[test]
    fn voice_leading_is_symmetric(a in 0u8..24, b in 0u8..24) {
        let (x, y) = (Triad::from_index(a).unwrap(), Triad::from_index(b).unwrap());
        prop_assert_eq!(x.voice_leading_distance(y), y.voice_leading_distance(x));
        prop_assert_eq!(x.common_tones(y), y.common_tones(x));
    }
}
