// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! The common names of scales and scale families.
//!
//! Names are facts of usage, not of structure, so they live in a table rather
//! than in [`Scale`]. Each family lists its reference mode first, the one the
//! family is known by, then whichever of its other modes have a name of their
//! own. A mode left out simply has no name.
//!
//! The table covers every symmetric family (there are fifteen with three or
//! more notes), the four seven-note families of common practice and jazz, the
//! named pentatonic and hexatonic scales, and the triads and seventh chords,
//! which are scales of three and four notes too. A test holds it to one name
//! per mode and one family per entry.

use crate::scale::{Scale, ScaleFamily};

/// One named family: its name, a three-letter tag, and its named modes, the
/// reference mode first.
struct Family {
    name: &'static str,
    abbreviation: &'static str,
    modes: &'static [(u16, &'static str)],
}

/// The mask of a scale given as semitones above its root.
const fn bits(semitones: &[u8]) -> u16 {
    let mut out = 0u16;
    let mut i = 0;
    while i < semitones.len() {
        out |= 1 << semitones[i];
        i += 1;
    }
    out
}

#[rustfmt::skip]
const FAMILIES: &[Family] = &[
    // The symmetric families, by note count.
    Family { name: "Augmented triad", abbreviation: "AuT", modes: &[
        (bits(&[0, 4, 8]), "Augmented triad"),
    ]},
    Family { name: "Diminished seventh", abbreviation: "Di7", modes: &[
        (bits(&[0, 3, 6, 9]), "Diminished seventh"),
    ]},
    Family { name: "French sixth", abbreviation: "Fr6", modes: &[
        (bits(&[0, 4, 6, 10]), "Dominant 7 flat 5"),
        (bits(&[0, 2, 6, 8]), "French sixth"),
    ]},
    Family { name: "Tritone pairs", abbreviation: "TrP", modes: &[
        (bits(&[0, 1, 6, 7]), "Tritone pairs"),
    ]},
    Family { name: "Whole tone", abbreviation: "WhT", modes: &[
        (bits(&[0, 2, 4, 6, 8, 10]), "Whole tone"),
    ]},
    Family { name: "Augmented", abbreviation: "Aug", modes: &[
        (bits(&[0, 3, 4, 7, 8, 11]), "Augmented"),
        (bits(&[0, 1, 4, 5, 8, 9]), "Augmented inverse"),
    ]},
    Family { name: "Tritone", abbreviation: "Tri", modes: &[
        (bits(&[0, 1, 4, 6, 7, 10]), "Tritone"),
    ]},
    Family { name: "Messiaen 5", abbreviation: "Me5", modes: &[
        (bits(&[0, 1, 5, 6, 7, 11]), "Messiaen 5"),
    ]},
    Family { name: "Truncated octatonic", abbreviation: "TrO", modes: &[
        (bits(&[0, 1, 3, 6, 7, 9]), "Truncated octatonic"),
    ]},
    Family { name: "Octatonic", abbreviation: "Oct", modes: &[
        (bits(&[0, 1, 3, 4, 6, 7, 9, 10]), "Half-whole diminished"),
        (bits(&[0, 2, 3, 5, 6, 8, 9, 11]), "Whole-half diminished"),
    ]},
    Family { name: "Messiaen 4", abbreviation: "Me4", modes: &[
        (bits(&[0, 1, 2, 5, 6, 7, 8, 11]), "Messiaen 4"),
    ]},
    Family { name: "Messiaen 6", abbreviation: "Me6", modes: &[
        (bits(&[0, 2, 4, 5, 6, 8, 10, 11]), "Messiaen 6"),
    ]},
    Family { name: "Messiaen 3", abbreviation: "Me3", modes: &[
        (bits(&[0, 2, 3, 4, 6, 7, 8, 10, 11]), "Messiaen 3"),
    ]},
    Family { name: "Messiaen 7", abbreviation: "Me7", modes: &[
        (bits(&[0, 1, 2, 3, 5, 6, 7, 8, 9, 11]), "Messiaen 7"),
    ]},
    Family { name: "Chromatic", abbreviation: "Chr", modes: &[
        (bits(&[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11]), "Chromatic"),
    ]},

    // Triads and seventh chords, as the scales of three and four notes.
    Family { name: "Major triad", abbreviation: "Maj", modes: &[
        (bits(&[0, 4, 7]), "Major triad"),
    ]},
    Family { name: "Minor triad", abbreviation: "Min", modes: &[
        (bits(&[0, 3, 7]), "Minor triad"),
    ]},
    Family { name: "Diminished triad", abbreviation: "DiT", modes: &[
        (bits(&[0, 3, 6]), "Diminished triad"),
    ]},
    Family { name: "Suspended", abbreviation: "Sus", modes: &[
        (bits(&[0, 2, 7]), "Sus2"),
        (bits(&[0, 5, 7]), "Sus4"),
        (bits(&[0, 5, 10]), "Quartal"),
    ]},
    Family { name: "Major seventh", abbreviation: "Ma7", modes: &[
        (bits(&[0, 4, 7, 11]), "Major seventh"),
    ]},
    Family { name: "Dominant seventh", abbreviation: "Dom", modes: &[
        (bits(&[0, 4, 7, 10]), "Dominant seventh"),
    ]},
    Family { name: "Minor seventh", abbreviation: "Mi7", modes: &[
        (bits(&[0, 3, 7, 10]), "Minor seventh"),
        (bits(&[0, 4, 7, 9]), "Major sixth"),
    ]},
    Family { name: "Half-diminished", abbreviation: "HDi", modes: &[
        (bits(&[0, 3, 6, 10]), "Half-diminished seventh"),
        (bits(&[0, 3, 7, 9]), "Minor sixth"),
    ]},
    Family { name: "Minor-major seventh", abbreviation: "MM7", modes: &[
        (bits(&[0, 3, 7, 11]), "Minor-major seventh"),
    ]},

    // Pentatonic and hexatonic scales.
    Family { name: "Pentatonic", abbreviation: "Pen", modes: &[
        (bits(&[0, 2, 4, 7, 9]), "Major pentatonic"),
        (bits(&[0, 2, 5, 7, 10]), "Suspended pentatonic"),
        (bits(&[0, 3, 5, 8, 10]), "Man gong"),
        (bits(&[0, 2, 5, 7, 9]), "Ritsu"),
        (bits(&[0, 3, 5, 7, 10]), "Minor pentatonic"),
    ]},
    Family { name: "Hirajoshi", abbreviation: "Hir", modes: &[
        (bits(&[0, 2, 3, 7, 8]), "Hirajoshi"),
        (bits(&[0, 1, 5, 6, 10]), "Iwato"),
        (bits(&[0, 1, 5, 7, 8]), "In"),
    ]},
    Family { name: "Kumoi", abbreviation: "Kum", modes: &[
        (bits(&[0, 2, 3, 7, 9]), "Kumoi"),
    ]},
    Family { name: "Dominant pentatonic", abbreviation: "DPe", modes: &[
        (bits(&[0, 2, 4, 7, 10]), "Dominant pentatonic"),
    ]},
    Family { name: "Blues", abbreviation: "Blu", modes: &[
        (bits(&[0, 3, 5, 6, 7, 10]), "Blues"),
        (bits(&[0, 2, 3, 4, 7, 9]), "Major blues"),
    ]},
    Family { name: "Prometheus", abbreviation: "Pro", modes: &[
        (bits(&[0, 2, 4, 6, 9, 10]), "Prometheus"),
    ]},

    // Seven-note families.
    Family { name: "Diatonic", abbreviation: "Dia", modes: &[
        (bits(&[0, 2, 4, 5, 7, 9, 11]), "Ionian"),
        (bits(&[0, 2, 3, 5, 7, 9, 10]), "Dorian"),
        (bits(&[0, 1, 3, 5, 7, 8, 10]), "Phrygian"),
        (bits(&[0, 2, 4, 6, 7, 9, 11]), "Lydian"),
        (bits(&[0, 2, 4, 5, 7, 9, 10]), "Mixolydian"),
        (bits(&[0, 2, 3, 5, 7, 8, 10]), "Aeolian"),
        (bits(&[0, 1, 3, 5, 6, 8, 10]), "Locrian"),
    ]},
    Family { name: "Melodic minor", abbreviation: "Mel", modes: &[
        (bits(&[0, 2, 3, 5, 7, 9, 11]), "Melodic minor"),
        (bits(&[0, 1, 3, 5, 7, 9, 10]), "Dorian b2"),
        (bits(&[0, 2, 4, 6, 8, 9, 11]), "Lydian augmented"),
        (bits(&[0, 2, 4, 6, 7, 9, 10]), "Lydian dominant"),
        (bits(&[0, 2, 4, 5, 7, 8, 10]), "Mixolydian b6"),
        (bits(&[0, 2, 3, 5, 6, 8, 10]), "Locrian #2"),
        (bits(&[0, 1, 3, 4, 6, 8, 10]), "Altered"),
    ]},
    Family { name: "Harmonic minor", abbreviation: "HMi", modes: &[
        (bits(&[0, 2, 3, 5, 7, 8, 11]), "Harmonic minor"),
        (bits(&[0, 1, 3, 5, 6, 9, 10]), "Locrian #6"),
        (bits(&[0, 2, 4, 5, 8, 9, 11]), "Ionian #5"),
        (bits(&[0, 2, 3, 6, 7, 9, 10]), "Dorian #4"),
        (bits(&[0, 1, 4, 5, 7, 8, 10]), "Phrygian dominant"),
        (bits(&[0, 3, 4, 6, 7, 9, 11]), "Lydian #2"),
        (bits(&[0, 1, 3, 4, 6, 8, 9]), "Altered bb7"),
    ]},
    Family { name: "Harmonic major", abbreviation: "HMa", modes: &[
        (bits(&[0, 2, 4, 5, 7, 8, 11]), "Harmonic major"),
        (bits(&[0, 2, 3, 5, 6, 9, 10]), "Dorian b5"),
        (bits(&[0, 1, 3, 4, 7, 8, 10]), "Phrygian b4"),
        (bits(&[0, 2, 3, 6, 7, 9, 11]), "Lydian b3"),
        (bits(&[0, 1, 4, 5, 7, 9, 10]), "Mixolydian b2"),
        (bits(&[0, 3, 4, 6, 8, 9, 11]), "Lydian augmented #2"),
        (bits(&[0, 1, 3, 5, 6, 8, 9]), "Locrian bb7"),
    ]},
    Family { name: "Double harmonic", abbreviation: "DHa", modes: &[
        (bits(&[0, 1, 4, 5, 7, 8, 11]), "Double harmonic"),
        (bits(&[0, 3, 4, 6, 7, 10, 11]), "Lydian #2 #6"),
        (bits(&[0, 1, 3, 4, 7, 8, 9]), "Ultraphrygian"),
        (bits(&[0, 2, 3, 6, 7, 8, 11]), "Hungarian minor"),
        (bits(&[0, 1, 4, 5, 6, 9, 10]), "Oriental"),
        (bits(&[0, 3, 4, 5, 8, 9, 11]), "Ionian #2 #5"),
        (bits(&[0, 1, 2, 5, 6, 8, 9]), "Locrian bb3 bb7"),
    ]},
    Family { name: "Neapolitan major", abbreviation: "NMa", modes: &[
        (bits(&[0, 1, 3, 5, 7, 9, 11]), "Neapolitan major"),
        (bits(&[0, 2, 4, 6, 8, 10, 11]), "Leading whole tone"),
        (bits(&[0, 2, 4, 6, 8, 9, 10]), "Lydian augmented dominant"),
        (bits(&[0, 2, 4, 6, 7, 8, 10]), "Lydian dominant b6"),
        (bits(&[0, 2, 4, 5, 6, 8, 10]), "Major Locrian"),
    ]},
    Family { name: "Neapolitan minor", abbreviation: "NMi", modes: &[
        (bits(&[0, 1, 3, 5, 7, 8, 11]), "Neapolitan minor"),
        (bits(&[0, 2, 4, 6, 7, 10, 11]), "Lydian #6"),
        (bits(&[0, 2, 3, 6, 7, 8, 10]), "Romani minor"),
        (bits(&[0, 3, 4, 5, 7, 9, 11]), "Ionian #2"),
    ]},
    Family { name: "Hungarian major", abbreviation: "HuM", modes: &[
        (bits(&[0, 3, 4, 6, 7, 9, 10]), "Hungarian major"),
    ]},
    Family { name: "Enigmatic", abbreviation: "Eni", modes: &[
        (bits(&[0, 1, 4, 6, 8, 10, 11]), "Enigmatic"),
    ]},
    Family { name: "Persian", abbreviation: "Per", modes: &[
        (bits(&[0, 1, 4, 5, 6, 8, 11]), "Persian"),
    ]},

    // Eight-note scales.
    Family { name: "Bebop dominant", abbreviation: "BeD", modes: &[
        (bits(&[0, 2, 4, 5, 7, 9, 10, 11]), "Bebop dominant"),
        (bits(&[0, 2, 3, 4, 5, 7, 9, 10]), "Bebop Dorian"),
    ]},
    Family { name: "Bebop major", abbreviation: "BeM", modes: &[
        (bits(&[0, 2, 4, 5, 7, 8, 9, 11]), "Bebop major"),
    ]},
];

/// The entry whose family is `family`, if there is one.
fn entry(family: ScaleFamily) -> Option<&'static Family> {
    FAMILIES
        .iter()
        .find(|entry| Scale::from_bits(entry.modes[0].0).map(ScaleFamily::new) == Some(family))
}

/// The common name of this exact mode.
pub(crate) fn mode_name(scale: Scale) -> Option<&'static str> {
    FAMILIES
        .iter()
        .flat_map(|entry| entry.modes.iter())
        .find(|(bits, _)| *bits == scale.bits())
        .map(|(_, name)| *name)
}

/// The common name of the family.
pub(crate) fn family_name(family: ScaleFamily) -> Option<&'static str> {
    entry(family).map(|entry| entry.name)
}

/// The family's three-letter tag.
pub(crate) fn family_abbreviation(family: ScaleFamily) -> Option<&'static str> {
    entry(family).map(|entry| entry.abbreviation)
}

/// The mode the family is known by.
pub(crate) fn family_reference(family: ScaleFamily) -> Option<Scale> {
    entry(family).and_then(|entry| Scale::from_bits(entry.modes[0].0))
}

/// Iterator over the families with a common name, from
/// [`ScaleFamily::named`], in the order of the table: symmetric families
/// first, then chords, pentatonic and hexatonic, seven- and eight-note scales.
#[derive(Clone, Copy, Debug, Default)]
pub struct NamedFamilies {
    next: usize,
}

impl NamedFamilies {
    pub(crate) const fn new() -> Self {
        Self { next: 0 }
    }
}

impl Iterator for NamedFamilies {
    type Item = ScaleFamily;

    fn next(&mut self) -> Option<ScaleFamily> {
        while self.next < FAMILIES.len() {
            let entry = &FAMILIES[self.next];
            self.next += 1;
            if let Some(scale) = Scale::from_bits(entry.modes[0].0) {
                return Some(ScaleFamily::new(scale));
            }
        }
        None
    }
}

impl core::iter::FusedIterator for NamedFamilies {}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_named_mode_belongs_to_its_entry() {
        for entry in FAMILIES {
            let family = Scale::from_bits(entry.modes[0].0).map(ScaleFamily::new);
            assert!(family.is_some(), "{} has no root", entry.name);
            for (bits, name) in entry.modes {
                let scale = Scale::from_bits(*bits);
                assert!(scale.is_some(), "{name} has no root");
                assert_eq!(
                    scale.map(ScaleFamily::new),
                    family,
                    "{name} is not a mode of {}",
                    entry.name
                );
            }
        }
    }

    #[test]
    fn abbreviations_are_three_letters_and_unique() {
        for (i, a) in FAMILIES.iter().enumerate() {
            assert_eq!(a.abbreviation.len(), 3, "{}", a.name);
            for b in &FAMILIES[i + 1..] {
                assert_ne!(a.abbreviation, b.abbreviation);
                assert_ne!(a.name, b.name);
            }
        }
    }
}
