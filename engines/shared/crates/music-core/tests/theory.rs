// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! The behaviour of the core types: wrapping, folding, register, arithmetic
//! and frequency.
//!
//! These are exhaustive where the domain is small enough to walk, which for a
//! twelve-tone system is most of the time.

use music_core::{Chord, Interval, Note, Pitch, PitchSet};

#[test]
fn pitch_wrapping_is_total_over_every_interval() {
    // The README claims a pitch has no boundary to overflow. Check it against
    // all 65,536 interval values rather than a sample.
    for semitones in i16::MIN..=i16::MAX {
        let moved = Pitch::C.transpose(Interval::new(semitones));
        assert!(moved.value() < 12, "{semitones} escaped the octave");
        assert_eq!(
            moved,
            Pitch::new(i32::from(semitones)),
            "{semitones} disagreed with from_wrapping"
        );
    }
}

#[test]
fn interval_class_folds_every_input_into_range() {
    for semitones in i16::MIN..=i16::MAX {
        let folded = Interval::new(semitones).class();
        assert!(folded.value() <= 6, "{semitones} folded to {folded}");
        // Folding is symmetric: an interval and its negation agree.
        if semitones != i16::MIN {
            assert_eq!(folded, Interval::new(-semitones).class(), "{semitones}");
        }
    }
}

#[test]
fn every_midi_value_round_trips() {
    // The octave is a signed 16-bit value, so this holds for every i16 input
    // with no boundary to document and nothing to truncate. Before the octave
    // was widened this broke silently outside -1524 to 1547.
    for midi in i16::MIN..=i16::MAX {
        let note = Note::from_midi(midi);
        assert_eq!(note.midi(), midi, "{midi} did not round-trip");
        assert_eq!(
            i32::from(note.octave()),
            (i32::from(midi) - i32::from(note.pitch().value())) / 12 - 1,
            "{midi} landed in the wrong octave"
        );
    }
}

#[test]
fn subtraction_is_the_signed_form_of_distance() {
    // `a - b` is the shortest signed path, so its magnitude must agree with
    // `distance_to` for every one of the 144 ordered pairs.
    for a in Pitch::ALL {
        for b in Pitch::ALL {
            let signed = a - b;
            assert_eq!(
                signed.abs() as u8,
                b.distance_to(a).value(),
                "{a} - {b} = {signed}"
            );
            assert!(
                signed.semitones() >= -5 && signed.semitones() <= 6,
                "{a} - {b}"
            );
        }
    }
}

#[test]
fn subtraction_is_antisymmetric_except_at_the_tritone() {
    let mut tritones = 0;
    for a in Pitch::ALL {
        for b in Pitch::ALL {
            if a.distance_to(b).value() == 6 {
                // Both directions are equally short, so the tie resolves up.
                assert_eq!((a - b).semitones(), 6, "{a} - {b}");
                assert_eq!((b - a).semitones(), 6, "{b} - {a}");
                tritones += 1;
            } else {
                assert_eq!(a - b, -(b - a), "{a} - {b} broke the negation law");
            }
        }
    }
    // Six unordered tritone pairs, so twelve ordered ones.
    assert_eq!(tritones, 12);
}

#[test]
fn subtraction_is_not_interval_to() {
    // The two answer different questions and must not be confused.
    assert_eq!(Pitch::G.interval_to(Pitch::C).semitones(), 5); // ascending
    assert_eq!((Pitch::C - Pitch::G).semitones(), 5); // shortest signed
    assert_eq!(Pitch::C.interval_to(Pitch::G).semitones(), 7); // ascending
    assert_eq!((Pitch::G - Pitch::C).semitones(), -5); // shortest signed
}

#[cfg(feature = "std")]
#[test]
fn frequencies_follow_equal_temperament() {
    let close = |a: f64, b: f64| (a - b).abs() < 1e-9;

    assert!(close(Pitch::A.at(4).frequency_hz(), 440.0), "concert A");
    assert!(close(Pitch::A.at(5).frequency_hz(), 880.0), "an octave up");
    assert!(
        close(Pitch::A.at(3).frequency_hz(), 220.0),
        "an octave down"
    );

    // Middle C, to four decimal places from the standard tables.
    assert!((Pitch::C.at(4).frequency_hz() - 261.6256).abs() < 1e-4);

    // Every octave doubles, across the whole MIDI range.
    for midi in 0..116i16 {
        let low = Note::from_midi(midi).frequency_hz();
        let high = Note::from_midi(midi + 12).frequency_hz();
        assert!((high - low * 2.0).abs() < 1e-6, "midi {midi}");
    }
}

#[cfg(feature = "std")]
#[test]
fn frequencies_honour_a_tuning_reference() {
    let close = |a: f64, b: f64| (a - b).abs() < 1e-9;

    assert!(close(Pitch::A.at(4).frequency_hz_at(432.0), 432.0));
    assert!(close(Pitch::A.at(5).frequency_hz_at(432.0), 864.0));
    assert!(close(Pitch::A.at(4).frequency_hz_at(415.0), 415.0));

    // The default is the 440 reference.
    assert!(close(
        Pitch::C.at(4).frequency_hz(),
        Pitch::C.at(4).frequency_hz_at(440.0)
    ));
}

#[test]
fn middle_c_is_sixty() {
    assert_eq!(Pitch::C.at(4).midi(), 60);
    assert_eq!(Pitch::E.at(4).midi(), 64);
    assert_eq!(Pitch::G.at(4).midi(), 67);
    assert_eq!(Pitch::A.at(4).midi(), 69);
}

#[test]
fn both_consonant_triads_share_an_interval_vector() {
    // The classic fingerprint of a chord's sound, independent of its root. A
    // major and a minor triad are indistinguishable by it, which is exactly
    // why the neo-Riemannian transformations can move between them so
    // smoothly.
    let major = Chord::major(Pitch::C).pitches().interval_vector();
    let minor = Chord::minor(Pitch::A).pitches().interval_vector();
    assert_eq!(major, [0, 0, 1, 1, 1, 0]);
    assert_eq!(major, minor);
}
#[test]
fn the_core_types_stay_small() {
    use std::mem::size_of;

    // These sizes are the point of the design. If one grows, something has
    // stopped being a plain byte-sized value and the docs are now lying.
    assert_eq!(size_of::<Pitch>(), 1);
    assert_eq!(size_of::<Interval>(), 2);
    assert_eq!(size_of::<PitchSet>(), 2);
    // A chord is a root plus a twelve-bit set, so it holds any number of
    // notes for one byte more than the fixed-quality version cost.
    assert_eq!(size_of::<Chord>(), 4);

    // A note carries a 16-bit octave, which is what buys the total MIDI round
    // trip. Notes are never on a hot path here.
    assert_eq!(size_of::<Note>(), 4);
    assert_eq!(size_of::<music_core::Notes>(), 66);

    // A quality and an arrangement are each one plain byte, and the iterator
    // that scans every chord for a fragment is a set plus two counters.
    assert_eq!(size_of::<music_core::ChordQuality>(), 1);
    assert_eq!(size_of::<music_core::Voicing>(), 1);
    assert_eq!(size_of::<music_core::Completions>(), 4);
}
