// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! Inversions, slash chords, and how open a voicing is.
//!
//! The thread running through these is the bass. A set of pitch classes cannot
//! have one, which is why naming a voicing is a better-informed question than
//! naming a set.

use music_core::{Chord, ChordQuality, Interval, Notes, Pitch, PitchSet, Voicing};

// --- the bass -------------------------------------------------------------

#[test]
fn the_bass_defaults_to_the_root() {
    for root in Pitch::ALL {
        for quality in ChordQuality::ALL {
            let chord = Chord::from_quality(root, quality);
            assert_eq!(chord.bass(), root, "{quality:?} on {root}");
            assert!(!chord.is_inverted());
            assert_eq!(chord.inversion(), 0);
        }
    }
}

#[test]
fn over_moves_the_bass_without_moving_the_root() {
    let c = Chord::major(Pitch::C);
    let first = c.over(Pitch::E);

    assert_eq!(first.root(), Pitch::C, "the root does not move");
    assert_eq!(first.bass(), Pitch::E);
    assert!(first.is_inverted());
    assert_eq!(first.pitches(), c.pitches(), "no pitch was added");
}

#[test]
fn a_bass_from_outside_the_chord_joins_it() {
    // `over` is not a pure relabelling: the bass is a note that sounds.
    let a_minor = Chord::minor(Pitch::A);
    let slash = a_minor.over(Pitch::F_SHARP);

    assert_eq!(slash.size(), a_minor.size() + 1);
    assert!(slash.contains(Pitch::F_SHARP));
    assert_eq!(slash.root(), Pitch::A);
}

#[test]
fn the_bass_cannot_be_removed() {
    let slash = Chord::major(Pitch::C).over(Pitch::E);
    assert_eq!(slash.without(Pitch::E), slash, "the bass sounds");
    assert_eq!(slash.without(Pitch::C), slash, "the root is the root");
    assert!(!slash.without(Pitch::G).contains(Pitch::G));
}

#[test]
fn root_position_can_be_restored() {
    let slash = Chord::maj7(Pitch::C).over(Pitch::B);
    let restored = slash.in_root_position();

    assert_eq!(restored, Chord::maj7(Pitch::C));
    assert!(!restored.is_inverted());
}

#[test]
fn inversions_are_numbered_upward_from_the_root() {
    let c = Chord::maj7(Pitch::C); // C E G B

    assert_eq!(c.inversion(), 0);
    assert_eq!(c.over(Pitch::E).inversion(), 1);
    assert_eq!(c.over(Pitch::G).inversion(), 2);
    assert_eq!(c.over(Pitch::B).inversion(), 3);
}

#[test]
fn every_note_of_every_chord_names_an_inversion() {
    for quality in ChordQuality::ALL {
        let chord = Chord::from_quality(Pitch::C, quality);
        let mut seen = 0u8;
        for pitch in chord.pitches().iter() {
            let inverted = chord.over(pitch);
            assert!(
                (inverted.inversion() as usize) < quality.size(),
                "{quality:?} over {pitch} gave inversion {}",
                inverted.inversion()
            );
            seen += 1;
        }
        assert_eq!(seen as usize, quality.size(), "{quality:?}");
    }
}

// --- writing them down ----------------------------------------------------

#[test]
fn slash_chords_print_with_a_slash() {
    assert_eq!(Chord::major(Pitch::C).to_string(), "C");
    assert_eq!(Chord::major(Pitch::C).over(Pitch::E).to_string(), "C/E");
    assert_eq!(Chord::major(Pitch::C).over(Pitch::G).to_string(), "C/G");
    assert_eq!(Chord::maj7(Pitch::C).over(Pitch::B).to_string(), "Cmaj7/B");
}

#[test]
fn slash_chords_round_trip_through_text() {
    for root in Pitch::ALL {
        for quality in ChordQuality::ALL {
            let chord = Chord::from_quality(root, quality);
            for bass in chord.pitches().iter() {
                let inverted = chord.over(bass);
                let text = inverted.to_string();
                assert_eq!(text.parse::<Chord>().unwrap(), inverted, "{text}");
            }
        }
    }
}

#[test]
fn a_six_nine_chord_is_not_a_slash_chord() {
    // The quality itself contains a slash, so parsing splits on the last one.
    let six_nine: Chord = "C6/9".parse().unwrap();
    assert_eq!(six_nine.quality(), Some(ChordQuality::SixNine));
    assert!(!six_nine.is_inverted());

    let inverted: Chord = "C6/9/E".parse().unwrap();
    assert_eq!(inverted.quality(), Some(ChordQuality::SixNine));
    assert_eq!(inverted.bass(), Pitch::E);
}

#[test]
fn unnamed_chords_take_a_slash_too() {
    let odd = Chord::new(
        Pitch::C,
        PitchSet::from_pitches(&[Pitch::C, Pitch::C_SHARP, Pitch::E, Pitch::F_SHARP]),
    );
    let inverted = odd.over(Pitch::F_SHARP);

    assert_eq!(inverted.to_string(), "C[0,1,4,6]/F#");
    assert_eq!(
        inverted.to_string().parse::<Chord>().unwrap(),
        inverted,
        "the bracket form must round-trip with a bass"
    );
}

// --- spread ---------------------------------------------------------------

#[test]
fn an_open_voicing_spans_more_than_an_octave() {
    for quality in ChordQuality::ALL {
        let chord = Chord::from_quality(Pitch::C, quality);
        let close = chord.voice_as(3, Voicing::Close);
        let open = chord.voice_as(3, Voicing::Open);

        if close.len() < 2 {
            continue;
        }
        assert!(close.is_close(), "{quality:?} close voicing was not close");
        assert!(
            open.spread().unwrap() > close.spread().unwrap(),
            "{quality:?} open voicing was no wider than close"
        );
    }
}

#[test]
fn opening_a_chord_changes_no_pitch() {
    // Spreading moves notes between octaves and nothing else.
    for quality in ChordQuality::ALL {
        let chord = Chord::from_quality(Pitch::C, quality);
        assert_eq!(
            chord.voice_as(3, Voicing::Open).pitch_set(),
            chord.voice_as(3, Voicing::Close).pitch_set(),
            "{quality:?}"
        );
    }
}

#[test]
fn a_triad_opens_to_root_fifth_third() {
    let open = Chord::major(Pitch::C).voice_as(3, Voicing::Open);
    let midi: Vec<i16> = open.sorted().as_slice().iter().map(|n| n.midi()).collect();

    // C3, G3, E4: the classic open position.
    assert_eq!(midi, vec![48, 55, 64]);
}

#[test]
fn spread_measures_lowest_to_highest() {
    let empty = Notes::EMPTY;
    assert_eq!(empty.spread(), None);
    assert!(empty.is_close());

    let one = Notes::from_slice(&[Pitch::C.at(4)]).unwrap();
    assert_eq!(one.spread(), Some(Interval::UNISON));
    assert!(one.is_close());

    let octave = Notes::from_slice(&[Pitch::C.at(4), Pitch::C.at(5)]).unwrap();
    assert_eq!(octave.spread(), Some(Interval::OCTAVE));
    assert!(!octave.is_close(), "an octave is not inside an octave");

    let wide = Notes::from_slice(&[Pitch::C.at(2), Pitch::C.at(6)]).unwrap();
    assert_eq!(wide.spread().unwrap().semitones(), 48);
}

// --- naming a voicing -----------------------------------------------------

#[test]
fn a_voicing_is_named_by_what_is_underneath_it() {
    // The same four pitch classes, and the bass decides.
    let rooted_on_a = Notes::from_slice(&[
        Pitch::A.at(3),
        Pitch::C.at(4),
        Pitch::E.at(4),
        Pitch::G.at(4),
    ])
    .unwrap();
    let rooted_on_c = Notes::from_slice(&[
        Pitch::C.at(3),
        Pitch::E.at(3),
        Pitch::G.at(3),
        Pitch::A.at(4),
    ])
    .unwrap();

    assert_eq!(rooted_on_a.pitch_set(), rooted_on_c.pitch_set());
    assert_eq!(rooted_on_a.identify(), Some(Chord::min7(Pitch::A)));
    assert_eq!(rooted_on_c.identify(), Some(Chord::sixth(Pitch::C)));
}

#[test]
fn a_bass_that_roots_nothing_becomes_a_slash() {
    // E under a C major triad roots no chord, so the reading keeps C as root
    // and records what is actually at the bottom.
    let first_inversion =
        Notes::from_slice(&[Pitch::E.at(3), Pitch::G.at(3), Pitch::C.at(4)]).unwrap();

    let named = first_inversion.identify().unwrap();
    assert_eq!(named, Chord::major(Pitch::C).over(Pitch::E));
    assert_eq!(named.to_string(), "C/E");
    assert_eq!(named.inversion(), 1);
}

#[test]
fn an_empty_voicing_names_nothing() {
    assert_eq!(Notes::EMPTY.identify(), None);
}

#[test]
fn root_position_chords_round_trip_through_a_voicing() {
    // Voice it, name it back, get what you started with. This holds exactly in
    // root position, where the bass is the root and that reading always exists.
    for root in Pitch::ALL {
        for quality in ChordQuality::ALL {
            let chord = Chord::from_quality(root, quality);
            for voicing in [Voicing::Close, Voicing::Stacked, Voicing::Open] {
                let voiced = chord.voice_as(3, voicing);
                assert_eq!(
                    voiced.identify(),
                    Some(chord),
                    "{quality:?} on {root} as {voicing:?}"
                );
            }
        }
    }
}

#[test]
fn an_inverted_voicing_keeps_its_pitches_and_its_bass() {
    // Inversions cannot round-trip exactly, and that is honest rather than a
    // shortcoming: C6 over E and Am7 over E are the same sounding notes, and
    // only the music around them decides. What must survive is the evidence.
    for root in Pitch::ALL {
        for quality in ChordQuality::ALL {
            let chord = Chord::from_quality(root, quality);
            for bass in chord.pitches().iter() {
                let voiced = chord.over(bass).voice_as(3, Voicing::Close);
                let named = voiced.identify().unwrap();

                assert_eq!(named.pitches(), chord.pitches(), "{quality:?} over {bass}");
                assert_eq!(named.bass(), bass, "{quality:?} over {bass}");
            }
        }
    }
}

#[test]
fn voicing_a_slash_chord_sounds_its_bass() {
    for root in Pitch::ALL {
        for quality in ChordQuality::ALL {
            let chord = Chord::from_quality(root, quality);
            for bass in chord.pitches().iter() {
                for voicing in [Voicing::Close, Voicing::Stacked, Voicing::Open] {
                    let voiced = chord.over(bass).voice_as(3, voicing);
                    assert_eq!(
                        voiced.bass().unwrap().pitch(),
                        bass,
                        "{quality:?} over {bass} as {voicing:?} did not sound its bass"
                    );
                }
            }
        }
    }
}

#[test]
fn every_chord_tone_in_the_bass_is_its_own_inversion_from_any_root() {
    // Pinned after `inversion` read A minor 7 over E as root position: the
    // bass's distance from the root was taken by subtracting bytes, which
    // wraps whenever the bass's pitch class is numerically below the root's.
    for root in Pitch::ALL {
        for quality in ChordQuality::ALL {
            let chord = Chord::from_quality(root, quality);
            for (index, offset) in chord.intervals().iter().enumerate() {
                let bass = root.transpose(Interval::new(offset.value() as i16));
                assert_eq!(
                    chord.over(bass).inversion() as usize,
                    index,
                    "{chord} over {bass}"
                );
            }
        }
    }
    assert_eq!(Chord::min7(Pitch::A).over(Pitch::E).inversion(), 2);
}
