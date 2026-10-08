# music-core — rules for agents

General music theory primitives: pitch classes, intervals, notes, chords, keys
and pitch-class sets. Performance is why this is Rust: every type is `Copy`,
fixed-size, and allocation-free, so it runs on an audio thread.

## Where it lives, and who uses it

It came here from [neo-riemann](https://codeberg.org/graebe/neo-riemann) with
its history (`git subtree`), and is GPL-3.0-or-later like the rest of this
repository. It is a **shared crate**: it names no product.

| User | How |
|---|---|
| NI Chord-Detector (`engines/chord-detector`) | path dependency, `default-features = false` |
| neo-riemann (codeberg) | git dependency on this repository, pinned by `rev`; it adds the PLR group on top |

Anything that is not specifically neo-Riemannian belongs here — keys, scales
and modes included. A change here is a change to neo-riemann too, the next time
it moves its `rev`: keep the public API additive.

## Scope

In: pitch primitives, pitch sets, chords of any size with identification,
consonant triads, voicings, frequency, keys in the seven church modes and
roman-numeral degrees. Not yet: analysis passes, file or MIDI I/O, key
detection.

### The chord vocabulary

`Chord` stores a root and an arbitrary `PitchSet`, so it is never limited by the
name table. `ChordQuality` is what identification *returns*, not how a chord is
stored. The recognised qualities live in `ChordQuality::ALL`; the docs site
asserts that count rather than restating it, so adding one there is enough.

Two rules hold that table together, both tested:

- **No two qualities may share an interval set.** A duplicate would make
  `Chord::quality` pick arbitrarily by declaration order.
- **Collisions across roots are expected and correct.** A C6 and an A minor 7
  really are the same pitches. `PitchSet::interpretations` exists to surface
  that rather than hide it.
- **`stacking()` must reduce to `interval_set()`.** The canonical voicing is a
  re-registration of `intervals()`, so it may move a note above the octave but
  never add one, drop one, or change its pitch class. Same length, strictly
  ascending from 0. A new quality with an extension needs a row in both tables.

`PitchSet::completions` is the counterpart to `interpretations`: it asks what a
set could be *part of*, so a voicing missing its root still finds its name. It
scans all twelve roots against every quality, and its root need not be in the
set.

**A chord carries a bass as well as a root**, equal to the root until `over`
says otherwise. Three rules hold it together:

- **`over` is not a pure relabelling.** The bass sounds, so a bass from outside
  the chord joins the pitch set. `Am/F#` has four notes.
- **Only an inverted chord forces its bass when voiced.** A chord in root
  position lets the arrangement choose what sits lowest, which is the whole
  point of `Drop2` putting the fifth underneath. Forcing the root down there
  broke Drop2 once already.
- **`rebass` lowers the bass rather than raising the rest.** Raising fails
  whenever the bass sits high in the arrangement, leaving other notes stranded
  between the floor and it. The cost is that a slash chord can voice up to an
  octave below the one asked for.

**`Notes::identify` beats `pitch_set().identify()`** because it can see the
bass. It prefers the reading rooted there, and falls back to the rank heuristic
with the bass recorded as a slash. Inversions cannot round-trip exactly, since
C6 over E and Am7 over E are the same sounding notes; what survives a round trip
is the pitch set and the bass, and a test says exactly that.

`PitchSet::identify` picks one reading using `ChordQuality::rank`. It is a
heuristic, so its answer is pinned by a test: change the ranking and the test
should change deliberately, not incidentally.

### Keys and degrees

- A `Key` is a tonic and a `Mode`. Its `signature()` is its parent major's,
  from -5 to 6; six is always six sharps, because a pitch class cannot say
  which enharmonic was meant. `spelling()` is flats for a negative signature.
- `degree_of` is total. A root in the key is never altered. Otherwise the step
  is the one the note names in the parallel major — flats (`bII`, `bIII`,
  `bVI`, `bVII`), except the tritone, which is the raised fourth — and the
  accidental is measured against the mode's own step, so it is never more than
  one. A test sweeps every root in every key to hold that.
- A `Degree` ignores the bass: a slash chord is the degree of its root.
- `Key::name_of` writes a pitch as a score in the key does: the seven scale
  notes take seven consecutive letters from the tonic's, every other pitch
  takes the letter of the step `degree_of` reads it as. A `NoteName` is the
  letter plus accidentals (never more than two here); `octave_of` and
  `staff_step` follow the letter, so C flat 4 is written in octave 4 though it
  sounds as B 3. `Display` stays one text per value (sharps); a name is asked
  for, never implied.

## Writing examples

The three examples in `music-core` are for people new to Rust, not a showcase.

- **No arrays, `for` loops, iterators, `collect`, `Vec`, or hash collections.**
  One named binding per idea, one printed line per fact, comparisons written out
  rather than generated.
- Plain `println!` headings with a literal underline. No helper that computes
  the underline.
- Prefer repeating five lines over writing one loop. The line count is supposed
  to be higher and the difficulty lower.
- If a claim needs an exhaustive sweep to prove, state it in prose and let the
  test suite carry the proof.

## Design rules

- `#![no_std]` by default; `std` is an additive feature. Never introduce `alloc`
  in the core. Return fixed-size arrays or custom iterators, never `Vec`.
- `#![forbid(unsafe_code)]`. No exceptions.
- Every public type is `Copy` and `Eq`, and `#[repr(transparent)]` or a small
  plain struct. `tests/theory.rs` asserts the sizes; if one grows, the docs have
  started lying. The transformation core is one or two bytes; register-bearing
  types are four to sixty-six.
- `std` gates exactly two methods, `Note::frequency_hz` and `frequency_hz_at`,
  because `core` has no floating-point maths. Keep additions like this additive:
  a `no_std` user should lose a method, never get a different one.
- `const fn` and `#[inline]` on anything in a hot path.
- No dependencies in the library. Keep it that way without a measured reason.
  Dev dependencies come from the workspace's `[workspace.dependencies]`, and
  the benches use divan rather than criterion: criterion turns on serde's
  `derive`, which `cargo metadata` then reports as shipping (see
  `scripts/check-licenses.mjs`).
- `missing_docs` is warned on: document every public item.
- `Debug` prints the same text as `Display` for the music types, so a failing
  assertion reads as music rather than as struct fields.

## API conventions

- **`new` never fails.** Every constructor named `new` is total. One that can
  fail says what it converts from and returns `Option`: `Triad::from_index`,
  `PitchSet::from_bits`, `Notes::from_slice`. This is why `Pitch::new` takes an
  `i32` and wraps rather than taking a `u8` and returning `Option`. A pitch
  class *is* an integer modulo twelve, so there is no invalid input; validating
  a byte is a question about the input, not about the note it denotes.
- **Constructors over parsing.** Every parseable type also has constructors, and
  docs, examples and tests use those. `FromStr` is for real user input. No
  example in the crate contains `.parse().unwrap()`.
- **One verb per idea.** Giving something a register is `voice`. Projecting back
  down is `pitch` or `pitch_set`. Never a synonym for either.
- **Method names say what comes back.** `distance_to` returns a distance,
  `interval_to` returns an interval.
- **No type exists only to be converted into another.** `Spelled` is the one
  wrapper, and it exists to print: it carries a spelling to `Display` without
  changing the value.
- **Machinery is private.** `VoiceMap` and `cheapest_assignment` are
  crate-private; the public answers are `voice_leading_distance` and
  `displacement`.
- **Type parameters appear only where they carry meaning.** The voice count
  lives in `Voiceable`'s associated types, never in a signature.

## The theory this maps onto

**The OPTIC relations** (Callender, Quinn and Tymoczko) name what each
collection type throws away:

| Type | Equivalent under | So it forgets |
|---|---|---|
| `PitchSet` | Octave, Permutation | register and order |
| `Voiced` | Permutation | order only; voices are indexed by chord function |
| `Notes` | nothing | nothing |

The `Voiced` row is the one that bites: because voices are ordered by function,
a transformation can move which voice holds which note, so `displacement` has to
solve an assignment rather than compare index to index.

## Arithmetic conventions

- `Pitch` wraps at every edge and has no range. Verified across all 65,536
  interval values.
- `Interval` is a signed semitone count, never wrapped and never clamped.
- `a - b` on pitches is the **shortest signed path**, not `interval_to`. Its
  magnitude equals `b.distance_to(a)`, and it is antisymmetric except at the
  tritone, where both directions are equally short and the tie resolves upward.
  That tie-break matches `Triad::voice_map`.
- `Note` stores a 16-bit octave, so `from_midi(m).midi() == m` for every `i16`.
  That costs four bytes per note and is why the register-bearing types are
  larger than the core ones. Nothing here is on a hot path.
- `midi()` is not clamped to 0 through 127. An octave too large for a 16-bit
  MIDI number trips a debug assertion inside `midi_of`.

## Two traps worth knowing

**Voicings do not preserve voice identity.** `Voiced` stores one octave per
*chord function*, so index 1 always means "the third". After a transformation
the new third may be the note that used to be the root. Pairing voices by index
across a transformation compares unrelated notes, which is why
`Voiced::displacement` solves a minimum-cost assignment instead. `Notes` is the
opposite: it keeps whatever order you built it in, so its `displacement` pairs
by index and that is correct there.

**`Chord` is deliberately not `Voiceable`.** Its cardinality varies between
two and six notes, so it has no fixed voice count. Chords voice into `Notes`.

## Commands

    cargo test -p music-core
    cargo test -p music-core --no-default-features   # proves no_std
    cargo clippy -p music-core --all-targets -- -D warnings
    cargo bench -p music-core
    cargo run -p music-core --example chord

ctest runs the first two as `music_core_rs` and `music_core_no_std` in the
quick tier. Doctests run the README, so a claim on the front page is checked;
a block that needs `std` takes a hidden `#[cfg]` main, or it breaks the
`no_std` run.
