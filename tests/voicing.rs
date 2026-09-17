//! Giving a chord a register: canonical stacks, arrangements and inversions.
//!
//! `music-core` keeps harmony as pitch classes, so everything here is about the
//! step where octaves come back: which note sounds where, and what survives
//! that choice.

use music_core::{Chord, ChordQuality, Interval, Notes, Pitch, PitchSet, Voicing};

// --- the canonical stack --------------------------------------------------

#[test]
fn every_stacking_reduces_to_its_interval_set() {
    // The invariant that holds the two offset tables together. `stacking` is a
    // re-registration of `intervals`, so it may move a note up an octave but
    // never add one, drop one, or change which pitch class it is.
    for quality in ChordQuality::ALL {
        let stacking = quality.stacking();

        assert_eq!(
            stacking.len(),
            quality.intervals().len(),
            "{quality:?} changed how many notes it has"
        );

        let mut set = PitchSet::EMPTY;
        for offset in stacking {
            set = set.insert(Pitch::new(i32::from(*offset)));
        }
        assert_eq!(
            set,
            quality.interval_set(),
            "{quality:?} stacks to different pitch classes"
        );
    }
}

#[test]
fn every_stacking_climbs_strictly_from_the_root() {
    for quality in ChordQuality::ALL {
        let stacking = quality.stacking();

        assert_eq!(stacking[0], 0, "{quality:?} does not start at its root");
        for pair in stacking.windows(2) {
            assert!(pair[0] < pair[1], "{quality:?} does not ascend: {pair:?}");
        }
    }
}

#[test]
fn extensions_are_stacked_above_the_octave() {
    // The rows that differ from `intervals`, pinned literally. These are the
    // numbers a chord effect wants, so a typo here is a wrong sound.
    assert_eq!(ChordQuality::Add9.stacking(), &[0, 4, 7, 14]);
    assert_eq!(ChordQuality::MinorAdd9.stacking(), &[0, 3, 7, 14]);
    assert_eq!(ChordQuality::SixNine.stacking(), &[0, 4, 7, 9, 14]);
    assert_eq!(ChordQuality::Dominant9.stacking(), &[0, 4, 7, 10, 14]);
    assert_eq!(ChordQuality::Major9.stacking(), &[0, 4, 7, 11, 14]);
    assert_eq!(ChordQuality::Minor9.stacking(), &[0, 3, 7, 10, 14]);
    assert_eq!(ChordQuality::Dominant11.stacking(), &[0, 4, 7, 10, 14, 17]);
    assert_eq!(ChordQuality::Minor11.stacking(), &[0, 3, 7, 10, 14, 17]);
    assert_eq!(ChordQuality::Dominant13.stacking(), &[0, 4, 7, 10, 14, 21]);
    assert_eq!(ChordQuality::Major13.stacking(), &[0, 4, 7, 11, 14, 21]);
    assert_eq!(ChordQuality::SevenFlatNine.stacking(), &[0, 4, 7, 10, 13]);
    assert_eq!(ChordQuality::SevenSharpNine.stacking(), &[0, 4, 7, 10, 15]);
    assert_eq!(
        ChordQuality::SevenSharpEleven.stacking(),
        &[0, 4, 7, 10, 18]
    );
}

#[test]
fn a_chord_that_fits_in_an_octave_stacks_where_it_already_was() {
    // Eighteen of the thirty-one qualities have nothing above the octave. They
    // share the slice rather than repeating it, so check they really agree.
    assert_eq!(
        ChordQuality::Major.stacking(),
        ChordQuality::Major.intervals()
    );
    assert_eq!(
        ChordQuality::Minor7.stacking(),
        ChordQuality::Minor7.intervals()
    );
    assert_eq!(
        ChordQuality::Sixth.stacking(),
        ChordQuality::Sixth.intervals()
    );
    assert_eq!(ChordQuality::Fifth.stacking(), &[0, 7]);
    assert_eq!(ChordQuality::Dominant7Sus4.stacking(), &[0, 5, 7, 10]);
}

#[test]
fn no_stacking_reaches_beyond_a_thirteenth() {
    // The widest offset in the table is 21, a thirteenth. That it fits in a
    // `u8` is why the offsets are bytes.
    for quality in ChordQuality::ALL {
        for offset in quality.stacking() {
            assert!(*offset <= 21, "{quality:?} reaches {offset} semitones");
        }
    }
}

// --- arrangements ---------------------------------------------------------

#[test]
fn close_position_is_what_voice_already_did() {
    // `voice` grew an argument, but did not change its mind. Every chord it
    // ever produced it still produces.
    for root in Pitch::ALL {
        for quality in ChordQuality::ALL {
            let chord = Chord::from_quality(root, quality);
            assert_eq!(
                chord.voice(4),
                chord.voice_as(4, Voicing::Close),
                "{quality:?} on {root}"
            );
        }
    }
}

#[test]
fn close_is_the_default_arrangement() {
    assert_eq!(Voicing::default(), Voicing::Close);
}

#[test]
fn only_a_rootless_voicing_changes_which_pitches_sound() {
    // Close, stacked and drop 2 move notes between octaves. None of them adds
    // a pitch class or takes one away.
    for quality in ChordQuality::ALL {
        let chord = Chord::from_quality(Pitch::C, quality);

        for voicing in [Voicing::Close, Voicing::Stacked, Voicing::Drop2] {
            let voiced = chord.voice_as(4, voicing);
            assert_eq!(
                voiced.pitch_set(),
                chord.pitches(),
                "{quality:?} as {voicing:?} changed its pitches"
            );
            assert_eq!(voiced.len(), quality.size(), "{quality:?} as {voicing:?}");
        }
    }
}

#[test]
fn a_stacked_voicing_sounds_the_written_heights() {
    // The whole point: a ninth is fourteen semitones up, not two.
    let cmaj9 = Chord::maj9(Pitch::C).voice_as(4, Voicing::Stacked);
    assert_eq!(cmaj9.to_string(), "[C4 E4 G4 B4 D5]");

    let cm11 = Chord::min11(Pitch::C).voice_as(3, Voicing::Stacked);
    assert_eq!(cm11.to_string(), "[C3 D#3 G3 A#3 D4 F4]");

    // Close position packs the same chord into an octave instead.
    assert_eq!(
        Chord::maj9(Pitch::C).voice(4).to_string(),
        "[C4 D4 E4 G4 B4]"
    );
}

#[test]
fn a_stacked_voicing_climbs_without_repeating() {
    for quality in ChordQuality::ALL {
        let voiced = Chord::from_quality(Pitch::C, quality).voice_as(4, Voicing::Stacked);
        for pair in voiced.as_slice().windows(2) {
            assert!(
                pair[0].midi() < pair[1].midi(),
                "{quality:?} does not ascend: {voiced}"
            );
        }
    }
}

#[test]
fn a_rootless_voicing_drops_exactly_the_root() {
    for quality in ChordQuality::ALL {
        let chord = Chord::from_quality(Pitch::C, quality);
        let rootless = chord.voice_as(4, Voicing::Rootless);

        assert_eq!(rootless.len(), quality.size() - 1, "{quality:?}");
        assert!(
            !rootless.pitch_set().contains(Pitch::C),
            "{quality:?} kept its root"
        );
        assert_eq!(
            rootless.pitch_set().insert(Pitch::C),
            chord.pitches(),
            "{quality:?} lost more than its root"
        );
    }
}

#[test]
fn a_chord_of_nothing_but_its_root_voices_to_nothing() {
    // Honest rather than surprising: there is no note left to play.
    let bare = Chord::new(Pitch::C, PitchSet::EMPTY);
    assert_eq!(bare.voice_as(4, Voicing::Rootless), Notes::EMPTY);
}

#[test]
fn dropping_the_second_voice_lowers_it_an_octave() {
    // C4 E4 G4 B4 becomes G3 C4 E4 B4: the G fell, nothing else moved.
    let drop2 = Chord::maj7(Pitch::C).voice_as(4, Voicing::Drop2);
    assert_eq!(drop2.to_string(), "[G3 C4 E4 B4]");
}

#[test]
fn dropping_the_second_voice_moves_exactly_one_octave_of_weight() {
    // Drop 2 lowers one voice by twelve semitones and leaves the others alone,
    // so the total height falls by exactly twelve. Note it does not always
    // widen the chord: in a thirteenth the second voice already sits more than
    // an octave up, so dropping it lands it above the bass rather than below.
    for quality in ChordQuality::ALL {
        let chord = Chord::from_quality(Pitch::C, quality);
        let stacked = chord.voice_as(4, Voicing::Stacked);
        let drop2 = chord.voice_as(4, Voicing::Drop2);

        let height = |notes: &Notes| {
            notes
                .as_slice()
                .iter()
                .map(|n| i32::from(n.midi()))
                .sum::<i32>()
        };
        assert_eq!(
            height(&stacked) - height(&drop2),
            12,
            "{quality:?} did not move one voice by an octave"
        );
    }
}

#[test]
fn an_unnamed_chord_stacks_in_close_position() {
    // There is no canonical stack for a shape with no name, so the documented
    // fallback is the one arrangement that always works.
    let cluster = Chord::new(
        Pitch::C,
        PitchSet::from_pitches(&[Pitch::C, Pitch::C_SHARP, Pitch::E, Pitch::F_SHARP]),
    );
    assert_eq!(cluster.quality(), None);
    assert_eq!(
        cluster.voice_as(4, Voicing::Stacked),
        cluster.voice(4),
        "an unnamed chord should fall back to close position"
    );
}

#[test]
fn every_voicing_fits_in_a_notes_list() {
    // Six notes at most in the table, sixteen slots in a `Notes`. Nothing here
    // can silently drop a note, but the claim is cheap to check.
    for root in Pitch::ALL {
        for quality in ChordQuality::ALL {
            let chord = Chord::from_quality(root, quality);
            for voicing in [
                Voicing::Close,
                Voicing::Stacked,
                Voicing::Drop2,
                Voicing::Rootless,
            ] {
                let voiced = chord.voice_as(4, voicing);
                assert!(
                    voiced.len() <= Notes::CAPACITY,
                    "{quality:?} as {voicing:?} overflowed"
                );
            }
        }
    }
}

// --- turning a chord over -------------------------------------------------

#[test]
fn rotating_up_moves_the_bass_up_an_octave() {
    let root_position = Chord::major(Pitch::C).voice(4);
    let first_inversion = root_position.rotate_up();

    assert_eq!(first_inversion.bass(), Some(Pitch::E.at(4)));
    assert_eq!(first_inversion.pitch_set(), root_position.pitch_set());
    assert_eq!(first_inversion.len(), root_position.len());
}

#[test]
fn rotating_through_every_voice_returns_the_chord_an_octave_higher() {
    // Rotating an n-note chord n times is the same as transposing it by an
    // octave, so the chords in between are exactly its inversions.
    for quality in ChordQuality::ALL {
        let start = Chord::from_quality(Pitch::C, quality).voice(4);

        let mut rotated = start;
        for _ in 0..start.len() {
            rotated = rotated.rotate_up();
        }

        assert_eq!(
            rotated,
            start.sorted().transpose(Interval::OCTAVE),
            "{quality:?} did not come back an octave up"
        );
    }
}

#[test]
fn rotating_down_undoes_rotating_up() {
    for quality in ChordQuality::ALL {
        let start = Chord::from_quality(Pitch::C, quality).voice(4).sorted();
        assert_eq!(
            start.rotate_up().rotate_down(),
            start,
            "{quality:?} did not come back"
        );
    }
}

#[test]
fn every_inversion_is_reachable_by_rotating() {
    // A seventh chord has four of them, one per chord tone in the bass.
    let root_position = Chord::maj7(Pitch::C).voice(4);
    let first = root_position.rotate_up();
    let second = first.rotate_up();
    let third = second.rotate_up();

    assert_eq!(root_position.bass(), Some(Pitch::C.at(4)));
    assert_eq!(first.bass(), Some(Pitch::E.at(4)));
    assert_eq!(second.bass(), Some(Pitch::G.at(4)));
    assert_eq!(third.bass(), Some(Pitch::B.at(4)));
}

#[test]
fn rotating_an_empty_list_changes_nothing() {
    assert_eq!(Notes::EMPTY.rotate_up(), Notes::EMPTY);
    assert_eq!(Notes::EMPTY.rotate_down(), Notes::EMPTY);
}

#[test]
fn rotating_one_note_just_moves_it() {
    let single = Notes::from_slice(&[Pitch::C.at(4)]).unwrap();

    assert_eq!(single.rotate_up().bass(), Some(Pitch::C.at(5)));
    assert_eq!(single.rotate_down().bass(), Some(Pitch::C.at(3)));
}
