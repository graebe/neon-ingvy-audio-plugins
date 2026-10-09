// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What each building block costs per sample: the cost column of the paper's
 * section 4. One block of 4096 samples per iteration, so the per-sample figure
 * is the time divan reports divided by 4096.
 *
 *   cargo bench -p ni-paper-subsynth
 */

use divan::{black_box, Bencher};
use ni_paper_subsynth::filter::{ladder::Ladder, svf::Svf};
use ni_paper_subsynth::osc::{dpw::DpwSaw, naive::NaiveSaw, polyblep::PolyBlepSaw, wavetable::*, Osc};
use ni_paper_subsynth::oversample::Oversampled;
use ni_paper_subsynth::shaper::{Adaa, Tanh};
use ni_paper_subsynth::voice::unison::Unison;

const N: usize = 4096;
const FS: f32 = 48_000.0;

fn main() {
    divan::main();
}

fn run(b: Bencher, mut f: impl FnMut() -> f32) {
    b.counter(divan::counter::ItemsCount::new(N)).bench_local(|| {
        let mut s = 0.0;
        for _ in 0..N {
            s += f();
        }
        black_box(s)
    });
}

#[divan::bench]
fn osc_naive(b: Bencher) {
    let mut o = NaiveSaw::new(440.0, FS);
    run(b, || o.next());
}

#[divan::bench]
fn osc_polyblep(b: Bencher) {
    let mut o = PolyBlepSaw::new(440.0, FS);
    run(b, || o.next());
}

#[divan::bench]
fn osc_dpw(b: Bencher) {
    let mut o = DpwSaw::new(440.0, FS);
    run(b, || o.next());
}

#[divan::bench]
fn osc_wavetable(b: Bencher) {
    let t = Wavetable::new(&[saw_frame(2048), saw_frame(2048)], 1);
    let mut o = WavetableOsc::new(&t, 440.0, FS, 24_000.0);
    o.position = 0.5;
    run(b, || o.next());
}

#[divan::bench]
fn unison_7(b: Bencher) {
    let mut u = Unison::new(7, 440.0, 20.0, 1.0, FS);
    run(b, || u.tick().0);
}

#[divan::bench]
fn filter_svf_modulated(b: Bencher) {
    let mut f = Svf::default();
    let mut n = 0.0f32;
    run(b, || {
        n += 1.0;
        f.set(1_000.0 + n % 1000.0, 2.0, FS);
        f.tick(black_box(0.5)).lp
    });
}

#[divan::bench]
fn filter_ladder_modulated(b: Bencher) {
    let mut f = Ladder::with_drive(1.0);
    let mut n = 0.0f32;
    run(b, || {
        n += 1.0;
        f.set(1_000.0 + n % 1000.0, 0.8, FS);
        f.tick(black_box(0.5))
    });
}

#[divan::bench]
fn shaper_tanh(b: Bencher) {
    let mut x = 0.0f32;
    run(b, || {
        x += 0.01;
        (3.0 * x.sin()).tanh()
    });
}

#[divan::bench]
fn shaper_tanh_adaa(b: Bencher) {
    let mut s = Adaa::<Tanh>::default();
    let mut x = 0.0f32;
    run(b, || {
        x += 0.01;
        s.tick(3.0 * x.sin())
    });
}

#[divan::bench]
fn shaper_tanh_2x(b: Bencher) {
    let mut s = Oversampled::new(|v: f32| (3.0 * v).tanh());
    let mut x = 0.0f32;
    run(b, || {
        x += 0.01;
        s.tick(x.sin())
    });
}
