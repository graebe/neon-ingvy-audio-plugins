// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! Chord types: a base chord and tensions, and how the named qualities map
//! onto them.

use music_core::{
    ChordBase, ChordFamily, ChordQuality, ChordRow, ChordScale, ChordType, Scale, Tension, Tensions,
};

#[test]
fn every_quality_is_a_chord_type_with_the_same_notes_and_guide_tones() {
    for quality in ChordQuality::ALL {
        let chord = quality.chord_type();
        assert_eq!(chord.interval_set(), quality.interval_set(), "{quality:?}");
        assert_eq!(chord.guide_tones(), quality.guide_tones(), "{quality:?}");
        assert_eq!(chord.quality(), Some(quality), "{quality:?}");
    }
}

#[test]
fn a_tension_may_not_double_a_note_of_the_base() {
    let sharp_nine = Tensions::NONE.with(Tension::SharpNine);
    assert!(ChordType::new(ChordBase::Minor7, sharp_nine).is_none());
    assert!(ChordType::new(ChordBase::Dominant7, sharp_nine).is_some());
    let flat_thirteen = Tensions::NONE.with(Tension::FlatThirteen);
    assert!(ChordType::new(ChordBase::Augmented7, flat_thirteen).is_none());
}

#[test]
fn rows_follow_the_highest_tension() {
    let row = |q: ChordQuality| q.chord_type().row();
    assert_eq!(row(ChordQuality::Major), ChordRow::Triad);
    assert_eq!(row(ChordQuality::Sixth), ChordRow::Seventh);
    assert_eq!(row(ChordQuality::Diminished7), ChordRow::Seventh);
    assert_eq!(row(ChordQuality::Add9), ChordRow::Ninth);
    assert_eq!(row(ChordQuality::SixNine), ChordRow::Ninth);
    assert_eq!(row(ChordQuality::SevenSharpNine), ChordRow::Ninth);
    assert_eq!(row(ChordQuality::Minor11), ChordRow::Eleventh);
    assert_eq!(row(ChordQuality::SevenSharpEleven), ChordRow::Eleventh);
    assert_eq!(row(ChordQuality::Major13), ChordRow::Thirteenth);
}

#[test]
fn families_follow_the_base() {
    let family = |q: ChordQuality| q.chord_type().family();
    assert_eq!(family(ChordQuality::Sixth), ChordFamily::Major);
    assert_eq!(family(ChordQuality::Dominant13), ChordFamily::Dominant);
    assert_eq!(family(ChordQuality::SevenSharpFive), ChordFamily::Dominant);
    assert_eq!(family(ChordQuality::MinorSixth), ChordFamily::Minor);
    assert_eq!(
        family(ChordQuality::HalfDiminished7),
        ChordFamily::HalfDiminished
    );
    assert_eq!(family(ChordQuality::Augmented), ChordFamily::Augmented);
    assert_eq!(family(ChordQuality::Dominant7Sus4), ChordFamily::Suspended);
    for base in ChordBase::ALL {
        assert!(ChordFamily::ALL.contains(&base.family()));
    }
}

#[test]
fn a_chord_type_pairs_with_a_scale_like_a_quality() {
    let lydian = Scale::MAJOR.mode(3);
    let maj9_sharp11 = ChordType::new(
        ChordBase::Major7,
        Tensions::NONE
            .with(Tension::Nine)
            .with(Tension::SharpEleven),
    )
    .unwrap();
    let pair = ChordScale::from_chord(lydian, maj9_sharp11).unwrap();
    assert_eq!(pair.quality(), None);
    assert_eq!(pair.chord(), maj9_sharp11);
    assert_eq!(pair.avoid_notes().len(), 0);
    assert_eq!(pair.tensions().len(), 1); // the 13
    assert!(ChordScale::from_chord(Scale::MAJOR, maj9_sharp11).is_none());
}
