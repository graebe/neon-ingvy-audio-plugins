//! Everything `Pitch` can do, and the `Interval` and `IntervalClass` types it
//! works with.
//!
//! Run it with:
//!
//! ```sh
//! cargo run -p music-core --example pitch
//! ```
//!
//! A `Pitch` is a note name with the octave thrown away. Every C is the same C.
//! There are exactly twelve, they fit in one byte, and all of neo-Riemannian
//! theory happens at this level. For pitches that know their register, see the
//! `note` example.

use std::collections::{HashMap, HashSet};

use music_core::{EDO, Interval, IntervalClass, Pitch, PitchSet, Spelling};

fn heading(title: &str) {
    println!("\n\x1b[1m{title}\x1b[0m");
    println!("{}", "-".repeat(title.len()));
}

fn main() {
    the_twelve();
    building_one();
    enharmonics();
    printing();
    transposing();
    wrapping();
    two_kinds_of_distance();
    the_interval_type();
    the_interval_class_type();
    inverting();
    comparing();
    sorting_and_collections();
    collecting_into_a_set();
}

/// There are twelve, numbered upward from C, and no thirteenth.
fn the_twelve() {
    heading("The twelve pitches");

    println!("  EDO = {EDO}, so pitch classes run 0 to {}.\n", EDO - 1);

    print!("  name  ");
    for pitch in Pitch::ALL {
        print!("{pitch:>4}");
    }
    println!();
    print!("  value ");
    for pitch in Pitch::ALL {
        print!("{:>4}", pitch.value());
    }
    println!();

    println!("\n  A pitch is {} byte.", size_of::<Pitch>());
    println!("  Pitch::ALL has {} entries.", Pitch::ALL.len());
}

/// Four ways in, three of them infallible.
fn building_one() {
    heading("Building a pitch");

    println!("  From a constant, which is what code should normally use:");
    println!("    Pitch::C       = {}", Pitch::C);
    println!("    Pitch::F_SHARP = {}", Pitch::F_SHARP);
    println!("    Pitch::B_FLAT  = {}", Pitch::B_FLAT);

    println!("\n  From any integer, wrapping. There is no failure case here,");
    println!("  because there is no invalid input: a pitch class IS an integer");
    println!("  modulo twelve, so twelve simply is C.");
    for raw in [0i32, 7, 11, 12, 13, -1, -13, 200, 1_000_000] {
        println!("    Pitch::new({raw:>8}) = {}", Pitch::new(raw));
    }

    println!("\n  If you are validating a byte that claims to encode a pitch,");
    println!("  range-check it yourself. That is a question about your input,");
    println!("  not about which note the number denotes:");
    for raw in [7u8, 12, 200] {
        let checked = (raw < 12).then(|| Pitch::new(i32::from(raw)));
        println!("    byte {raw:>3} -> {checked:?}");
    }

    println!("\n  From text, for user input. Accidentals stack:");
    for text in ["C", "C#", "Db", "F##", "Ebb", "B#", "Cb"] {
        let pitch: Pitch = text.parse().unwrap();
        println!("    {text:<6} -> {pitch:<4} (value {})", pitch.value());
    }

    println!("\n  Bad input is rejected rather than guessed at:");
    for text in ["H", "", "C4", "#C", "x"] {
        println!(
            "    {:<6} -> {:?}",
            format!("{text:?}"),
            text.parse::<Pitch>()
        );
    }
}

/// C sharp and D flat are one value wearing two names.
fn enharmonics() {
    heading("Enharmonics");

    println!("  Equal temperament is an assumption of the theory, so the crate");
    println!("  makes no enharmonic distinction. These are the same value:\n");

    let pairs = [
        (Pitch::C_SHARP, Pitch::D_FLAT),
        (Pitch::D_SHARP, Pitch::E_FLAT),
        (Pitch::F_SHARP, Pitch::G_FLAT),
        (Pitch::G_SHARP, Pitch::A_FLAT),
        (Pitch::A_SHARP, Pitch::B_FLAT),
    ];
    for (sharp, flat) in pairs {
        println!(
            "    {:<4} == {:<4}  ->  {}   (both are value {})",
            sharp.name(Spelling::Sharps),
            flat.name(Spelling::Flats),
            sharp == flat,
            sharp.value()
        );
    }

    println!("\n  Parsing agrees, so text input normalises automatically:");
    let sharp: Pitch = "C#".parse().unwrap();
    let flat: Pitch = "Db".parse().unwrap();
    println!(
        "    \"C#\".parse() == \"Db\".parse()  ->  {}",
        sharp == flat
    );

    println!("\n  Spelling is a naming choice, applied when you print:\n");
    println!("    {:<8}{:<8}flats", "value", "sharps");
    for pitch in Pitch::ALL {
        let s = pitch.name(Spelling::Sharps);
        let f = pitch.name(Spelling::Flats);
        let mark = if s == f { "" } else { "  <- differs" };
        println!("    {:<8}{:<8}{}{}", pitch.value(), s, f, mark);
    }
}

/// Display honours the usual format flags.
fn printing() {
    heading("Printing");

    println!("  Display uses sharps. Debug prints the same text, so a failing");
    println!("  assertion reads as music rather than as struct fields:\n");
    println!("    Display: {}", Pitch::E_FLAT);
    println!("    Debug:   {:?}", Pitch::E_FLAT);

    println!("\n  Width, fill and alignment all work, so tables line up:\n");
    println!("    |{:>6}|{:<6}|{:^6}|", Pitch::C, Pitch::C, Pitch::C);
    println!(
        "    |{:>6}|{:<6}|{:^6}|",
        Pitch::C_SHARP,
        Pitch::C_SHARP,
        Pitch::C_SHARP
    );
    println!("    |{:*>6}|{:*<6}|{:*^6}|", Pitch::B, Pitch::B, Pitch::B);

    println!("\n  For flats, name the pitch explicitly:");
    println!("    {:>6}  (Display)", Pitch::A_FLAT);
    println!(
        "    {:>6}  (name with flats)",
        Pitch::A_FLAT.name(Spelling::Flats)
    );
}

/// Moving a pitch by an interval.
fn transposing() {
    heading("Transposing");

    let named = [
        ("minor second", Interval::MINOR_SECOND),
        ("major second", Interval::MAJOR_SECOND),
        ("minor third", Interval::MINOR_THIRD),
        ("major third", Interval::MAJOR_THIRD),
        ("perfect fourth", Interval::PERFECT_FOURTH),
        ("tritone", Interval::TRITONE),
        ("perfect fifth", Interval::PERFECT_FIFTH),
        ("minor sixth", Interval::MINOR_SIXTH),
        ("major sixth", Interval::MAJOR_SIXTH),
        ("minor seventh", Interval::MINOR_SEVENTH),
        ("major seventh", Interval::MAJOR_SEVENTH),
        ("octave", Interval::OCTAVE),
    ];

    println!("  Every named interval, starting from C:\n");
    for (name, interval) in named {
        println!(
            "    C {:>3} ({:<15}) = {}",
            interval,
            name,
            Pitch::C.transpose(interval)
        );
    }

    println!("\n  The `+` and `-` operators do the same thing:");
    println!(
        "    Pitch::G + PERFECT_FIFTH = {}",
        Pitch::G + Interval::PERFECT_FIFTH
    );
    println!(
        "    Pitch::G - PERFECT_FIFTH = {}",
        Pitch::G - Interval::PERFECT_FIFTH
    );

    println!("\n  The right-hand side must be an Interval, never a bare number.");
    println!("  `Pitch::C + 1` does not compile, on purpose: a pitch is a point");
    println!("  and an interval is a displacement, the same way an Instant takes");
    println!("  a Duration rather than an integer.");

    println!("\n  An octave is a no-op here, because an octave is exactly what a");
    println!("  pitch discards:");
    println!(
        "    C + an octave  = {}",
        Pitch::C.transpose(Interval::OCTAVE)
    );
    println!(
        "    C + 10 octaves = {}",
        Pitch::C.transpose(Interval::new(120))
    );

    println!("\n  Stacking thirds builds a triad:");
    let root = Pitch::C;
    println!(
        "    {} + {} + {}",
        root,
        root.transpose(Interval::MAJOR_THIRD),
        root.transpose(Interval::PERFECT_FIFTH)
    );
}

/// A pitch has no boundary to overflow.
fn wrapping() {
    heading("Wrapping: there is no edge to fall off");

    println!("  A pitch is modular by definition, so any interval wraps into");
    println!("  0 through 11, in either direction and at any size:\n");

    for semitones in [13i16, 24, 1000, -1, -13, -1000, i16::MAX, i16::MIN] {
        let moved = Pitch::C.transpose(Interval::new(semitones));
        println!(
            "    C transposed by {semitones:>7} = {moved:<4} (value {})",
            moved.value()
        );
    }

    println!(
        "\n  Checked exhaustively: all {} interval values keep the",
        1u32 << 16
    );
    let escaped = (i16::MIN..=i16::MAX)
        .filter(|s| Pitch::C.transpose(Interval::new(*s)).value() >= 12)
        .count();
    println!("  result inside the octave. Escapes found: {escaped}.");

    println!("\n  Contrast with a Note, which has a real register and therefore a");
    println!("  real range. See the `note` example.");
}

/// Two different questions, two different answers.
fn two_kinds_of_distance() {
    heading("Two kinds of distance");

    println!("  `interval_to` is directed. It asks \"how far up?\", so it is");
    println!("  asymmetric and always lands in 0 through 11:\n");
    println!("    C to G: {}", Pitch::C.interval_to(Pitch::G));
    println!("    G to C: {}", Pitch::G.interval_to(Pitch::C));
    println!("    C to C: {}", Pitch::C.interval_to(Pitch::C));

    println!("\n  `distance_to` is undirected. It asks \"how far must a voice");
    println!("  move?\", so it is symmetric and folds at the tritone:\n");
    println!("    C to G: {}", Pitch::C.distance_to(Pitch::G));
    println!("    G to C: {}", Pitch::G.distance_to(Pitch::C));

    println!("\n  The second is the one neo-Riemannian theory cares about, since");
    println!("  it measures voice-leading effort. It never exceeds 6:\n");
    print!("    from C   ");
    for pitch in Pitch::ALL {
        print!("{pitch:>4}");
    }
    println!();
    print!("    interval ");
    for pitch in Pitch::ALL {
        print!("{:>4}", Pitch::C.interval_to(pitch).semitones());
    }
    println!();
    print!("    distance ");
    for pitch in Pitch::ALL {
        print!("{:>4}", Pitch::C.distance_to(pitch).value());
    }
    println!();
}

/// Intervals are unbounded and signed.
fn the_interval_type() {
    heading("The Interval type");

    println!("  An Interval is a signed semitone count. Unlike a pitch it is");
    println!("  never wrapped and never clamped, because it has to express both");
    println!("  \"down a semitone\" and \"up two octaves and a fifth\":\n");

    for semitones in [0i16, 1, -1, 12, -12, 31, 1000] {
        let interval = Interval::new(semitones);
        println!(
            "    Interval::new({semitones:>5}) -> display {:<7} semitones {:>6} abs {:>6} class {}",
            interval.to_string(),
            interval.semitones(),
            interval.abs(),
            interval.class()
        );
    }

    println!("\n  Intervals add, subtract and negate among themselves:");
    let fifth = Interval::PERFECT_FIFTH;
    let third = Interval::MAJOR_THIRD;
    println!("    fifth + third = {}", fifth + third);
    println!("    fifth - third = {}", fifth - third);
    println!("    -fifth        = {}", -fifth);

    println!("\n  Intervals compare, so you can sort or take a minimum:");
    let mut spread = [Interval::OCTAVE, -Interval::MINOR_SECOND, Interval::TRITONE];
    spread.sort();
    let shown: Vec<String> = spread.iter().map(|i| i.to_string()).collect();
    println!("    sorted: {}", shown.join(" "));
}

/// Folding an interval into 0 through 6.
fn the_interval_class_type() {
    heading("The IntervalClass type");

    println!("  An interval class is an interval with its direction and its");
    println!("  octaves thrown away. A fifth up and a fourth down are the same");
    println!("  interval class, which is why it measures voice movement:\n");

    for semitones in [0i32, 5, 7, 6, 10, -5, 31, -1000] {
        println!(
            "    from_semitones({semitones:>6}) = {}",
            IntervalClass::new(semitones)
        );
    }

    println!("\n  Like Pitch::new, this cannot fail. Anything folds:");
    for raw in [0i32, 6, 7, 13, -13] {
        println!(
            "    IntervalClass::new({raw:>4}) = {}",
            IntervalClass::new(raw)
        );
    }

    println!("\n  Folding is symmetric: an interval and its negation agree.");
    let disagreements = (1i16..=i16::MAX)
        .filter(|s| Interval::new(*s).class() != Interval::new(-*s).class())
        .count();
    println!("    disagreements across the whole range: {disagreements}");
}

/// Reflecting a pitch about an axis.
fn inverting() {
    heading("Inverting");

    println!("  Inversion reflects a pitch: x becomes axis - x. With transposition");
    println!("  it generates the group that the neo-Riemannian transformations");
    println!("  commute with.\n");

    for axis in [0i32, 3, 7] {
        print!("    axis {axis:>2}: ");
        for pitch in Pitch::ALL {
            print!("{:>4}", pitch.invert(axis));
        }
        println!();
    }
    print!("    original: ");
    for pitch in Pitch::ALL {
        print!("{pitch:>4}");
    }
    println!();

    println!("\n  Inverting twice about the same axis is the identity:");
    let pitch = Pitch::E;
    println!(
        "    {pitch} -> {} -> {}",
        pitch.invert(3),
        pitch.invert(3).invert(3)
    );

    println!("\n  A C major triad inverted about C becomes an F minor triad:");
    let inverted: Vec<String> = [Pitch::C, Pitch::E, Pitch::G]
        .iter()
        .map(|p| p.invert(0).to_string())
        .collect();
    println!("    [C, E, G] -> [{}]", inverted.join(", "));
}

/// Equality, ordering, and what the ordering actually means.
fn comparing() {
    heading("Comparing");

    println!("  Equality ignores spelling, because spelling is not part of the");
    println!("  value:\n");
    println!("    C# == Db  ->  {}", Pitch::C_SHARP == Pitch::D_FLAT);
    println!("    C  == D   ->  {}", Pitch::C == Pitch::D);
    println!("    C  != D   ->  {}", Pitch::C != Pitch::D);

    println!("\n  Pitches are totally ordered, by pitch-class number:\n");
    println!("    C  <  D   ->  {}", Pitch::C < Pitch::D);
    println!("    B  >  C   ->  {}", Pitch::B > Pitch::C);
    println!(
        "    D# <= Eb  ->  {}   (one value, two names)",
        Pitch::D_SHARP <= Pitch::E_FLAT
    );
    println!("    C.cmp(&G) ->  {:?}", Pitch::C.cmp(&Pitch::G));

    println!("\n  Read that ordering carefully. It compares class numbers, not");
    println!("  musical height, because a pitch has no height to compare. B > C");
    println!("  is true here, but a B can perfectly well sound below a C. If you");
    println!("  want height, use a Note.\n");
    println!("    Pitch::B > Pitch::C  ->  {}", Pitch::B > Pitch::C);
    println!(
        "    but B3 < C4          ->  {}",
        Pitch::B.at(3) < Pitch::C.at(4)
    );

    println!("\n  Subtracting two pitches gives the shortest signed path, which");
    println!("  is the signed companion of `distance_to`:\n");
    for (a, b) in [
        (Pitch::G, Pitch::C),
        (Pitch::C, Pitch::G),
        (Pitch::E, Pitch::C),
        (Pitch::F_SHARP, Pitch::C),
        (Pitch::C, Pitch::F_SHARP),
    ] {
        println!(
            "    {a} - {b} = {:>3}   (distance {}){}",
            (a - b).semitones(),
            b.distance_to(a).value(),
            if b.distance_to(a).value() == 6 {
                "  <- tritone, ties resolve upward"
            } else {
                ""
            }
        );
    }
    println!("\n  So `a - b == -(b - a)` everywhere except the tritone, where");
    println!("  both directions are equally short.");

    println!("\n  The ordering is still useful: it gives sets and maps a stable");
    println!("  key, and makes `min`, `max` and `sort` available.");
    println!("    min(G, D) = {}", Pitch::G.min(Pitch::D));
    println!("    max(G, D) = {}", Pitch::G.max(Pitch::D));
}

/// Sorting, deduplicating and using pitches as keys.
fn sorting_and_collections() {
    heading("Sorting and collections");

    let mut scattered = [Pitch::G, Pitch::C_SHARP, Pitch::B_FLAT, Pitch::C, Pitch::E];
    let before: Vec<String> = scattered.iter().map(|p| p.to_string()).collect();
    scattered.sort();
    let after: Vec<String> = scattered.iter().map(|p| p.to_string()).collect();
    println!("    before: {}", before.join(" "));
    println!("    sorted: {}", after.join(" "));

    println!("\n  Pitch is Hash and Eq, so it works as a key directly:");
    let mut fingering: HashMap<Pitch, &str> = HashMap::new();
    fingering.insert(Pitch::C, "thumb");
    fingering.insert(Pitch::E, "middle");
    fingering.insert(Pitch::G, "little");
    let mut keys: Vec<_> = fingering.keys().copied().collect();
    keys.sort();
    for key in keys {
        println!("    {key:<4} -> {}", fingering[&key]);
    }

    println!("\n  Enharmonics collapse in a set, since they are one value:");
    let set: HashSet<Pitch> = [Pitch::C_SHARP, Pitch::D_FLAT, Pitch::C]
        .into_iter()
        .collect();
    println!("    {{C#, Db, C}} has {} distinct members", set.len());
}

/// The natural collection of pitches is a bitset.
fn collecting_into_a_set() {
    heading("PitchSet: the natural collection");

    let major = PitchSet::from_pitches(&[Pitch::C, Pitch::E, Pitch::G]);
    let minor = PitchSet::from_pitches(&[Pitch::A, Pitch::C, Pitch::E]);

    println!("    C major triad: {major}");
    println!("    A minor triad: {minor}");
    println!("    shared:        {}", major & minor);
    println!("    combined:      {}", major | minor);
    println!("    only in major: {}", major - minor);
    println!("    everything else: {}", !(major | minor));

    println!("\n  It is a 12-bit mask, so set operations are single instructions:");
    println!("    bits of C major: {:012b}", major.bits());
    println!("    size in memory:  {} bytes", size_of::<PitchSet>());

    println!("\n  Transposing a set rotates the mask:");
    for n in [0i16, 1, 7] {
        println!(
            "    transposed by {n:>2}: {}",
            major.transpose(Interval::new(n))
        );
    }

    println!("\n  The interval-class vector fingerprints a chord's sound,");
    println!("  independent of its root. Both consonant triads share one:");
    println!("    C major: {:?}", major.interval_vector());
    println!("    A minor: {:?}", minor.interval_vector());
    println!();
}
