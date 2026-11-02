// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! Chord types: a base chord and tensions, and how the named qualities map
//! onto them.

use music_core::{
    ChordBase, ChordFamily, ChordQuality, ChordRow, ChordScale, ChordType, Scale, ScaleDegree,
    Tension, Tensions,
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
    assert_eq!(family(ChordQuality::Augmented), ChordFamily::Other);
    assert_eq!(family(ChordQuality::MinorMajor7), ChordFamily::Other);
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

fn grammar() -> &'static [ChordType] {
    static ALL: std::sync::OnceLock<Vec<ChordType>> = std::sync::OnceLock::new();
    ALL.get_or_init(|| ChordType::all().collect())
}

fn union_of_tensions(base: ChordBase) -> Vec<Tension> {
    Tension::ALL
        .into_iter()
        .filter(|&t| {
            grammar()
                .iter()
                .any(|c| c.base() == base && c.tensions().contains(t))
        })
        .collect()
}

#[test]
fn the_grammar_holds_every_named_quality_but_two() {
    let all = grammar();
    assert_eq!(all.len(), 111, "the grammar changed: update docs/chords.md");
    for quality in ChordQuality::ALL {
        let chord = quality.chord_type();
        let expected = !matches!(quality, ChordQuality::Fifth | ChordQuality::Dominant11);
        assert_eq!(all.contains(&chord), expected, "{quality:?}");
        if quality != ChordQuality::MinorMajor7 {
            assert_eq!(chord.symbol().as_str(), quality.symbol(), "{quality:?}");
        }
    }
    assert_eq!(
        ChordQuality::MinorMajor7.chord_type().symbol().as_str(),
        "m(maj7)"
    );
}

#[test]
fn tensions_are_berklees_lists() {
    use Tension::*;
    assert_eq!(
        union_of_tensions(ChordBase::Major7),
        [Nine, SharpEleven, Thirteen]
    );
    assert_eq!(
        union_of_tensions(ChordBase::Dominant7),
        [
            FlatNine,
            Nine,
            SharpNine,
            SharpEleven,
            FlatThirteen,
            Thirteen
        ]
    );
    assert_eq!(
        union_of_tensions(ChordBase::HalfDiminished7),
        [Nine, Eleven, FlatThirteen, Thirteen]
    );
    assert_eq!(
        union_of_tensions(ChordBase::Diminished7),
        [Nine, Eleven, FlatThirteen]
    );
    assert_eq!(
        union_of_tensions(ChordBase::Dominant7Sus4),
        [FlatNine, Nine, FlatThirteen, Thirteen]
    );
    assert_eq!(union_of_tensions(ChordBase::Major6), [Nine]);
}

#[test]
fn symbols_follow_the_conventions() {
    let symbols: Vec<String> = grammar()
        .iter()
        .map(|c| c.symbol().as_str().to_owned())
        .collect();
    for expected in [
        "",
        "m",
        "dim",
        "aug",
        "add9",
        "madd9",
        "6/9",
        "m6/9",
        "maj9#11",
        "maj13#11",
        "13b9",
        "13#9#11",
        "9b13",
        "7b9#9",
        "m11",
        "m13",
        "m9b5",
        "m11b5",
        "7sus4b9",
        "13sus4b9",
        "dim7(9,b13)",
        "maj7#5",
        "9#5",
        "m(maj9)",
    ] {
        assert!(
            symbols.iter().any(|s| s == expected),
            "{expected:?} missing"
        );
    }
    for refused in ["11", "maj7#9", "9b9", "13b13", "7sus4#9", "maj11"] {
        assert!(
            !symbols.iter().any(|s| s == refused),
            "{refused:?} should not be in the grammar"
        );
    }
}

#[test]
fn numerals_read_like_the_textbooks() {
    use ChordBase::*;
    let num = |base: ChordBase, number: u8, accidental: i8| {
        ChordType::plain(base)
            .numeral(ScaleDegree::new(number, accidental))
            .as_str()
            .to_owned()
    };
    // Major-key triads and sevenths.
    assert_eq!(
        [
            num(Major, 1, 0),
            num(Minor, 2, 0),
            num(Minor, 3, 0),
            num(Major, 4, 0),
            num(Major, 5, 0),
            num(Minor, 6, 0),
            num(Diminished, 7, 0)
        ],
        ["I", "ii", "iii", "IV", "V", "vi", "vii°"]
    );
    assert_eq!(
        [
            num(Major7, 1, 0),
            num(Minor7, 2, 0),
            num(Dominant7, 5, 0),
            num(HalfDiminished7, 7, 0),
            num(Diminished7, 7, 0)
        ],
        ["Imaj7", "ii7", "V7", "viiø7", "vii°7"]
    );
    // Aeolian, relative to the major scale on the tonic.
    assert_eq!(
        [
            num(Minor, 1, 0),
            num(Diminished, 2, 0),
            num(Major, 3, -1),
            num(Minor, 4, 0),
            num(Minor, 5, 0),
            num(Major, 6, -1),
            num(Major, 7, -1)
        ],
        ["i", "ii°", "bIII", "iv", "v", "bVI", "bVII"]
    );
    assert_eq!(num(MinorMajor7, 1, 0), "imaj7");
    assert_eq!(num(Augmented, 3, -1), "bIII+");
    assert_eq!(num(Sus4, 5, 0), "Vsus4");
    let ninth = |b: ChordBase, ts: &[Tension]| {
        let t = ts.iter().fold(Tensions::NONE, |acc, &x| acc.with(x));
        ChordType::new(b, t)
            .unwrap()
            .numeral(ScaleDegree::new(5, 0))
            .as_str()
            .to_owned()
    };
    assert_eq!(ninth(Dominant7, &[Tension::FlatNine]), "V7b9");
    assert_eq!(ninth(Dominant7, &[Tension::Nine, Tension::Thirteen]), "V13");
    assert_eq!(ninth(Minor7, &[Tension::Nine]), "v9");
}

#[test]
fn a_dominant_takes_its_flat_nine_and_flat_thirteen() {
    let phrygian_dominant = Scale::new(&[0, 1, 4, 5, 7, 8, 10]);
    let pair = ChordScale::from_parts(phrygian_dominant, ChordQuality::Dominant7).unwrap();
    assert_eq!(pair.avoid_notes().len(), 1); // only the 11, a semitone over the third
    let mixolydian = ChordScale::from_parts(Scale::MAJOR.mode(4), ChordQuality::Dominant7).unwrap();
    assert_eq!(mixolydian.avoid_notes().len(), 1); // the 11
    // Over a minor seventh the b9 stays an avoid note.
    let phrygian = ChordScale::from_parts(Scale::MAJOR.mode(2), ChordQuality::Minor7).unwrap();
    assert_eq!(phrygian.avoid_notes().len(), 2);
}

#[test]
fn every_type_has_a_name_in_words() {
    let nine_sharp_eleven = ChordType::new(
        ChordBase::Dominant7,
        Tensions::NONE
            .with(Tension::FlatNine)
            .with(Tension::SharpEleven),
    )
    .unwrap();
    assert_eq!(
        nine_sharp_eleven.name().as_str(),
        "dominant 7 with flat 9, sharp 11"
    );
    assert!(grammar().iter().all(|c| !c.name().as_str().is_empty()));
}
