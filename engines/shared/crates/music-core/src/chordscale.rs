// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! A chord heard against a scale: which notes anchor it, which colour it,
//! and which rub.
//!
//! The chord is built on the scale's root and must lie inside the scale. A
//! chord on another degree is the same question asked of the mode on that
//! degree: D minor 7 in C major is the minor 7 against D Dorian. That is why
//! there is no degree argument anywhere here.

use crate::chord::ChordQuality;
use crate::pitch::Pitch;
use crate::pitchset::PitchSet;
use crate::scale::Scale;

impl ChordQuality {
    /// The tones that make the quality what it is, as intervals above the
    /// root: the root, the third, the seventh or sixth, and an altered fifth.
    ///
    /// The third is the major one when a chord has both (a 7#9). A chord
    /// without a third takes its suspension instead: the fourth of a sus4,
    /// the second of a sus2. A sixth counts only without a seventh, so a 6 or
    /// a dim7 has one and a 13th does not; a flattened or raised fifth counts
    /// only without a perfect one. The power chord keeps just its root.
    ///
    /// ```
    /// use music_core::{ChordQuality, PitchSet};
    ///
    /// let guide = ChordQuality::Dominant7.guide_tones();
    /// assert_eq!(guide.bits(), 0b0100_0001_0001);   // root, 3, b7
    /// assert_eq!(ChordQuality::Diminished7.guide_tones().len(), 4);
    /// ```
    #[must_use]
    pub const fn guide_tones(self) -> PitchSet {
        let set = self.interval_set();
        let mut guide = PitchSet::EMPTY.insert(Pitch::C);

        let third = if set.contains(Pitch::new(4)) {
            4
        } else if set.contains(Pitch::new(3)) {
            3
        } else if set.contains(Pitch::new(5)) {
            5
        } else if set.contains(Pitch::new(2)) {
            2
        } else {
            0
        };
        guide = guide.insert(Pitch::new(third));

        let seventh = set.contains(Pitch::new(10)) || set.contains(Pitch::new(11));
        if set.contains(Pitch::new(10)) {
            guide = guide.insert(Pitch::new(10));
        }
        if set.contains(Pitch::new(11)) {
            guide = guide.insert(Pitch::new(11));
        }
        if !seventh && set.contains(Pitch::new(9)) {
            guide = guide.insert(Pitch::new(9));
        }

        if !set.contains(Pitch::new(7)) {
            if set.contains(Pitch::new(6)) {
                guide = guide.insert(Pitch::new(6));
            }
            if set.contains(Pitch::new(8)) {
                guide = guide.insert(Pitch::new(8));
            }
        }
        guide
    }
}

/// What a note of a scale does against a chord on the scale's root.
///
/// ```
/// use music_core::{ChordQuality, ChordScale, Pitch, Scale, ToneRole};
///
/// let ionian = ChordScale::from_parts(Scale::MAJOR, ChordQuality::Major7).unwrap();
/// assert_eq!(ionian.role(Pitch::E), ToneRole::Guide);     // the third
/// assert_eq!(ionian.role(Pitch::G), ToneRole::Colour);    // the fifth
/// assert_eq!(ionian.role(Pitch::D), ToneRole::Tension);   // the ninth
/// assert_eq!(ionian.role(Pitch::F), ToneRole::Avoid);     // a b9 above E
/// assert_eq!(ionian.role(Pitch::F_SHARP), ToneRole::Absent);
/// ```
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Debug)]
#[repr(u8)]
pub enum ToneRole {
    /// A chord tone that defines the quality: the root, third, seventh or
    /// sixth, or an altered fifth. See [`ChordQuality::guide_tones`].
    Guide,
    /// Any other chord tone: a perfect fifth, or an extension the chord
    /// spells out.
    Colour,
    /// A scale tone outside the chord that sits comfortably over it.
    Tension,
    /// A scale tone outside the chord a semitone above a chord tone, a minor
    /// ninth against it. Held over the chord, it clashes.
    Avoid,
    /// Not in the scale.
    Absent,
}

impl ToneRole {
    /// A one-letter code for compact display: `G`, `c`, `t`, `x`, or `.` for
    /// a note the scale does not have.
    #[inline]
    #[must_use]
    pub const fn code(self) -> char {
        match self {
            ToneRole::Guide => 'G',
            ToneRole::Colour => 'c',
            ToneRole::Tension => 't',
            ToneRole::Avoid => 'x',
            ToneRole::Absent => '.',
        }
    }
}

/// A chord built on a scale's root, every note of it inside the scale.
///
/// This is chord–scale theory's pairing: which scale to play over a chord,
/// and what each of its notes does there. The **avoid notes** follow the rule
/// jazz pedagogy teaches: a scale tone a semitone above a chord tone. Over a
/// major 7, Ionian's fourth is one, a minor ninth above the third; Lydian
/// raises it and has none.
///
/// ```
/// use music_core::{ChordQuality, ChordScale, Scale};
///
/// let dorian = Scale::MAJOR.mode(1);
/// let phrygian = Scale::MAJOR.mode(2);
///
/// let smooth = ChordScale::from_parts(dorian, ChordQuality::Minor7).unwrap();
/// let rough = ChordScale::from_parts(phrygian, ChordQuality::Minor7).unwrap();
///
/// assert_eq!(smooth.avoid_notes().len(), 0);
/// assert_eq!(rough.avoid_notes().len(), 2);   // b2 and b6
///
/// // A major 7 does not fit in Dorian at all.
/// assert_eq!(ChordScale::from_parts(dorian, ChordQuality::Major7), None);
/// ```
#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug)]
pub struct ChordScale {
    scale: Scale,
    quality: ChordQuality,
}

impl ChordScale {
    /// The chord `quality` on the root of `scale`, or `None` when one of the
    /// chord's notes is not in the scale.
    #[inline]
    #[must_use]
    pub const fn from_parts(scale: Scale, quality: ChordQuality) -> Option<Self> {
        if quality.interval_set().is_subset(scale.pitch_set()) {
            Some(Self { scale, quality })
        } else {
            None
        }
    }

    /// The scale.
    #[inline]
    #[must_use]
    pub const fn scale(self) -> Scale {
        self.scale
    }

    /// The chord's quality.
    #[inline]
    #[must_use]
    pub const fn quality(self) -> ChordQuality {
        self.quality
    }

    /// The chord's guide tones, as intervals above the root.
    #[inline]
    #[must_use]
    pub const fn guide_tones(self) -> PitchSet {
        self.quality.guide_tones()
    }

    /// The chord's other tones.
    #[inline]
    #[must_use]
    pub const fn colour_tones(self) -> PitchSet {
        self.quality
            .interval_set()
            .difference(self.quality.guide_tones())
    }

    /// The scale tones outside the chord a semitone above a chord tone.
    #[must_use]
    pub const fn avoid_notes(self) -> PitchSet {
        let chord = self.quality.interval_set();
        let outside = self.outside();
        // Every chord tone moved up a semitone, kept where the scale has a
        // note the chord does not.
        outside.intersection(chord.transpose(crate::pitch::Interval::new(1)))
    }

    /// The scale tones outside the chord that are not avoid notes.
    #[inline]
    #[must_use]
    pub const fn tensions(self) -> PitchSet {
        self.outside().difference(self.avoid_notes())
    }

    /// The scale tones outside the chord.
    #[inline]
    #[must_use]
    pub const fn outside(self) -> PitchSet {
        self.scale
            .pitch_set()
            .difference(self.quality.interval_set())
    }

    /// What the note `interval` semitones above the root does here. Only the
    /// pitch class of `interval` counts, so pass it as a [`Pitch`] above C.
    #[must_use]
    pub const fn role(self, interval: Pitch) -> ToneRole {
        if self.guide_tones().contains(interval) {
            ToneRole::Guide
        } else if self.quality.interval_set().contains(interval) {
            ToneRole::Colour
        } else if self.avoid_notes().contains(interval) {
            ToneRole::Avoid
        } else if self.scale.pitch_set().contains(interval) {
            ToneRole::Tension
        } else {
            ToneRole::Absent
        }
    }

    /// The share of the scale tones outside the chord that are avoid notes,
    /// 0.0 to 1.0, and 0.0 when the scale adds nothing to the chord.
    ///
    /// Over a minor 7: Dorian 0, Aeolian a third, Phrygian two thirds.
    #[must_use]
    pub fn clash(self) -> f32 {
        let outside = self.outside().len();
        if outside == 0 {
            0.0
        } else {
            self.avoid_notes().len() as f32 / outside as f32
        }
    }
}
