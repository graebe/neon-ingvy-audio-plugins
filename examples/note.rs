//! Everything `Note` can do: a pitch that knows its register.
//!
//! Run it with:
//!
//! ```sh
//! cargo run -p music-core --example note
//! ```
//!
//! A `Note` is a `Pitch` given an octave. That is the definition rather than an
//! analogy: `Note` is a type alias for `Voiced<Pitch>`, the same generic that
//! produces `VoicedTriad` as `Voiced<Triad>`. Giving something a register is one
//! act that applies at both levels.
//!
//! For the octave-free level, see the `pitch` example.

use std::collections::{HashMap, HashSet};

use music_core::{Harmony, Interval, Note, Notes, Pitch};

fn heading(title: &str) {
    println!("\n\x1b[1m{title}\x1b[0m");
    println!("{}", "-".repeat(title.len()));
}

fn main() {
    building_one();
    reading_one();
    the_relationship_to_pitch();
    printing();
    arithmetic();
    the_octave_is_real();
    comparing();
    sorting_and_collections();
    the_midi_range();
    the_range_limit();
    #[cfg(feature = "std")]
    frequencies();
    as_a_voicing();
    lists_of_notes();
}

/// Four ways in.
fn building_one() {
    heading("Building a note");

    println!("  From a pitch plus an octave, which reads best:");
    println!("    Pitch::C.at(4)   = {}", Pitch::C.at(4));
    println!("    Pitch::A.at(4)   = {}", Pitch::A.at(4));
    println!("    Pitch::E_FLAT.at(3) = {}", Pitch::E_FLAT.at(3));

    println!("\n  From a MIDI number, where 60 is middle C:");
    for midi in [0i16, 60, 69, 127] {
        println!("    Note::from_midi({midi:>3}) = {}", Note::from_midi(midi));
    }

    println!("\n  From its parts, for const contexts:");
    const MIDDLE_C: Note = Note::from_parts(Pitch::C, 4);
    println!("    Note::from_parts(Pitch::C, 4) = {MIDDLE_C}");

    println!("\n  From text. Unlike a pitch, the octave is required:");
    for text in ["C4", "Eb3", "A4", "C-1", "G9"] {
        let note: Note = text.parse().unwrap();
        println!("    {text:<5} -> {note:<5} (midi {})", note.midi());
    }

    println!("\n  Text without an octave is a pitch, not a note, and is rejected:");
    for text in ["C", "C4x", "", "4"] {
        println!(
            "    {:<6} -> {:?}",
            format!("{text:?}"),
            text.parse::<Note>()
        );
    }
}

/// Three accessors, no surprises.
fn reading_one() {
    heading("Reading a note");

    let note = Pitch::E_FLAT.at(3);
    println!("    note      = {note}");
    println!("    .pitch()  = {}", note.pitch());
    println!("    .octave() = {}", note.octave());
    println!("    .midi()   = {}", note.midi());

    println!("\n  Octaves are scientific pitch notation, so middle C is C4 and");
    println!("  the bottom of MIDI is C in octave -1:\n");
    for octave in -1..=9 {
        let c = Pitch::C.at(octave);
        println!("    {:<6} midi {:>4}", c.to_string(), c.midi());
    }
}

/// Projecting down and lifting back up.
fn the_relationship_to_pitch() {
    heading("How a note relates to its pitch");

    println!("  Lifting adds a register, projecting discards it:\n");
    println!("    Pitch::C.at(4)          = {}   (lift)", Pitch::C.at(4));
    println!(
        "    Pitch::C.at(4).pitch()  = {}    (project)",
        Pitch::C.at(4).pitch()
    );

    println!("\n  Every C has the same pitch but they are different notes:");
    let cs = [Pitch::C.at(3), Pitch::C.at(4), Pitch::C.at(5)];
    println!(
        "    {} {} {}  all have pitch {}",
        cs[0],
        cs[1],
        cs[2],
        cs[0].pitch()
    );
    println!("    C3 == C4  ->  {}", cs[0] == cs[1]);

    println!("\n  Enharmonics still collapse, because the pitch inside them does:");
    println!(
        "    D#4 == Eb4  ->  {}",
        Pitch::D_SHARP.at(4) == Pitch::E_FLAT.at(4)
    );
}

/// Display honours the usual format flags.
fn printing() {
    heading("Printing");

    println!("    Display: {}", Pitch::E_FLAT.at(3));
    println!("    Debug:   {:?}", Pitch::E_FLAT.at(3));

    println!("\n  Width, fill and alignment work, so tables line up:\n");
    for note in [Pitch::C.at(4), Pitch::C_SHARP.at(10), Pitch::B.at(-1)] {
        println!("    |{note:>7}|{note:<7}|{note:^7}|");
    }
}

/// Adding intervals, and subtracting notes.
fn arithmetic() {
    heading("Arithmetic");

    let middle_c = Pitch::C.at(4);

    println!("  Adding and subtracting an interval moves the note in register:\n");
    println!("    C4 + a fifth  = {}", middle_c + Interval::PERFECT_FIFTH);
    println!("    C4 - a fifth  = {}", middle_c - Interval::PERFECT_FIFTH);
    println!("    C4 + an octave = {}", middle_c + Interval::OCTAVE);
    println!("    C4 - an octave = {}", middle_c - Interval::OCTAVE);

    println!("\n  Walking a C major scale from middle C:\n");
    print!("    ");
    for step in [0, 2, 4, 5, 7, 9, 11, 12] {
        print!("{} ", middle_c + Interval::new(step));
    }
    println!();

    println!("\n  Subtracting two notes gives the interval between them. Unlike");
    println!("  the pitch level this is signed and can exceed an octave:\n");
    for (a, b) in [
        (Pitch::C.at(4), Pitch::G.at(4)),
        (Pitch::G.at(4), Pitch::C.at(4)),
        (Pitch::C.at(4), Pitch::C.at(6)),
        (Pitch::C.at(6), Pitch::C.at(4)),
    ] {
        println!("    {b} - {a} = {:>4} semitones", (b - a).semitones());
    }

    println!("\n  Note that `a - b` is exactly `-(b - a)` here, which is not true");
    println!("  of the pitch-level `interval_to`, since that one always reports");
    println!("  the ascending distance.");
    println!(
        "    C.interval_to(G) = {}, G.interval_to(C) = {}",
        Pitch::C.interval_to(Pitch::G),
        Pitch::G.interval_to(Pitch::C)
    );
}

/// The one behaviour that most separates a note from a pitch.
fn the_octave_is_real() {
    heading("The octave is real here");

    println!("  At the pitch level an octave is a no-op, because an octave is");
    println!("  exactly what a pitch throws away. At the note level it moves:\n");

    println!(
        "    pitch:  C + an octave = {}   (unchanged)",
        Pitch::C.transpose(Interval::OCTAVE)
    );
    println!(
        "    note:   C4 + an octave = {}  (midi {} -> {})",
        Pitch::C.at(4) + Interval::OCTAVE,
        Pitch::C.at(4).midi(),
        (Pitch::C.at(4) + Interval::OCTAVE).midi()
    );

    println!("\n  Same for large intervals. The pitch wraps, the note climbs:\n");
    for semitones in [12i16, 24, 36, 120] {
        let interval = Interval::new(semitones);
        println!(
            "    {semitones:>4} semitones: pitch {} , note {}",
            Pitch::C.transpose(interval),
            Pitch::C.at(4) + interval
        );
    }
}

/// Ordering follows what you hear.
fn comparing() {
    heading("Comparing");

    println!("  Equality needs both the pitch and the octave to match:\n");
    println!("    C4 == C4  ->  {}", Pitch::C.at(4) == Pitch::C.at(4));
    println!("    C4 == C5  ->  {}", Pitch::C.at(4) == Pitch::C.at(5));
    println!(
        "    D#4 == Eb4 ->  {}   (enharmonics tie)",
        Pitch::D_SHARP.at(4) == Pitch::E_FLAT.at(4)
    );

    println!("\n  Ordering follows sounding pitch height, unlike the pitch level");
    println!("  which orders by class number:\n");
    println!("    C4 <  G4  ->  {}", Pitch::C.at(4) < Pitch::G.at(4));
    println!("    B3 <  C4  ->  {}", Pitch::B.at(3) < Pitch::C.at(4));
    println!("    but as pitches, B > C  ->  {}", Pitch::B > Pitch::C);

    println!("\n  That is the useful difference: a note knows how high it sounds,");
    println!("  a pitch has no height to know.\n");
    println!(
        "    C4.cmp(&G4) = {:?}",
        Pitch::C.at(4).cmp(&Pitch::G.at(4))
    );
    println!("    min(G4, C5) = {}", Pitch::G.at(4).min(Pitch::C.at(5)));
    println!("    max(G4, C5) = {}", Pitch::G.at(4).max(Pitch::C.at(5)));

    println!("\n  Equality and ordering agree, because a MIDI number determines");
    println!("  the pitch and the octave uniquely:");
    let mismatches = (0i16..2000)
        .filter(|m| {
            let a = Note::from_midi(*m);
            let b = Note::from_midi(*m);
            (a == b) != (a.cmp(&b) == std::cmp::Ordering::Equal)
        })
        .count();
    println!("    disagreements found: {mismatches}");
}

/// Sorting and using notes as keys.
fn sorting_and_collections() {
    heading("Sorting and collections");

    let mut notes = [
        Pitch::G.at(4),
        Pitch::C.at(4),
        Pitch::E.at(3),
        Pitch::C.at(5),
        Pitch::A.at(4),
    ];
    let before: Vec<String> = notes.iter().map(|n| n.to_string()).collect();
    notes.sort();
    let after: Vec<String> = notes.iter().map(|n| n.to_string()).collect();
    println!("    before: {}", before.join(" "));
    println!("    sorted: {}", after.join(" "));
    println!(
        "    lowest {}, highest {}",
        notes[0],
        notes[notes.len() - 1]
    );

    println!("\n  Note is Hash and Eq, so it keys a map directly:");
    let mut velocity: HashMap<Note, u8> = HashMap::new();
    velocity.insert(Pitch::C.at(4), 100);
    velocity.insert(Pitch::E.at(4), 80);
    let mut keys: Vec<_> = velocity.keys().copied().collect();
    keys.sort();
    for key in keys {
        println!("    {key:<5} velocity {}", velocity[&key]);
    }

    println!("\n  Enharmonics collapse in a set, octaves do not:");
    let set: HashSet<Note> = [
        Pitch::D_SHARP.at(4),
        Pitch::E_FLAT.at(4),
        Pitch::E_FLAT.at(5),
    ]
    .into_iter()
    .collect();
    println!("    {{D#4, Eb4, Eb5}} has {} distinct members", set.len());
}

/// Where the standard MIDI numbers land.
fn the_midi_range() {
    heading("The MIDI range");

    for (midi, label) in [
        (0i16, "bottom of MIDI"),
        (21, "lowest key on a piano"),
        (60, "middle C"),
        (69, "concert A, 440 Hz"),
        (108, "top key on a piano"),
        (127, "top of MIDI"),
    ] {
        println!(
            "    midi {midi:>3} = {:<5} {label}",
            Note::from_midi(midi).to_string()
        );
    }

    println!("\n  `midi()` is not clamped, so a note outside the range still");
    println!("  answers. Validate before writing a MIDI file:\n");
    for note in [Pitch::C.at(4), Pitch::C.at(-5), Pitch::C.at(40)] {
        let midi = note.midi();
        println!(
            "    {:<7} midi {:>6}  in range? {}",
            note.to_string(),
            midi,
            (0..=127).contains(&midi)
        );
    }
}

/// The range, which is now total.
fn the_range_limit() {
    heading("The range");

    println!("  The octave is a signed 16-bit value, so every MIDI number a");
    println!("  signed 16-bit integer can hold round-trips exactly. There is no");
    println!("  boundary to remember and nothing truncates.\n");

    for midi in [i16::MIN, -1000, 0, 60, 127, 5000, i16::MAX] {
        let back = Note::from_midi(midi).midi();
        let verdict = if back == midi {
            "round-trips"
        } else {
            "BROKEN"
        };
        println!(
            "    midi {midi:>6} -> {:<9} -> midi {back:>6}   {verdict}",
            Note::from_midi(midi).to_string()
        );
    }

    let broken = (i16::MIN..=i16::MAX)
        .filter(|m| Note::from_midi(*m).midi() != *m)
        .count();
    println!("\n  Checked across all 65,536 values. Failures: {broken}.");

    println!("\n  An earlier version stored the octave in a single byte and");
    println!("  corrupted silently outside roughly -1524 to 1547, which is why");
    println!(
        "  it was widened. The cost was {} bytes per note instead of 2.",
        size_of::<Note>()
    );
}

/// Hertz, the one place a register becomes a physical quantity.
///
/// Behind the `std` feature, because computing it needs floating-point maths
/// that `core` does not provide.
#[cfg(feature = "std")]
fn frequencies() {
    heading("Frequency");

    println!("  Only a note has a frequency, because only a note has a register.");
    println!("  A pitch class has no such method, and that is the point.\n");

    for note in [
        Pitch::A.at(4),
        Pitch::C.at(4),
        Pitch::A.at(3),
        Pitch::A.at(5),
        Pitch::C.at(-1),
    ] {
        println!(
            "    {:<6} midi {:>4}  {:>12.4} Hz",
            note.to_string(),
            note.midi(),
            note.frequency_hz()
        );
    }

    println!("\n  Every octave doubles the frequency:");
    for octave in 0..=8 {
        let a = Pitch::A.at(octave);
        println!("    {:<5} {:>10.3} Hz", a.to_string(), a.frequency_hz());
    }

    println!("\n  Other tuning references, for anyone not at 440:");
    for reference in [440.0, 432.0, 415.0] {
        println!(
            "    A4 at {reference:>5.0} -> middle C is {:>8.3} Hz",
            Pitch::C.at(4).frequency_hz_at(reference)
        );
    }
}

/// A note is a voicing, so the generic methods apply.
fn as_a_voicing() {
    heading("A note is a voicing of one voice");

    let note = Pitch::G.at(4);

    println!("  `Note` is `Voiced<Pitch>`, so the generic voicing methods work:\n");
    println!("    .harmony() = {}   (the abstract half)", note.harmony());
    println!(
        "    .octaves() = {:?}  (one register per voice)",
        note.octaves()
    );
    println!("    .voices()  = {}", note.voices());
    println!("    .bass()    = {}", note.bass());
    println!("    .note_at(0) = {}", note.note_at(0));
    println!("    .midi_at(0) = {}", note.midi_at(0));
    println!("    .pitch_set() = {}", note.pitch_set());

    println!("\n  `displacement` measures total movement between two voicings:");
    for target in [Pitch::G.at(4), Pitch::A_FLAT.at(4), Pitch::G.at(5)] {
        println!(
            "    {} to {:<5} = {} semitones",
            note,
            target.to_string(),
            note.displacement(target)
        );
    }

    println!("\n  The same method on a VoicedTriad solves a three-voice");
    println!("  assignment. One idea, two arities.");
}

/// The free-form list, for when structure is not wanted.
fn lists_of_notes() {
    heading("Notes: a free-form list");

    println!(
        "  `Notes` holds up to {} notes in whatever order you build",
        Notes::CAPACITY
    );
    println!("  them, with doublings allowed. It makes no claim about what the");
    println!("  notes spell, so it carries no harmony and supports no");
    println!("  transformations. It is the bag you hand to a synthesizer.\n");

    let spread = Notes::from_slice(&[
        Pitch::C.at(3),
        Pitch::G.at(3),
        Pitch::E.at(4),
        Pitch::C.at(5),
    ])
    .unwrap();

    println!("    notes:       {spread}");
    println!("    len:         {}", spread.len());
    println!("    bass:        {:?}", spread.bass());
    println!(
        "    pitch set:   {}   (doubling collapses)",
        spread.pitch_set()
    );
    println!("    sorted:      {}", spread.sorted());
    println!(
        "    up a fifth:  {}",
        spread.transpose(Interval::PERFECT_FIFTH)
    );

    println!("\n  Unlike a Voiced, this keeps the order you gave it, so its");
    println!("  `displacement` pairs voice by voice rather than searching:");
    let moved = spread.transpose(Interval::MINOR_SECOND);
    println!(
        "    displacement to the same list up a semitone: {:?}",
        spread.displacement(&moved)
    );

    println!("\n  Capacity is fixed, so pushing past it reports failure rather");
    println!("  than allocating:");
    let mut full = Notes::EMPTY;
    let mut accepted = 0;
    for _ in 0..20 {
        if full.push(Pitch::C.at(4)) {
            accepted += 1;
        }
    }
    println!("    pushed 20, accepted {accepted}, len {}", full.len());
    println!();
}
