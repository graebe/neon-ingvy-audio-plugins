// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! Benchmarks for the core types.
//!
//! Each walks a fixed sweep rather than a single call, so the numbers are
//! per-sweep and stable enough to compare across runs.
//!
//! divan, not criterion: criterion asks for serde's `derive`, and a dev
//! dependency's features land in the one feature set `cargo metadata` reports
//! for the whole workspace -- which is what scripts/check-licenses.mjs reads to
//! decide what ships. Nothing here needs a report format.

use std::hint::black_box;

use music_core::{Chord, ChordQuality, Interval, Notes, Pitch, PitchSet};

fn main() {
    divan::main();
}

/// The 24 consonant triads as pitch-class sets, built without the theory crate.
fn all_triad_sets() -> Vec<PitchSet> {
    let mut out = Vec::with_capacity(24);
    for pitch in Pitch::ALL {
        out.push(Chord::from_quality(pitch, ChordQuality::Major).pitches());
        out.push(Chord::from_quality(pitch, ChordQuality::Minor).pitches());
    }
    out
}

#[divan::bench_group]
mod pitch_set {
    use super::*;

    #[divan::bench(name = "intersection over 24 pairs")]
    fn intersection(bencher: divan::Bencher) {
        let sets = all_triad_sets();
        bencher.bench_local(|| {
            let mut acc = 0u32;
            for set in &sets {
                acc += (black_box(*set) & black_box(sets[0])).len();
            }
            acc
        });
    }

    #[divan::bench(name = "transpose over 12 steps")]
    fn transpose() -> u16 {
        let set = Chord::major(Pitch::C).pitches();
        let mut acc = 0u16;
        for n in 0..12 {
            acc ^= black_box(set).transpose(Interval::new(n)).bits();
        }
        acc
    }

    #[divan::bench(name = "interval vector over 24 sets")]
    fn interval_vector(bencher: divan::Bencher) {
        let sets = all_triad_sets();
        bencher.bench_local(|| {
            let mut acc = 0u32;
            for set in &sets {
                acc += black_box(*set).interval_vector()[3] as u32;
            }
            acc
        });
    }
}

/// Naming what is sounding: the call a chord display makes on every change.
#[divan::bench_group]
mod identify {
    use super::*;

    #[divan::bench(name = "a voicing, bass first")]
    fn voicing() -> Option<Chord> {
        let notes = Notes::from_slice(&[
            Pitch::C.at(3),
            Pitch::A.at(3),
            Pitch::E.at(4),
            Pitch::G.at(4),
        ])
        .unwrap();
        black_box(notes).identify()
    }

    #[divan::bench(name = "every pitch-class set")]
    fn every_set() -> u32 {
        let mut named = 0u32;
        for bits in 0..4096u16 {
            if PitchSet::from_bits_truncating(black_box(bits))
                .identify()
                .is_some()
            {
                named += 1;
            }
        }
        named
    }
}

#[divan::bench_group]
mod parse {
    use super::*;

    #[divan::bench(name = "parse chord")]
    fn short() -> Chord {
        black_box("Cm7b5").parse::<Chord>().unwrap()
    }

    #[divan::bench(name = "parse chord, long")]
    fn long() -> Chord {
        black_box("F#m7b5").parse::<Chord>().unwrap()
    }
}
