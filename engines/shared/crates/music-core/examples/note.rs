// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! A tour of `Note`, written to be read rather than to be clever.
//!
//! Run it with:
//!
//! ```sh
//! cargo run -p music-core --example note
//! ```
//!
//! A `Note` is a `Pitch` that knows which octave it is in. A pitch is just "C";
//! a note is "the C in the middle of the piano".
//!
//! For the octave-free level, see the `pitch` example.

use music_core::{Interval, Note, Notes, Pitch};

fn main() {
    what_a_note_is();
    making_a_note();
    reading_a_note();
    octave_numbers();
    printing_a_note();
    moving_a_note();
    the_octave_matters_here();
    the_gap_between_two_notes();
    comparing_notes();
    how_high_does_it_sound();
    frequencies();
    a_handful_of_notes();
}

fn what_a_note_is() {
    println!();
    println!("WHAT A NOTE IS");
    println!("--------------");
    println!();
    println!("A pitch is a note name with no octave: just \"C\". There are twelve.");
    println!();
    println!("A note is a pitch that knows its octave: \"C in octave 4\", the C in");
    println!("the middle of a piano. Give a pitch an octave and you have a note.");
    println!();

    let pitch = Pitch::C;
    let note = pitch.at(4);

    println!("  The pitch is {pitch}");
    println!("  The note is  {note}");
    println!();
    println!("Going the other way throws the octave away again.");
    println!();
    println!("  The note {note} has pitch {}", note.pitch());
}

fn making_a_note() {
    println!();
    println!("MAKING A NOTE");
    println!("-------------");
    println!();
    println!("The clearest way is to take a pitch and give it an octave.");
    println!();

    let middle_c = Pitch::C.at(4);
    let concert_a = Pitch::A.at(4);
    let low_e = Pitch::E.at(2);

    println!("  Pitch::C.at(4) gives {middle_c}");
    println!("  Pitch::A.at(4) gives {concert_a}");
    println!("  Pitch::E.at(2) gives {low_e}");
    println!();
    println!("You can also build one from a MIDI number, which is how computers");
    println!("usually name notes. Middle C is 60.");
    println!();

    println!("  Note::from_midi(60) gives {}", Note::from_midi(60));
    println!("  Note::from_midi(69) gives {}", Note::from_midi(69));
    println!("  Note::from_midi(0)  gives {}", Note::from_midi(0));
    println!();
    println!("Or read one from text. Unlike a pitch, the octave is required.");
    println!();

    let parsed: Note = "C4".parse().unwrap();
    let parsed_flat: Note = "Eb3".parse().unwrap();

    println!("  \"C4\"  reads as {parsed}");
    println!("  \"Eb3\" reads as {parsed_flat}");
    println!();
    println!("Text without an octave is a pitch, not a note, so it is refused.");
    println!();
    println!("  \"C\" reads as {:?}", "C".parse::<Note>());
}

fn reading_a_note() {
    println!();
    println!("READING A NOTE");
    println!("--------------");
    println!();
    println!("A note answers three questions.");
    println!();

    let note = Pitch::E_FLAT.at(3);

    println!("  The note is        {note}");
    println!("  Its pitch is       {}", note.pitch());
    println!("  Its octave is      {}", note.octave());
    println!("  Its MIDI number is {}", note.midi());
}

fn octave_numbers() {
    println!();
    println!("HOW OCTAVES ARE NUMBERED");
    println!("------------------------");
    println!();
    println!("Middle C is called C4. Each C above adds one to the octave, and");
    println!("each C below subtracts one. The lowest C in MIDI is C-1.");
    println!();

    println!("  {} is MIDI {}", Pitch::C.at(-1), Pitch::C.at(-1).midi());
    println!("  {}  is MIDI {}", Pitch::C.at(1), Pitch::C.at(1).midi());
    println!("  {}  is MIDI {}", Pitch::C.at(2), Pitch::C.at(2).midi());
    println!("  {}  is MIDI {}", Pitch::C.at(3), Pitch::C.at(3).midi());
    println!(
        "  {}  is MIDI {}  <- middle C",
        Pitch::C.at(4),
        Pitch::C.at(4).midi()
    );
    println!("  {}  is MIDI {}", Pitch::C.at(5), Pitch::C.at(5).midi());
    println!("  {}  is MIDI {}", Pitch::C.at(6), Pitch::C.at(6).midi());
    println!();
    println!("Every octave adds 12, because there are twelve pitches in one.");
}

fn printing_a_note() {
    println!();
    println!("PRINTING A NOTE");
    println!("---------------");
    println!();
    println!("A note prints as its pitch followed by its octave.");
    println!();

    println!("  {}", Pitch::C.at(4));
    println!("  {}", Pitch::E_FLAT.at(3));
    println!("  {}", Pitch::B.at(-1));
    println!();
    println!("Printing uses sharps, so E flat shows as D sharp.");
    println!();
    println!("Notes line up in columns too.");
    println!();
    println!("  |{:>6}|", Pitch::C.at(4));
    println!("  |{:>6}|", Pitch::C_SHARP.at(10));
    println!("  |{:<6}|", Pitch::C.at(4));
}

fn moving_a_note() {
    println!();
    println!("MOVING A NOTE");
    println!("-------------");
    println!();
    println!("Adding an interval moves a note, exactly as it moves a pitch.");
    println!();

    let middle_c = Pitch::C.at(4);

    println!(
        "  C4 plus a major third   is {}",
        middle_c + Interval::MAJOR_THIRD
    );
    println!(
        "  C4 plus a perfect fifth is {}",
        middle_c + Interval::PERFECT_FIFTH
    );
    println!(
        "  C4 minus a major third  is {}",
        middle_c - Interval::MAJOR_THIRD
    );
    println!();
    println!("Walking up a C major scale, one step at a time:");
    println!();

    println!("  {}", middle_c);
    println!("  {}", middle_c + Interval::new(2));
    println!("  {}", middle_c + Interval::new(4));
    println!("  {}", middle_c + Interval::new(5));
    println!("  {}", middle_c + Interval::new(7));
    println!("  {}", middle_c + Interval::new(9));
    println!("  {}", middle_c + Interval::new(11));
    println!("  {}", middle_c + Interval::new(12));
    println!();
    println!("Notice the last one is C again, one octave higher.");
}

fn the_octave_matters_here() {
    println!();
    println!("THE OCTAVE ACTUALLY MATTERS HERE");
    println!("--------------------------------");
    println!();
    println!("This is the one big difference between a pitch and a note.");
    println!();
    println!("Adding an octave to a pitch changes nothing, because a pitch has");
    println!("no octave to change.");
    println!();

    let c_pitch = Pitch::C;

    println!(
        "  The pitch C plus an octave is still {}",
        c_pitch + Interval::OCTAVE
    );
    println!();
    println!("Adding an octave to a note moves it somewhere new.");
    println!();

    let c_note = Pitch::C.at(4);
    let higher = c_note + Interval::OCTAVE;

    println!("  The note {c_note} plus an octave is {higher}");
    println!("  MIDI {} becomes MIDI {}", c_note.midi(), higher.midi());
    println!();
    println!("A pitch wraps around like a clock. A note keeps climbing.");
    println!();
    println!(
        "  Pitch C plus 24 semitones: {}",
        c_pitch + Interval::new(24)
    );
    println!(
        "  Note C4 plus 24 semitones: {}",
        c_note + Interval::new(24)
    );
}

fn the_gap_between_two_notes() {
    println!();
    println!("THE GAP BETWEEN TWO NOTES");
    println!("-------------------------");
    println!();
    println!("Subtracting two notes tells you the distance between them. Unlike");
    println!("the pitch version this can be bigger than an octave, and it is");
    println!("negative when you go down.");
    println!();

    let c4 = Pitch::C.at(4);
    let g4 = Pitch::G.at(4);
    let c6 = Pitch::C.at(6);

    println!(
        "  From {c4} up to {g4} is {} semitones",
        (g4 - c4).semitones()
    );
    println!(
        "  From {g4} down to {c4} is {} semitones",
        (c4 - g4).semitones()
    );
    println!(
        "  From {c4} up to {c6} is {} semitones",
        (c6 - c4).semitones()
    );
    println!();
    println!("Those first two are exact opposites, which is what a minus sign");
    println!("should do.");
}

fn comparing_notes() {
    println!();
    println!("COMPARING NOTES");
    println!("---------------");
    println!();
    println!("Two notes are equal only if the pitch AND the octave match.");
    println!();

    let middle_c = Pitch::C.at(4);
    let same_note = Note::from_midi(60);
    let octave_up = Pitch::C.at(5);

    println!("  Is C4 equal to C4? {}", middle_c == same_note);
    println!("  Is C4 equal to C5? {}", middle_c == octave_up);
    println!();
    println!("Sharps and flats still tie, because the pitch inside them ties.");
    println!();
    println!(
        "  Is D#4 equal to Eb4? {}",
        Pitch::D_SHARP.at(4) == Pitch::E_FLAT.at(4)
    );
}

fn how_high_does_it_sound() {
    println!();
    println!("WHICH NOTE SOUNDS HIGHER");
    println!("------------------------");
    println!();
    println!("This is where notes are more useful than pitches. A note knows how");
    println!("high it sounds, so comparing them means what you expect.");
    println!();

    let b3 = Pitch::B.at(3);
    let c4 = Pitch::C.at(4);

    println!("  Does {b3} sound lower than {c4}? {}", b3 < c4);
    println!();
    println!("Compare that with the pitches alone, where the answer flips:");
    println!();
    println!(
        "  Is the pitch B less than the pitch C? {}",
        Pitch::B < Pitch::C
    );
    println!();
    println!("Both answers are right. The pitches compare their numbers, 11 and");
    println!("0. The notes compare how they sound. When you mean sound, use a");
    println!("note.");
    println!();
    println!("Picking the lower or higher of two notes works as you would hope.");
    println!();

    let g4 = Pitch::G.at(4);
    let c5 = Pitch::C.at(5);

    println!("  The lower of {g4} and {c5} is {}", g4.min(c5));
    println!("  The higher of {g4} and {c5} is {}", g4.max(c5));
}

#[cfg(feature = "std")]
fn frequencies() {
    println!();
    println!("HOW MANY TIMES A SECOND IT VIBRATES");
    println!("-----------------------------------");
    println!();
    println!("Only a note has a frequency, because only a note has an octave. A");
    println!("pitch has no such method, and that is the point of the two types.");
    println!();

    let concert_a = Pitch::A.at(4);

    println!(
        "  {} vibrates {} times a second",
        concert_a,
        concert_a.frequency_hz()
    );
    println!();
    println!("Going up an octave doubles it. Going down halves it.");
    println!();

    println!(
        "  {} is {:.2} Hz",
        Pitch::A.at(2),
        Pitch::A.at(2).frequency_hz()
    );
    println!(
        "  {} is {:.2} Hz",
        Pitch::A.at(3),
        Pitch::A.at(3).frequency_hz()
    );
    println!(
        "  {} is {:.2} Hz",
        Pitch::A.at(4),
        Pitch::A.at(4).frequency_hz()
    );
    println!(
        "  {} is {:.2} Hz",
        Pitch::A.at(5),
        Pitch::A.at(5).frequency_hz()
    );
    println!(
        "  {} is {:.2} Hz",
        Pitch::A.at(6),
        Pitch::A.at(6).frequency_hz()
    );
    println!();
    println!("Middle C is not a round number.");
    println!();
    println!(
        "  {} is {:.4} Hz",
        Pitch::C.at(4),
        Pitch::C.at(4).frequency_hz()
    );
    println!();
    println!("Orchestras have not always tuned A to 440. You can ask for another");
    println!("reference.");
    println!();
    println!(
        "  Tuned to 440: {} is {:.2} Hz",
        Pitch::C.at(4),
        Pitch::C.at(4).frequency_hz_at(440.0)
    );
    println!(
        "  Tuned to 432: {} is {:.2} Hz",
        Pitch::C.at(4),
        Pitch::C.at(4).frequency_hz_at(432.0)
    );
    println!(
        "  Tuned to 415: {} is {:.2} Hz",
        Pitch::C.at(4),
        Pitch::C.at(4).frequency_hz_at(415.0)
    );
}

#[cfg(not(feature = "std"))]
fn frequencies() {
    println!();
    println!("(Frequency needs the `std` feature, which is off in this build.)");
}

fn a_handful_of_notes() {
    println!();
    println!("A HANDFUL OF NOTES");
    println!("------------------");
    println!();
    println!("`Notes` holds several notes in the order you add them, and lets");
    println!("you repeat one. That is how a real chord is played: the same note");
    println!("can appear twice, in different octaves.");
    println!();

    let mut chord = Notes::EMPTY;
    let _ = chord.push(Pitch::C.at(3));
    let _ = chord.push(Pitch::G.at(3));
    let _ = chord.push(Pitch::E.at(4));
    let _ = chord.push(Pitch::C.at(5));

    println!("  The chord is {chord}");
    println!("  It has {} notes", chord.len());
    println!();
    println!("Ask which is lowest, or sort them from low to high.");
    println!();
    println!("  The lowest note is {}", chord.bass().unwrap());
    println!("  Sorted low to high: {}", chord.sorted());
    println!();
    println!("Move the whole chord at once.");
    println!();
    println!(
        "  Up a perfect fifth: {}",
        chord.transpose(Interval::PERFECT_FIFTH)
    );
    println!(
        "  Down an octave:     {}",
        chord.transpose(-Interval::OCTAVE)
    );
    println!();
    println!("Throw the octaves away and you are left with the pitches. The C");
    println!("appears twice above but only once here.");
    println!();
    println!("  The pitches are {}", chord.pitch_set());
    println!();
}
