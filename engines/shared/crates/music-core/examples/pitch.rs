// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! A tour of `Pitch`, written to be read rather than to be clever.
//!
//! Run it with:
//!
//! ```sh
//! cargo run -p music-core --example pitch
//! ```
//!
//! A `Pitch` is a note name with the octave thrown away. Every C is the same C.
//! There are exactly twelve of them and each fits in one byte.
//!
//! For pitches that know which octave they are in, see the `note` example.

use music_core::{Interval, IntervalClass, Pitch, PitchSet, Spelling};

fn main() {
    the_twelve_pitches();
    making_a_pitch();
    sharps_and_flats();
    printing_a_pitch();
    moving_a_pitch();
    wrapping_around();
    measuring_the_gap();
    the_interval_type();
    the_interval_class_type();
    mirroring_a_pitch();
    comparing_pitches();
    a_set_of_pitches();
}

fn the_twelve_pitches() {
    println!();
    println!("THE TWELVE PITCHES");
    println!("------------------");
    println!();
    println!("Each pitch has a number from 0 to 11, counting up from C.");
    println!();

    println!("  C  is {}", Pitch::C.value());
    println!("  C# is {}", Pitch::C_SHARP.value());
    println!("  D  is {}", Pitch::D.value());
    println!("  D# is {}", Pitch::D_SHARP.value());
    println!("  E  is {}", Pitch::E.value());
    println!("  F  is {}", Pitch::F.value());
    println!("  F# is {}", Pitch::F_SHARP.value());
    println!("  G  is {}", Pitch::G.value());
    println!("  G# is {}", Pitch::G_SHARP.value());
    println!("  A  is {}", Pitch::A.value());
    println!("  A# is {}", Pitch::A_SHARP.value());
    println!("  B  is {}", Pitch::B.value());
    println!();
    println!(
        "There is no thirteenth. A pitch takes up {} byte of memory.",
        size_of::<Pitch>()
    );
}

fn making_a_pitch() {
    println!();
    println!("MAKING A PITCH");
    println!("--------------");
    println!();
    println!("The usual way is to name one directly.");
    println!();

    let c = Pitch::C;
    let f_sharp = Pitch::F_SHARP;

    println!("  Pitch::C       gives {c}");
    println!("  Pitch::F_SHARP gives {f_sharp}");
    println!();
    println!("You can also build one from a number. This never fails, because");
    println!("every number means some pitch: 12 wraps around to C again.");
    println!();

    println!("  Pitch::new(0)  gives {}", Pitch::new(0));
    println!("  Pitch::new(7)  gives {}", Pitch::new(7));
    println!("  Pitch::new(12) gives {}", Pitch::new(12));
    println!("  Pitch::new(13) gives {}", Pitch::new(13));
    println!("  Pitch::new(-1) gives {}", Pitch::new(-1));
    println!();
    println!("Or read one from text, which is what you want for user input.");
    println!();

    let parsed_c: Pitch = "C".parse().unwrap();
    let parsed_e_flat: Pitch = "Eb".parse().unwrap();
    let parsed_double: Pitch = "F##".parse().unwrap();

    println!("  \"C\"   reads as {parsed_c}");
    println!("  \"Eb\"  reads as {parsed_e_flat}");
    println!("  \"F##\" reads as {parsed_double}");
    println!();
    println!("Nonsense is refused rather than guessed at.");
    println!();
    println!("  \"H\" reads as {:?}", "H".parse::<Pitch>());
    println!("  \"\"  reads as {:?}", "".parse::<Pitch>());
}

fn sharps_and_flats() {
    println!();
    println!("SHARPS AND FLATS ARE THE SAME NOTE");
    println!("----------------------------------");
    println!();
    println!("On a piano, C sharp and D flat are one key. This library agrees:");
    println!("they are one value with two names, not two values.");
    println!();

    let c_sharp = Pitch::C_SHARP;
    let d_flat = Pitch::D_FLAT;

    println!("  Is C# the same as Db? {}", c_sharp == d_flat);
    println!(
        "  Is D# the same as Eb? {}",
        Pitch::D_SHARP == Pitch::E_FLAT
    );
    println!(
        "  Is F# the same as Gb? {}",
        Pitch::F_SHARP == Pitch::G_FLAT
    );
    println!(
        "  Is G# the same as Ab? {}",
        Pitch::G_SHARP == Pitch::A_FLAT
    );
    println!(
        "  Is A# the same as Bb? {}",
        Pitch::A_SHARP == Pitch::B_FLAT
    );
    println!();
    println!("The name is chosen when you print, not when you store.");
    println!();

    let e_flat = Pitch::E_FLAT;

    println!("  Called with sharps: {}", e_flat.name(Spelling::Sharps));
    println!("  Called with flats:  {}", e_flat.name(Spelling::Flats));
    println!();
    println!("Reading text works the same way. Both spellings give one value.");
    println!();

    let from_sharp: Pitch = "C#".parse().unwrap();
    let from_flat: Pitch = "Db".parse().unwrap();

    println!(
        "  Is \"C#\" the same as \"Db\"? {}",
        from_sharp == from_flat
    );
}

fn printing_a_pitch() {
    println!();
    println!("PRINTING A PITCH");
    println!("----------------");
    println!();
    println!("Printing always uses sharps.");
    println!();
    println!("  {}", Pitch::A_FLAT);
    println!();
    println!("If you want flats, ask for them by name.");
    println!();
    println!("  {}", Pitch::A_FLAT.name(Spelling::Flats));
    println!();
    println!("You can line pitches up in columns, because the usual width and");
    println!("alignment controls work.");
    println!();
    println!("  |{:>5}|", Pitch::C);
    println!("  |{:>5}|", Pitch::C_SHARP);
    println!("  |{:<5}|", Pitch::C);
    println!("  |{:^5}|", Pitch::C);
}

fn moving_a_pitch() {
    println!();
    println!("MOVING A PITCH");
    println!("--------------");
    println!();
    println!("To move a pitch you add an interval, which is a named distance.");
    println!();

    let c = Pitch::C;

    println!("  C plus a minor second  is {}", c + Interval::MINOR_SECOND);
    println!("  C plus a major second  is {}", c + Interval::MAJOR_SECOND);
    println!("  C plus a minor third   is {}", c + Interval::MINOR_THIRD);
    println!("  C plus a major third   is {}", c + Interval::MAJOR_THIRD);
    println!(
        "  C plus a perfect fifth is {}",
        c + Interval::PERFECT_FIFTH
    );
    println!();
    println!("Subtracting moves the other way.");
    println!();

    let g = Pitch::G;

    println!(
        "  G minus a perfect fifth is {}",
        g - Interval::PERFECT_FIFTH
    );
    println!("  G minus a major third   is {}", g - Interval::MAJOR_THIRD);
    println!();
    println!("You cannot write `Pitch::C + 1`, and that is on purpose. A plain");
    println!("number is ambiguous: one step could mean one semitone or one note");
    println!("of a scale. `Interval::MINOR_SECOND` cannot be misread.");
    println!();
    println!("An octave changes nothing here, because the octave is exactly what");
    println!("a pitch throws away.");
    println!();
    println!("  C plus an octave is {}", c + Interval::OCTAVE);
    println!();
    println!("Stacking a major third and a perfect fifth on C builds a C major");
    println!("chord.");
    println!();

    let third = c + Interval::MAJOR_THIRD;
    let fifth = c + Interval::PERFECT_FIFTH;

    println!("  {c}, {third} and {fifth}");
}

fn wrapping_around() {
    println!();
    println!("THERE IS NO EDGE TO FALL OFF");
    println!("----------------------------");
    println!();
    println!("Pitches wrap around like the hours on a clock. Past B you are back");
    println!("at C, and below C you are back at B. Any distance works, in either");
    println!("direction, however large.");
    println!();

    let c = Pitch::C;

    println!("  C moved up 1 semitone     is {}", c + Interval::new(1));
    println!("  C moved up 12 semitones   is {}", c + Interval::new(12));
    println!("  C moved up 13 semitones   is {}", c + Interval::new(13));
    println!("  C moved down 1 semitone   is {}", c + Interval::new(-1));
    println!("  C moved up 1000 semitones is {}", c + Interval::new(1000));
    println!();
    println!("The test suite checks this for all 65,536 possible distances. Not");
    println!("one of them escapes the twelve.");
}

fn measuring_the_gap() {
    println!();
    println!("TWO WAYS TO MEASURE A GAP");
    println!("-------------------------");
    println!();
    println!("There are two different questions you can ask about two pitches,");
    println!("and they have different answers.");
    println!();
    println!("The first is \"how far up?\". Going up from C to G is not the same");
    println!("as going up from G to C, so this one is not symmetric.");
    println!();

    let c = Pitch::C;
    let g = Pitch::G;

    println!(
        "  Up from C to G: {} semitones",
        c.interval_to(g).semitones()
    );
    println!(
        "  Up from G to C: {} semitones",
        g.interval_to(c).semitones()
    );
    println!();
    println!("The second is \"how far must a singer move?\". Direction does not");
    println!("matter, so this one is symmetric and never exceeds 6.");
    println!();
    println!("  Between C and G: {} semitones", c.distance_to(g).value());
    println!("  Between G and C: {} semitones", g.distance_to(c).value());
    println!();
    println!("Subtraction gives the same answer with a direction attached. Down");
    println!("is negative, up is positive.");
    println!();
    println!("  G minus C is {} semitones", (g - c).semitones());
    println!("  C minus G is {} semitones", (c - g).semitones());
    println!();
    println!("Those two are opposites, as a minus sign should be. The one");
    println!("exception is the tritone, the exact half-octave, where both");
    println!("directions are equally short and both come out positive.");
    println!();

    let f_sharp = Pitch::F_SHARP;

    println!("  F# minus C is {} semitones", (f_sharp - c).semitones());
    println!("  C minus F# is {} semitones", (c - f_sharp).semitones());
}

fn the_interval_type() {
    println!();
    println!("THE INTERVAL TYPE");
    println!("-----------------");
    println!();
    println!("An interval is a number of semitones with a direction. Unlike a");
    println!("pitch it does not wrap, because it has to be able to say \"up two");
    println!("octaves\" as well as \"down one semitone\".");
    println!();

    let fifth = Interval::PERFECT_FIFTH;
    let third = Interval::MAJOR_THIRD;

    println!("  A perfect fifth is {} semitones", fifth.semitones());
    println!("  A major third is   {} semitones", third.semitones());
    println!();
    println!("Intervals add and subtract with each other.");
    println!();
    println!(
        "  A fifth plus a third is {} semitones",
        (fifth + third).semitones()
    );
    println!(
        "  A fifth minus a third is {} semitones",
        (fifth - third).semitones()
    );
    println!(
        "  A fifth turned around is {} semitones",
        (-fifth).semitones()
    );
    println!();
    println!("They can be far larger than an octave.");
    println!();

    let two_octaves_and_a_fifth = Interval::new(31);

    println!(
        "  {} semitones is {} octaves plus {} semitones",
        two_octaves_and_a_fifth.semitones(),
        two_octaves_and_a_fifth.semitones() / 12,
        two_octaves_and_a_fifth.semitones() % 12
    );
}

fn the_interval_class_type() {
    println!();
    println!("THE INTERVAL CLASS TYPE");
    println!("-----------------------");
    println!();
    println!("An interval class is an interval with its direction and its");
    println!("octaves thrown away, leaving a number from 0 to 6. It answers");
    println!("\"how far must a voice move\", which is the question that matters");
    println!("when you care about smooth music.");
    println!();

    println!("  7 semitones up folds to   {}", IntervalClass::new(7));
    println!("  5 semitones down folds to {}", IntervalClass::new(-5));
    println!("  An octave folds to        {}", IntervalClass::new(12));
    println!("  A tritone folds to        {}", IntervalClass::new(6));
    println!();
    println!("A fifth up and a fourth down land on the same note, so they fold");
    println!("to the same class. Notice that the two lines above agree.");
}

fn mirroring_a_pitch() {
    println!();
    println!("MIRRORING A PITCH");
    println!("-----------------");
    println!();
    println!("Inverting reflects a pitch, as though the twelve were laid out in");
    println!("a circle and you flipped it over. Reflecting around C:");
    println!();

    println!("  C becomes {}", Pitch::C.invert(0));
    println!("  D becomes {}", Pitch::D.invert(0));
    println!("  E becomes {}", Pitch::E.invert(0));
    println!("  F becomes {}", Pitch::F.invert(0));
    println!("  G becomes {}", Pitch::G.invert(0));
    println!();
    println!("Do it twice around the same point and you are back where you");
    println!("started.");
    println!();

    let e = Pitch::E;
    let mirrored = e.invert(0);
    let back_again = mirrored.invert(0);

    println!("  E mirrors to {mirrored}, which mirrors back to {back_again}");
    println!();
    println!("Mirror a C major chord around C and you get an F minor chord.");
    println!();

    println!("  C becomes {}", Pitch::C.invert(0));
    println!("  E becomes {}", Pitch::E.invert(0));
    println!("  G becomes {}", Pitch::G.invert(0));
}

fn comparing_pitches() {
    println!();
    println!("COMPARING PITCHES");
    println!("-----------------");
    println!();
    println!("Two pitches are equal if they are the same note, whatever you");
    println!("call them.");
    println!();

    let one_c = Pitch::C;
    let another_c = Pitch::new(12);
    let d = Pitch::D;

    println!("  Is C equal to C?   {}", one_c == another_c);
    println!("  Is C equal to D?   {}", one_c == d);
    println!("  Is C# equal to Db? {}", Pitch::C_SHARP == Pitch::D_FLAT);
    println!();
    println!("Pitches can also be put in order, using their numbers.");
    println!();

    println!("  Is C less than D? {}", Pitch::C < Pitch::D);
    println!("  Is B more than C? {}", Pitch::B > Pitch::C);
    println!();
    println!("Read that second line carefully. It compares the numbers 11 and 0,");
    println!("not how high the notes sound. A pitch has no height to compare,");
    println!("because it has no octave. A B really can sound below a C.");
    println!();
    println!("If you want to know which note sounds higher, use a Note, which");
    println!("knows its octave. The `note` example shows that.");
    println!();
    println!("The ordering is still useful for picking one of two.");
    println!();
    println!(
        "  The lower-numbered of G and D is {}",
        Pitch::G.min(Pitch::D)
    );
    println!(
        "  The higher-numbered of G and D is {}",
        Pitch::G.max(Pitch::D)
    );
}

fn a_set_of_pitches() {
    println!();
    println!("A SET OF PITCHES");
    println!("----------------");
    println!();
    println!("A PitchSet holds any group of pitches. It has no order and no");
    println!("repeats, like a handful of piano keys held down at once.");
    println!();

    let c_major = PitchSet::EMPTY
        .insert(Pitch::C)
        .insert(Pitch::E)
        .insert(Pitch::G);

    let a_minor = PitchSet::EMPTY
        .insert(Pitch::A)
        .insert(Pitch::C)
        .insert(Pitch::E);

    println!("  A C major chord is {c_major}");
    println!("  An A minor chord is {a_minor}");
    println!();
    println!("You can ask what two chords share, or combine them.");
    println!();
    println!("  Notes in both:        {}", c_major & a_minor);
    println!("  Notes in either:      {}", c_major | a_minor);
    println!("  Only in the C major:  {}", c_major - a_minor);
    println!("  In neither:           {}", !(c_major | a_minor));
    println!();
    println!(
        "  How many notes do they share? {}",
        (c_major & a_minor).len()
    );
    println!(
        "  Does C major contain G?       {}",
        c_major.contains(Pitch::G)
    );
    println!(
        "  Does C major contain F?       {}",
        c_major.contains(Pitch::F)
    );
    println!();
    println!("The whole set is stored as twelve bits, one per pitch, so it takes");
    println!(
        "only {} bytes and these questions are almost free to ask.",
        size_of::<PitchSet>()
    );
    println!();
}
