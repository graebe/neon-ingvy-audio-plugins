//! Chords with arbitrary pitch content, and naming them.
//!
//! A chord here is a root plus any set of pitches, so most of these tests are
//! about the relationship between that set and the vocabulary of names.

use music_core::{Chord, ChordQuality, Interval, Pitch, PitchSet, Triad};

// --- the vocabulary -------------------------------------------------------

#[test]
fn no_two_qualities_share_an_interval_set() {
    // If two qualities had the same shape, `quality()` would pick between them
    // arbitrarily and the choice would depend on declaration order. With a
    // table this size that is easy to introduce by hand, so check it.
    for (index, first) in ChordQuality::ALL.iter().enumerate() {
        for second in ChordQuality::ALL.iter().skip(index + 1) {
            assert_ne!(
                first.interval_set(),
                second.interval_set(),
                "{first:?} and {second:?} have the same shape"
            );
        }
    }
}

#[test]
fn every_quality_starts_at_the_root() {
    for quality in ChordQuality::ALL {
        assert_eq!(
            quality.intervals()[0],
            0,
            "{quality:?} does not start at its root"
        );
        assert!(
            quality.interval_set().contains(Pitch::C),
            "{quality:?} lost its root"
        );
    }
}

#[test]
fn every_quality_is_named_back() {
    // Build a chord from each quality, then ask what it is. The answer must be
    // the quality we started from, on every root.
    for root in Pitch::ALL {
        for quality in ChordQuality::ALL {
            let chord = Chord::from_quality(root, quality);
            assert_eq!(chord.quality(), Some(quality), "{quality:?} on {root}");
            assert_eq!(chord.root(), root);
            assert_eq!(chord.size(), quality.size(), "{quality:?} on {root}");
        }
    }
}

#[test]
fn quality_sizes_run_from_three_to_six() {
    for quality in ChordQuality::ALL {
        let size = quality.size();
        assert!((3..=6).contains(&size), "{quality:?} has {size} notes");
    }
}

// --- the chord type -------------------------------------------------------

#[test]
fn a_chord_always_contains_its_root() {
    // `new` inserts the root rather than refusing, because a chord contains its
    // root by definition here.
    let without_root = PitchSet::from_pitches(&[Pitch::E, Pitch::G]);
    let chord = Chord::new(Pitch::C, without_root);

    assert!(chord.contains(Pitch::C));
    assert_eq!(chord.quality(), Some(ChordQuality::Major));
}

#[test]
fn removing_the_root_is_refused() {
    let c = Chord::major(Pitch::C);
    assert_eq!(c.without(Pitch::C), c, "the root must survive");
    assert!(!c.without(Pitch::E).contains(Pitch::E));
}

#[test]
fn chords_hold_anything_from_one_note_to_twelve() {
    let single = Chord::new(Pitch::C, PitchSet::EMPTY);
    assert_eq!(single.size(), 1);
    assert_eq!(single.quality(), None);

    let everything = Chord::new(Pitch::C, PitchSet::CHROMATIC);
    assert_eq!(everything.size(), 12);
    assert_eq!(everything.quality(), None);
}

#[test]
fn adding_a_note_can_change_the_name() {
    let c = Chord::major(Pitch::C);
    assert_eq!(c.quality(), Some(ChordQuality::Major));

    let with_seventh = c.with(Pitch::B);
    assert_eq!(with_seventh.quality(), Some(ChordQuality::Major7));

    let with_ninth = with_seventh.with(Pitch::D);
    assert_eq!(with_ninth.quality(), Some(ChordQuality::Major9));
}

#[test]
fn transposing_keeps_the_shape() {
    for root in Pitch::ALL {
        for quality in ChordQuality::ALL {
            let chord = Chord::from_quality(root, quality);
            for semitones in [1i16, 5, 7, -3, 12] {
                let moved = chord.transpose(Interval::new(semitones));
                assert_eq!(moved.quality(), Some(quality), "{quality:?} on {root}");
                assert_eq!(moved.intervals(), chord.intervals());
            }
        }
    }
}

#[test]
fn intervals_put_the_root_at_zero() {
    for root in Pitch::ALL {
        for quality in ChordQuality::ALL {
            let chord = Chord::from_quality(root, quality);
            assert!(
                chord.intervals().contains(Pitch::C),
                "{quality:?} on {root}"
            );
            assert_eq!(chord.intervals(), quality.interval_set());
        }
    }
}

#[test]
fn voicing_a_chord_gives_one_note_per_pitch() {
    for quality in ChordQuality::ALL {
        let voiced = Chord::from_quality(Pitch::C, quality).voice(4);
        assert_eq!(voiced.len(), quality.size(), "{quality:?}");
        assert_eq!(voiced.pitch_set(), quality.interval_set(), "{quality:?}");
    }
}

// --- naming and parsing ---------------------------------------------------

#[test]
fn named_chords_round_trip_through_text() {
    for root in Pitch::ALL {
        for quality in ChordQuality::ALL {
            let chord = Chord::from_quality(root, quality);
            let text = chord.to_string();
            assert_eq!(text.parse::<Chord>().unwrap(), chord, "{text}");
        }
    }
}

#[test]
fn unnamed_chords_round_trip_too() {
    // The bracket form exists so that a chord with no name is still printable
    // and still parses back. Walk a wide sample of shapes, not just a few.
    let mut checked = 0;
    for bits in 0u16..4096 {
        let set = PitchSet::from_bits(bits).unwrap();
        let chord = Chord::new(Pitch::C, set);
        if chord.quality().is_some() {
            continue;
        }
        let text = chord.to_string();
        assert_eq!(text.parse::<Chord>().unwrap(), chord, "{text}");
        checked += 1;
    }
    assert!(checked > 4000, "only {checked} unnamed shapes were checked");
}

#[test]
fn the_bracket_form_lists_offsets_from_the_root() {
    let odd = Chord::new(
        Pitch::C,
        PitchSet::from_pitches(&[Pitch::C, Pitch::C_SHARP, Pitch::E, Pitch::F_SHARP]),
    );
    assert_eq!(odd.quality(), None);
    assert_eq!(odd.to_string(), "C[0,1,4,6]");
}

#[test]
fn constructors_agree_with_the_parser() {
    assert_eq!(Chord::maj7(Pitch::C), "Cmaj7".parse().unwrap());
    assert_eq!(Chord::dom9(Pitch::G), "G9".parse().unwrap());
    assert_eq!(Chord::min11(Pitch::D), "Dm11".parse().unwrap());
    assert_eq!(Chord::six_nine(Pitch::F), "F6/9".parse().unwrap());
    assert_eq!(Chord::dom7_sharp11(Pitch::B), "B7#11".parse().unwrap());
    assert_eq!(Chord::maj13(Pitch::E_FLAT), "Ebmaj13".parse().unwrap());
}

// --- identification -------------------------------------------------------

#[test]
fn a_set_of_pitches_can_be_read_several_ways() {
    // C, E, G, A is a C6 and an A minor 7, and both readings are honest.
    let pitches = Chord::sixth(Pitch::C).pitches();

    let roots: Vec<Pitch> = pitches.interpretations().map(|c| c.root()).collect();
    assert!(roots.contains(&Pitch::C), "C6 reading missing");
    assert!(roots.contains(&Pitch::A), "Am7 reading missing");
    assert_eq!(roots.len(), 2);
}

#[test]
fn identify_prefers_the_lower_ranked_quality() {
    // The documented heuristic: sevenths outrank sixths, so Am7 wins over C6.
    // If this changes, it should change here first, deliberately.
    let pitches = Chord::sixth(Pitch::C).pitches();
    let best = pitches.identify().unwrap();

    assert_eq!(best.root(), Pitch::A);
    assert_eq!(best.quality(), Some(ChordQuality::Minor7));
}

#[test]
fn symmetric_chords_have_a_reading_for_every_root() {
    // A diminished seventh divides the octave evenly, so all four of its notes
    // work equally well as the root. The augmented triad does the same in three.
    let dim = Chord::dim7(Pitch::C).pitches();
    assert_eq!(dim.interpretations().count(), 4);

    let aug = Chord::aug(Pitch::C).pitches();
    assert_eq!(aug.interpretations().count(), 3);

    // Every reading really is that quality.
    for reading in dim.interpretations() {
        assert_eq!(reading.quality(), Some(ChordQuality::Diminished7));
    }
}

#[test]
fn every_named_chord_identifies_as_something() {
    for root in Pitch::ALL {
        for quality in ChordQuality::ALL {
            let pitches = Chord::from_quality(root, quality).pitches();
            assert!(
                pitches.identify().is_some(),
                "{quality:?} on {root} identified as nothing"
            );
        }
    }
}

#[test]
fn a_shape_with_no_name_identifies_as_nothing() {
    let cluster = PitchSet::from_pitches(&[Pitch::C, Pitch::C_SHARP, Pitch::D]);
    assert_eq!(cluster.identify(), None);
    assert_eq!(cluster.interpretations().count(), 0);
}

#[test]
fn interpretations_only_yield_roots_from_the_set() {
    for bits in 0u16..4096 {
        let set = PitchSet::from_bits(bits).unwrap();
        for reading in set.interpretations() {
            assert!(set.contains(reading.root()), "root outside the set");
            assert_eq!(reading.pitches(), set, "reading changed the pitches");
        }
    }
}

// --- triads ---------------------------------------------------------------

#[test]
fn triads_convert_to_chords_and_back() {
    for triad in Triad::ALL {
        let chord = Chord::from(triad);
        assert_eq!(chord.pitches(), triad.pitch_set());
        assert_eq!(chord.root(), triad.root);
        assert_eq!(Triad::try_from(chord), Ok(triad));
    }
}

#[test]
fn only_consonant_chords_become_triads() {
    for root in Pitch::ALL {
        for quality in ChordQuality::ALL {
            let chord = Chord::from_quality(root, quality);
            match quality {
                ChordQuality::Major => assert_eq!(Triad::try_from(chord), Ok(Triad::major(root))),
                ChordQuality::Minor => assert_eq!(Triad::try_from(chord), Ok(Triad::minor(root))),
                _ => assert_eq!(
                    Triad::try_from(chord),
                    Err(chord),
                    "{quality:?} is not a consonant triad"
                ),
            }
        }
    }
}

#[test]
fn identifying_a_triads_pitches_finds_the_triad() {
    for triad in Triad::ALL {
        let best = triad.pitch_set().identify().unwrap();
        assert_eq!(Triad::try_from(best), Ok(triad), "{triad}");
    }
}
