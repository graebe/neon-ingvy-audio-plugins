//! Benchmarks for the core types.
//!
//! Each walks a fixed sweep rather than a single call, so the numbers are
//! per-sweep and stable enough to compare across runs.

use std::hint::black_box;

use criterion::{Criterion, criterion_group, criterion_main};
use music_core::{Chord, ChordQuality, Interval, Pitch, PitchSet};

/// The 24 consonant triads as pitch-class sets, built without the theory crate.
fn all_triad_sets() -> Vec<PitchSet> {
    let mut out = Vec::with_capacity(24);
    for pitch in Pitch::ALL {
        out.push(Chord::new(pitch, ChordQuality::Major).pitch_set());
        out.push(Chord::new(pitch, ChordQuality::Minor).pitch_set());
    }
    out
}

fn sets(c: &mut Criterion) {
    let mut group = c.benchmark_group("pitch_set");

    group.bench_function("intersection over 24 pairs", |b| {
        let sets: Vec<PitchSet> = all_triad_sets();
        b.iter(|| {
            let mut acc = 0u32;
            for set in &sets {
                acc += (black_box(*set) & black_box(sets[0])).len();
            }
            acc
        })
    });

    group.bench_function("transpose over 12 steps", |b| {
        let set = Chord::major(Pitch::C).pitch_set();
        b.iter(|| {
            let mut acc = 0u16;
            for n in 0..12 {
                acc ^= black_box(set).transpose(Interval::new(n)).bits();
            }
            acc
        })
    });

    group.bench_function("interval vector over 24 sets", |b| {
        let sets: Vec<PitchSet> = all_triad_sets();
        b.iter(|| {
            let mut acc = 0u32;
            for set in &sets {
                acc += black_box(*set).interval_vector()[3] as u32;
            }
            acc
        })
    });

    group.finish();
}

fn search_and_parse(c: &mut Criterion) {
    let mut group = c.benchmark_group("search_and_parse");

    group.bench_function("parse chord", |b| {
        b.iter(|| black_box("Cm7b5").parse::<Chord>().unwrap())
    });

    group.bench_function("parse chord, long", |b| {
        b.iter(|| black_box("F#m7b5").parse::<Chord>().unwrap())
    });

    group.finish();
}

criterion_group!(benches, sets, search_and_parse);
criterion_main!(benches);
