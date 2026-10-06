//! Parsing, printing, and the arithmetic invariants underneath them.
//!
//! Parsing exists for user input. Everything else in this crate should reach
//! for a constructor instead, which is why these are the only tests that lean
//! on `FromStr`.

use music_core::{Chord, ChordQuality, Interval, Note, Pitch, PitchSet, Spelling};
use proptest::prelude::*;
use rstest::rstest;

#[rstest]
#[case("C", 0)]
#[case("C#", 1)]
#[case("Db", 1)]
#[case("D", 2)]
#[case("Eb", 3)]
#[case("E", 4)]
#[case("Fb", 4)]
#[case("F", 5)]
#[case("E#", 5)]
#[case("F#", 6)]
#[case("Gb", 6)]
#[case("G", 7)]
#[case("Ab", 8)]
#[case("A", 9)]
#[case("Bb", 10)]
#[case("B", 11)]
#[case("Cb", 11)]
#[case("B#", 0)]
#[case("C##", 2)]
#[case("Dbb", 0)]
fn pitches_parse(#[case] input: &str, #[case] expected: u8) {
    assert_eq!(input.parse::<Pitch>().unwrap().value(), expected);
}

#[rstest]
#[case("")]
#[case("H")]
#[case("C4")]
#[case("x")]
#[case("#C")]
fn bad_pitches_are_rejected(#[case] input: &str) {
    assert!(input.parse::<Pitch>().is_err(), "{input} should not parse");
}

#[test]
fn both_spellings_name_the_same_value() {
    assert_eq!(Pitch::C_SHARP, Pitch::D_FLAT);
    assert_eq!(Pitch::D_SHARP, Pitch::E_FLAT);
    assert_eq!(Pitch::F_SHARP, Pitch::G_FLAT);
    assert_eq!(Pitch::G_SHARP, Pitch::A_FLAT);
    assert_eq!(Pitch::A_SHARP, Pitch::B_FLAT);

    assert_eq!(Pitch::E_FLAT.name(Spelling::Sharps), "D#");
    assert_eq!(Pitch::E_FLAT.name(Spelling::Flats), "Eb");
}

#[test]
fn every_pitch_name_parses_back() {
    for pitch in Pitch::ALL {
        for spelling in [Spelling::Sharps, Spelling::Flats] {
            let name = pitch.name(spelling);
            assert_eq!(name.parse::<Pitch>().unwrap(), pitch, "{name}");
        }
    }
}

#[rstest]
#[case("C4", 60)]
#[case("C-1", 0)]
#[case("A4", 69)]
#[case("Eb3", 51)]
#[case("D#3", 51)]
#[case("G9", 127)]
fn notes_parse(#[case] input: &str, #[case] expected: i16) {
    assert_eq!(input.parse::<Note>().unwrap().midi(), expected);
}

#[rstest]
#[case("C")]
#[case("C4x")]
#[case("4")]
#[case("")]
fn bad_notes_are_rejected(#[case] input: &str) {
    assert!(input.parse::<Note>().is_err(), "{input} should not parse");
}

#[test]
fn notes_round_trip_across_the_midi_range() {
    for midi in 0..128i16 {
        let note = Note::from_midi(midi);
        assert_eq!(note.midi(), midi);
        assert_eq!(note.to_string().parse::<Note>().unwrap(), note);
    }
}

#[rstest]
#[case("C", ChordQuality::Major)]
#[case("Cm", ChordQuality::Minor)]
#[case("Cdim", ChordQuality::Diminished)]
#[case("Caug", ChordQuality::Augmented)]
#[case("C+", ChordQuality::Augmented)]
#[case("Csus2", ChordQuality::Sus2)]
#[case("Csus4", ChordQuality::Sus4)]
#[case("Cmaj7", ChordQuality::Major7)]
#[case("C7", ChordQuality::Dominant7)]
#[case("Cm7", ChordQuality::Minor7)]
#[case("Cmmaj7", ChordQuality::MinorMajor7)]
#[case("Cm7b5", ChordQuality::HalfDiminished7)]
#[case("Cdim7", ChordQuality::Diminished7)]
#[case("C5", ChordQuality::Fifth)]
#[case("C7sus4", ChordQuality::Dominant7Sus4)]
#[case("C7sus", ChordQuality::Dominant7Sus4)]
fn chords_parse(#[case] input: &str, #[case] quality: ChordQuality) {
    assert_eq!(
        input.parse::<Chord>().unwrap(),
        Chord::from_quality(Pitch::C, quality)
    );
}

#[test]
fn every_chord_round_trips() {
    for root in Pitch::ALL {
        for quality in ChordQuality::ALL {
            let chord = Chord::from_quality(root, quality);
            let text = chord.to_string();
            assert_eq!(text.parse::<Chord>().unwrap(), chord, "{text}");
        }
    }
}

#[test]
fn chord_sizes_and_sets_agree() {
    for root in Pitch::ALL {
        for quality in ChordQuality::ALL {
            let chord = Chord::from_quality(root, quality);
            assert_eq!(chord.pitches().len() as usize, chord.size(), "{chord}");
            assert!(chord.contains(root), "{chord} lost its root");
            assert_eq!(chord.voice(4).len(), chord.size(), "{chord}");
        }
    }
}
fn any_pitch() -> impl Strategy<Value = Pitch> {
    (0i32..12).prop_map(Pitch::new)
}

proptest! {
    #[test]
    fn transposing_there_and_back_is_identity(pitch in any_pitch(), by in -128i16..128) {
        let interval = Interval::new(by);
        prop_assert_eq!(pitch.transpose(interval).transpose(-interval), pitch);
    }

    #[test]
    fn inverting_twice_about_the_same_axis_is_identity(pitch in any_pitch(), axis in -64i32..64) {
        prop_assert_eq!(pitch.invert(axis).invert(axis), pitch);
    }

    #[test]
    fn wrapping_agrees_with_the_raw_value(raw in -10_000i32..10_000) {
        let pitch = Pitch::new(raw);
        prop_assert!(pitch.value() < 12);
        prop_assert_eq!(i32::from(pitch.value()), raw.rem_euclid(12));
    }

    #[test]
    fn distance_is_symmetric_and_bounded(a in any_pitch(), b in any_pitch()) {
        prop_assert_eq!(a.distance_to(b), b.distance_to(a));
        prop_assert!(a.distance_to(b).value() <= 6);
    }

    #[test]
    fn interval_and_distance_agree(a in any_pitch(), b in any_pitch()) {
        let up = a.interval_to(b).semitones();
        prop_assert!((0..12).contains(&up));
        prop_assert_eq!(a.transpose(Interval::new(up)), b);
        prop_assert_eq!(a.distance_to(b).value() as i16, up.min(12 - up) % 12);
    }

    #[test]
    fn set_transposition_matches_pitch_transposition(a in any_pitch(), b in any_pitch(), by in -64i16..64) {
        let interval = Interval::new(by);
        let set = PitchSet::from_pitches(&[a, b]);
        let moved = PitchSet::from_pitches(&[a.transpose(interval), b.transpose(interval)]);
        prop_assert_eq!(set.transpose(interval), moved);
    }

    #[test]
    fn set_operations_obey_de_morgan(x in 0u16..4096, y in 0u16..4096) {
        let a = PitchSet::from_bits(x).unwrap();
        let b = PitchSet::from_bits(y).unwrap();
        prop_assert_eq!(!(a | b), !a & !b);
        prop_assert_eq!(!(a & b), !a | !b);
        prop_assert_eq!(a - b, a & !b);
    }

}

// --- formatting -----------------------------------------------------------

#[test]
fn display_honours_width_and_alignment() {
    // Writing straight to the formatter ignores width, fill and alignment, so
    // every Display in this crate renders into a buffer and calls `pad`.
    // Without that, `{:>4}` on a pitch silently does nothing and any table
    // built from these types comes out ragged.
    assert_eq!(format!("{:>4}", Pitch::C), "   C");
    assert_eq!(format!("{:<4}|", Pitch::C_SHARP), "C#  |");
    assert_eq!(format!("{:>6}", Pitch::C.at(4)), "    C4");
    assert_eq!(format!("{:>6}", Chord::min7(Pitch::D)), "   Dm7");
    assert_eq!(format!("{:>5}", Interval::PERFECT_FIFTH), "   +7");
    assert_eq!(
        format!("{:>6}", music_core::IntervalClass::new(5)),
        "   ic5"
    );
    assert_eq!(
        format!(
            "{:>12}",
            PitchSet::from_pitches(&[Pitch::C, Pitch::E, Pitch::G])
        ),
        "   {C, E, G}"
    );
}

#[test]
fn display_buffers_are_wide_enough() {
    // Each Display renders into a fixed stack buffer before padding. If a
    // buffer is too small the output truncates silently, so check the widest
    // case each type can produce rather than a typical one.
    let chromatic = PitchSet::CHROMATIC.to_string();
    assert!(chromatic.starts_with('{'), "{chromatic}");
    assert!(chromatic.ends_with('}'), "{chromatic}");
    for pitch in Pitch::ALL {
        assert!(
            chromatic.contains(pitch.name(Spelling::Sharps)),
            "{chromatic} lost {pitch}"
        );
    }

    let full = music_core::Notes::from_slice(&[Pitch::C_SHARP.at(-128); 16]).unwrap();
    let rendered = full.to_string();
    assert!(rendered.ends_with(']'), "{rendered}");
    assert_eq!(rendered.matches("C#-128").count(), 16);

    assert_eq!(Chord::min_maj7(Pitch::C_SHARP).to_string(), "C#mmaj7");
    assert_eq!(Chord::dom7_sus4(Pitch::C_SHARP).to_string(), "C#7sus4");
    assert_eq!(Pitch::C.at(-128).to_string(), "C-128");
    assert_eq!(Pitch::B.at(127).to_string(), "B127");
}

#[test]
fn the_constructors_agree_with_the_parser() {
    // Constructors are the path code should take; these must not drift apart.
    assert_eq!(Chord::min7(Pitch::D), "Dm7".parse().unwrap());
    assert_eq!(Chord::maj7(Pitch::C), "Cmaj7".parse().unwrap());
    assert_eq!(Chord::dom7(Pitch::G), "G7".parse().unwrap());
    assert_eq!(Chord::half_dim7(Pitch::B), "Bm7b5".parse().unwrap());
    assert_eq!(Chord::dim7(Pitch::F_SHARP), "F#dim7".parse().unwrap());
    assert_eq!(Chord::sus4(Pitch::A), "Asus4".parse().unwrap());
    assert_eq!(Chord::fifth(Pitch::E), "E5".parse().unwrap());
    assert_eq!(Chord::dom7_sus4(Pitch::G), "G7sus4".parse().unwrap());
    assert_eq!(Chord::dom7_sus4(Pitch::G), "G7sus".parse().unwrap());
}

#[test]
fn a_power_chord_and_a_note_name_read_the_same_text() {
    // "C5" is a chord symbol and a note name at once, and both readings are
    // real. `FromStr` is per type, so the caller has already said which one it
    // wants by the time the text is parsed. Deliberate, not an oversight: the
    // same was already true of "G9" before the power chord arrived.
    let chord: Chord = "C5".parse().unwrap();
    let note: Note = "C5".parse().unwrap();

    assert_eq!(chord, Chord::fifth(Pitch::C));
    assert_eq!(note, Pitch::C.at(5));
    assert_eq!(chord.size(), 2);
}
