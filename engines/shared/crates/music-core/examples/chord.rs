// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! A tour of `Chord`, written to be read rather than to be clever.
//!
//! Run it with:
//!
//! ```sh
//! cargo run -p music-core --example chord
//! ```
//!
//! A chord is a root plus a set of pitches. It is not limited to a list of
//! known chord types, so it can hold two notes or nine, named or not.
//!
//! For the notes a chord is made of, see the `pitch` and `note` examples.

use music_core::{Chord, ChordQuality, Interval, Pitch, PitchSet, Triad, Voicing};

fn main() {
    what_a_chord_is();
    making_a_chord();
    the_root_is_part_of_the_chord();
    asking_what_a_chord_is();
    changing_a_chord();
    chords_with_no_name();
    reading_a_chord_from_text();
    naming_a_pile_of_notes();
    when_there_is_more_than_one_answer();
    symmetric_chords();
    triads_are_a_special_case();
    playing_a_chord();
    arranging_a_chord();
    turning_a_chord_over();
    finishing_a_chord_someone_started();
}

fn what_a_chord_is() {
    println!();
    println!("WHAT A CHORD IS");
    println!("---------------");
    println!();
    println!("A chord is two things together: a root, and a set of pitches.");
    println!();

    let c_major = Chord::major(Pitch::C);

    println!("  The chord is       {c_major}");
    println!("  Its root is        {}", c_major.root());
    println!("  Its pitches are    {}", c_major.pitches());
    println!("  It has this many notes: {}", c_major.size());
    println!();
    println!("The set has no order and no repeats, like a handful of piano keys");
    println!("held down at once. The root says which of them the chord is built");
    println!("on, which is not something the keys themselves can tell you.");
    println!();
    println!("A chord is not limited to three or four notes. It can hold as few");
    println!("as one and as many as all twelve.");
    println!();

    let big = Chord::maj13(Pitch::C);

    println!("  {big} has {} notes", big.size());
    println!(
        "  It takes up {} bytes of memory either way.",
        size_of::<Chord>()
    );
}

fn making_a_chord() {
    println!();
    println!("MAKING A CHORD");
    println!("--------------");
    println!();
    println!("The usual way is to name the kind of chord you want.");
    println!();

    println!("  Chord::major(Pitch::C)  gives {}", Chord::major(Pitch::C));
    println!("  Chord::minor(Pitch::A)  gives {}", Chord::minor(Pitch::A));
    println!("  Chord::dom7(Pitch::G)   gives {}", Chord::dom7(Pitch::G));
    println!("  Chord::maj7(Pitch::C)   gives {}", Chord::maj7(Pitch::C));
    println!("  Chord::min7(Pitch::D)   gives {}", Chord::min7(Pitch::D));
    println!("  Chord::dim7(Pitch::B)   gives {}", Chord::dim7(Pitch::B));
    println!("  Chord::sixth(Pitch::F)  gives {}", Chord::sixth(Pitch::F));
    println!("  Chord::dom9(Pitch::G)   gives {}", Chord::dom9(Pitch::G));
    println!("  Chord::min11(Pitch::D)  gives {}", Chord::min11(Pitch::D));
    println!("  Chord::maj13(Pitch::C)  gives {}", Chord::maj13(Pitch::C));
    println!("  Chord::fifth(Pitch::E)  gives {}", Chord::fifth(Pitch::E));
    println!();
    println!("Longer names read better with a name of their own:");
    println!();

    let vamp = Chord::dom7_sus4(Pitch::G);

    println!("  Chord::dom7_sus4(Pitch::G) gives {vamp}");
    println!();
    println!("There are thirty-one of these kinds in all, from a bare fifth up");
    println!("to thirteenths and altered chords.");
    println!();
    println!("You can also build a chord from a root and whatever pitches you");
    println!("like. This is what lets a chord be anything at all.");
    println!();

    let chosen = PitchSet::EMPTY
        .insert(Pitch::C)
        .insert(Pitch::E)
        .insert(Pitch::G);
    let built = Chord::new(Pitch::C, chosen);

    println!("  Pitches C, E and G, rooted on C, give {built}");
    println!();
    println!("If you leave the root out of the set, it is put back in, because a");
    println!("chord contains its own root by definition.");
    println!();

    let missing_root = PitchSet::EMPTY.insert(Pitch::E).insert(Pitch::G);
    let repaired = Chord::new(Pitch::C, missing_root);

    println!("  Pitches E and G, rooted on C, give {repaired}");
}

fn the_root_is_part_of_the_chord() {
    println!();
    println!("THE ROOT IS PART OF THE CHORD");
    println!("-----------------------------");
    println!();
    println!("This is the idea that makes everything else work. The same set of");
    println!("pitches can be two different chords, depending on which note you");
    println!("treat as the root.");
    println!();

    let shared = Chord::sixth(Pitch::C).pitches();
    let on_c = Chord::new(Pitch::C, shared);
    let on_a = Chord::new(Pitch::A, shared);

    println!("  The pitches are {shared}");
    println!();
    println!("  Rooted on C, that is {on_c}");
    println!("  Rooted on A, that is {on_a}");
    println!();
    println!("Same four notes, two different chords. A pianist playing them");
    println!("would call it one or the other depending on the music around it.");
    println!();
    println!("  Are the two chords equal? {}", on_c == on_a);
    println!(
        "  Are their pitches equal?  {}",
        on_c.pitches() == on_a.pitches()
    );
}

fn asking_what_a_chord_is() {
    println!();
    println!("ASKING WHAT A CHORD IS");
    println!("----------------------");
    println!();
    println!("Once the root is known, the kind of chord is not a matter of");
    println!("opinion. Ask and you get one answer.");
    println!();

    let dm7 = Chord::min7(Pitch::D);

    println!("  {dm7} is a {:?} chord", dm7.quality().unwrap());
    println!();
    println!("The same question on our two readings from before gives two");
    println!("different answers, which is exactly right.");
    println!();

    let shared = Chord::sixth(Pitch::C).pitches();

    println!(
        "  Rooted on C: {:?}",
        Chord::new(Pitch::C, shared).quality().unwrap()
    );
    println!(
        "  Rooted on A: {:?}",
        Chord::new(Pitch::A, shared).quality().unwrap()
    );
    println!();
    println!("What the chord looks for is the shape: the distances from the root");
    println!("up to each note, with the root itself counted as zero.");
    println!();
    println!(
        "  A major chord's shape is  {}",
        ChordQuality::Major.interval_set()
    );
    println!(
        "  A minor chord's shape is  {}",
        ChordQuality::Minor.interval_set()
    );
    println!(
        "  A dominant 7's shape is   {}",
        ChordQuality::Dominant7.interval_set()
    );
    println!();
    println!("Those read as semitones above the root, written as though the root");
    println!("were C. So a major chord is the root, four semitones up, and seven.");
}

fn changing_a_chord() {
    println!();
    println!("ADDING AND REMOVING NOTES");
    println!("-------------------------");
    println!();
    println!("Add a note and the chord may become a different kind of chord.");
    println!();

    let c = Chord::major(Pitch::C);
    let with_seventh = c.with(Pitch::B);
    let with_ninth = with_seventh.with(Pitch::D);

    println!("  Start with        {c}   ({:?})", c.quality().unwrap());
    println!(
        "  Add a B and it is {with_seventh}  ({:?})",
        with_seventh.quality().unwrap()
    );
    println!(
        "  Add a D and it is {with_ninth}  ({:?})",
        with_ninth.quality().unwrap()
    );
    println!();
    println!("Remove a note and it changes back.");
    println!();

    let back_to_seventh = with_ninth.without(Pitch::D);

    println!("  Take the D away and it is {back_to_seventh} again");
    println!();
    println!("The root is the one note you cannot remove, since a chord contains");
    println!("its root by definition. Asking is quietly ignored.");
    println!();

    let attempted = c.without(Pitch::C);

    println!("  Removing C from {c} leaves {attempted}");
    println!();
    println!("Moving the whole chord keeps its shape and changes its root.");
    println!();

    let up_a_fifth = c.transpose(Interval::PERFECT_FIFTH);

    println!("  {c} moved up a perfect fifth is {up_a_fifth}");
    println!("  Both are {:?} chords", up_a_fifth.quality().unwrap());
}

fn chords_with_no_name() {
    println!();
    println!("CHORDS WITH NO NAME");
    println!("-------------------");
    println!();
    println!("Most possible chords have no name, and that is fine. They are");
    println!("still chords and you can still use them.");
    println!();

    let odd = Chord::major(Pitch::C)
        .with(Pitch::C_SHARP)
        .with(Pitch::F_SHARP);

    println!("  The chord is       {odd}");
    println!("  Its pitches are    {}", odd.pitches());
    println!("  Its kind is        {:?}", odd.quality());
    println!();
    println!("When there is no name, printing falls back to the root followed by");
    println!("the distances up to each note. So the line above reads: a chord on");
    println!("C, with notes at 0, 1, 4, 6 and 7 semitones above it.");
    println!();
    println!("That form can be read back in, so nothing is lost by printing it.");
    println!();

    let text = odd.to_string();
    let parsed: Chord = text.parse().unwrap();

    println!("  Printed:   {text}");
    println!("  Read back: {parsed}");
    println!("  The same?  {}", parsed == odd);
}

fn reading_a_chord_from_text() {
    println!();
    println!("READING A CHORD FROM TEXT");
    println!("-------------------------");
    println!();
    println!("Chord symbols written the usual way can be read in.");
    println!();

    let cmaj7: Chord = "Cmaj7".parse().unwrap();
    let g7: Chord = "G7".parse().unwrap();
    let dm7b5: Chord = "Dm7b5".parse().unwrap();
    let f69: Chord = "F6/9".parse().unwrap();
    let eb13: Chord = "Eb13".parse().unwrap();

    println!("  \"Cmaj7\" is {cmaj7} with pitches {}", cmaj7.pitches());
    println!("  \"G7\"    is {g7} with pitches {}", g7.pitches());
    println!("  \"Dm7b5\" is {dm7b5} with pitches {}", dm7b5.pitches());
    println!("  \"F6/9\"  is {f69} with pitches {}", f69.pitches());
    println!("  \"Eb13\"  is {eb13} with pitches {}", eb13.pitches());
    println!();
    println!("Something that is not a chord symbol is refused.");
    println!();
    println!("  \"Cwobble\" reads as {:?}", "Cwobble".parse::<Chord>());
    println!();
    println!("In your own code, prefer the constructors. They cannot fail, so");
    println!("there is nothing to unwrap. Reading text is for input from people.");
}

fn naming_a_pile_of_notes() {
    println!();
    println!("NAMING A PILE OF NOTES");
    println!("----------------------");
    println!();
    println!("Suppose someone hands you a set of pitches with no root attached.");
    println!("Now the question is harder, because you have to work out which note");
    println!("the chord is built on.");
    println!();

    let mystery = PitchSet::EMPTY
        .insert(Pitch::G)
        .insert(Pitch::B)
        .insert(Pitch::D)
        .insert(Pitch::F);

    println!("  The pitches are {mystery}");
    println!("  The best answer is {}", mystery.identify().unwrap());
    println!();
    println!("That one has a single sensible reading, so there is nothing to");
    println!("choose between.");
    println!();
    println!("A set that spells nothing at all gets no answer, rather than a");
    println!("made-up one.");
    println!();

    let cluster = PitchSet::EMPTY
        .insert(Pitch::C)
        .insert(Pitch::C_SHARP)
        .insert(Pitch::D);

    println!("  The pitches are {cluster}");
    println!("  The best answer is {:?}", cluster.identify());
}

fn when_there_is_more_than_one_answer() {
    println!();
    println!("WHEN THERE IS MORE THAN ONE ANSWER");
    println!("----------------------------------");
    println!();
    println!("Often a set of pitches can honestly be read more than one way. You");
    println!("can ask for every reading rather than settling for one.");
    println!();

    let pitches = Chord::sixth(Pitch::C).pitches();
    let mut readings = pitches.interpretations();
    let first = readings.next().unwrap();
    let second = readings.next().unwrap();

    println!("  The pitches are {pitches}");
    println!();
    println!("  One reading is     {first}");
    println!("  Another reading is {second}");
    println!("  Any more?          {:?}", readings.next());
    println!();
    println!("Both are correct. Which one a musician would write down depends on");
    println!("the music around it, which the library cannot see.");
    println!();
    println!("If you just want one answer, ask for the best guess.");
    println!();
    println!("  The best guess is {}", pitches.identify().unwrap());
    println!();
    println!("That guess follows a fixed rule: simpler kinds of chord are");
    println!("preferred, and sevenths are counted simpler than sixths. It is a");
    println!("rule of thumb, not a fact about music. When the answer matters, ask");
    println!("for every reading and decide yourself.");
}

fn symmetric_chords() {
    println!();
    println!("CHORDS THAT SOUND THE SAME FROM EVERY NOTE");
    println!("------------------------------------------");
    println!();
    println!("A few chords divide the octave into equal steps. Those have no one");
    println!("root at all: every note in them works equally well.");
    println!();

    let diminished = Chord::dim7(Pitch::C).pitches();
    let mut dim_readings = diminished.interpretations();

    println!("  A diminished seventh has pitches {diminished}");
    println!("  Its steps are three semitones apart, all the way round.");
    println!();
    println!("  Reading one:   {}", dim_readings.next().unwrap());
    println!("  Reading two:   {}", dim_readings.next().unwrap());
    println!("  Reading three: {}", dim_readings.next().unwrap());
    println!("  Reading four:  {}", dim_readings.next().unwrap());
    println!("  Any more?      {:?}", dim_readings.next());
    println!();
    println!("The augmented triad does the same thing in three.");
    println!();

    let augmented = Chord::aug(Pitch::C).pitches();
    let mut aug_readings = augmented.interpretations();

    println!("  An augmented triad has pitches {augmented}");
    println!();
    println!("  Reading one:   {}", aug_readings.next().unwrap());
    println!("  Reading two:   {}", aug_readings.next().unwrap());
    println!("  Reading three: {}", aug_readings.next().unwrap());
    println!("  Any more?      {:?}", aug_readings.next());
    println!();
    println!("This is not a flaw in the library. It is a real property of those");
    println!("chords, and part of why composers reach for them.");
}

fn triads_are_a_special_case() {
    println!();
    println!("TRIADS ARE A SPECIAL CASE");
    println!("-------------------------");
    println!();
    println!("A `Triad` is the narrower type: major or minor only, never anything");
    println!("else. It exists because the neo-Riemannian transformations work on");
    println!("exactly those twenty-four chords and no others.");
    println!();

    let triad = Triad::major(Pitch::C);
    let as_chord = Chord::from(triad);

    println!("  The triad is        {triad}");
    println!("  As a chord it is    {as_chord}");
    println!("  With pitches        {}", as_chord.pitches());
    println!();
    println!("Going back the other way can fail, because most chords are not");
    println!("major or minor triads.");
    println!();

    let major = Chord::major(Pitch::C);
    let seventh = Chord::maj7(Pitch::C);

    println!("  {major} as a triad: {:?}", Triad::try_from(major));
    println!(
        "  {seventh} as a triad: is it possible? {}",
        Triad::try_from(seventh).is_ok()
    );
    println!();
    println!("So a triad is always a chord, but only some chords are triads.");
}

fn playing_a_chord() {
    println!();
    println!("PLAYING A CHORD");
    println!("---------------");
    println!();
    println!("A chord has no octave, the same way a pitch has none. To hear it");
    println!("you give it one, and get back real notes stacked upward.");
    println!();

    let c = Chord::maj7(Pitch::C);

    println!("  The chord is {c}");
    println!("  In octave 3: {}", c.voice(3));
    println!("  In octave 4: {}", c.voice(4));
    println!("  In octave 5: {}", c.voice(5));
    println!();
    println!("Bigger chords simply stack higher.");
    println!();

    println!(
        "  {} becomes {}",
        Chord::dom9(Pitch::G),
        Chord::dom9(Pitch::G).voice(3)
    );
    println!(
        "  {} becomes {}",
        Chord::maj13(Pitch::C),
        Chord::maj13(Pitch::C).voice(3)
    );
    println!();
}

fn arranging_a_chord() {
    println!();
    println!("ARRANGING A CHORD");
    println!("-----------------");
    println!();
    println!("Stacking upward from the root is only one way to play a chord.");
    println!("A ninth is really fourteen semitones up, not two, and a player");
    println!("might drop a voice an octave or leave the root to the bass.");
    println!();

    let chord = Chord::maj9(Pitch::C);

    let close = chord.voice_as(4, Voicing::Close);
    let stacked = chord.voice_as(4, Voicing::Stacked);
    let drop_two = chord.voice_as(4, Voicing::Drop2);
    let rootless = chord.voice_as(4, Voicing::Rootless);

    println!("  The chord is {chord}");
    println!();
    println!("  Close:    {close}");
    println!("  Stacked:  {stacked}");
    println!("  Drop 2:   {drop_two}");
    println!("  Rootless: {rootless}");
    println!();
    println!("Close is what `voice` does on its own, so those two agree.");
    println!("Only the rootless one changes which pitches sound at all.");
    println!();
    println!("The heights come from the quality, so a chord with no name has");
    println!("no stack to read and falls back to close position.");
    println!();

    let no_name = Chord::new(
        Pitch::C,
        PitchSet::from_pitches(&[Pitch::C, Pitch::C_SHARP, Pitch::E, Pitch::F_SHARP]),
    );

    let no_name_stacked = no_name.voice_as(4, Voicing::Stacked);
    let no_name_close = no_name.voice_as(4, Voicing::Close);

    println!("  The chord is {no_name}");
    println!("  Stacked:  {no_name_stacked}");
    println!("  Close:    {no_name_close}");
    println!();
}

fn turning_a_chord_over() {
    println!();
    println!("TURNING A CHORD OVER");
    println!("--------------------");
    println!();
    println!("Move the lowest note up an octave and the chord is inverted.");
    println!("Do it once per note and you arrive back where you started, an");
    println!("octave higher.");
    println!();

    let root_position = Chord::major(Pitch::C).voice(4);
    let first_inversion = root_position.rotate_up();
    let second_inversion = first_inversion.rotate_up();
    let back_to_the_start = second_inversion.rotate_up();

    println!("  Root position:    {root_position}");
    println!("  First inversion:  {first_inversion}");
    println!("  Second inversion: {second_inversion}");
    println!("  Back to start:    {back_to_the_start}");
    println!();
    println!("It is called `rotate_up` and not `invert` on purpose. In this");
    println!("crate `invert` already means mirroring pitch classes about an");
    println!("axis, which is a different idea entirely: it changes the notes.");
    println!("This only changes which octave they are in.");
    println!();
}

fn finishing_a_chord_someone_started() {
    println!();
    println!("FINISHING A CHORD SOMEONE STARTED");
    println!("---------------------------------");
    println!();
    println!("Players leave notes out. The root is the first to go, because");
    println!("the bass has it. A set that is missing a note is not the chord");
    println!("it happens to spell, so there are two questions to ask.");
    println!();

    let played = PitchSet::from_pitches(&[Pitch::E_FLAT, Pitch::G, Pitch::B_FLAT, Pitch::D]);

    println!("  Someone played {played}");
    println!();
    println!("Asked what it is, the answer is honest and probably not what");
    println!("the player meant:");
    println!();
    println!("  It is {}", played.identify().unwrap());
    println!();
    println!("Asked what it could be part of, the chord they meant shows up:");
    println!();

    let mut could_be = played.completions();

    println!("  It could be part of {}", could_be.next().unwrap());
    println!("  It could be part of {}", could_be.next().unwrap());
    println!("  It could be part of {}", could_be.next().unwrap());
    println!("  It could be part of {}", could_be.next().unwrap());
    println!("  It could be part of {}", could_be.next().unwrap());
    println!();
    println!("The first one has no C in it anywhere, and that is the point.");
    println!("A completion's root does not have to be a note you played.");
    println!();
}
