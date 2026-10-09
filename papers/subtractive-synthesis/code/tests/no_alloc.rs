// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Everything the paper says runs per sample costs no allocation, asserted
 * rather than claimed: the oscillators, both filters, the shaper, the
 * oversampler, unison, the allocator and a whole voice. Construction is
 * outside the guarded window, where it belongs.
 */

use std::hint::black_box;

use assert_no_alloc::{assert_no_alloc, violation_count, AllocDisabler};
use ni_paper_subsynth::filter::{ladder::Ladder, svf::Svf};
use ni_paper_subsynth::osc::{dpw::DpwSaw, naive::NaiveSaw, polyblep::*, wavetable::*, Osc};
use ni_paper_subsynth::oversample::Oversampled;
use ni_paper_subsynth::shaper::{Adaa, Tanh};
use ni_paper_subsynth::voice::{alloc::Allocator, unison::Unison, Voice};

#[global_allocator]
static ALLOCATOR: AllocDisabler = AllocDisabler;

#[test]
fn rendering_allocates_nothing() {
    let fs = 48_000.0;
    let table = Wavetable::new(&[saw_frame(2048), saw_frame(2048).iter().map(|x| -x).collect()], 1);
    let mut wt = WavetableOsc::new(&table, 440.0, fs, 24_000.0);
    let mut naive = NaiveSaw::new(440.0, fs);
    let mut blep = PolyBlepSaw::new(440.0, fs);
    let mut pulse = PolyBlepPulse::new(440.0, fs, 0.3);
    let mut dpw = DpwSaw::new(440.0, fs);
    let mut svf = Svf::default();
    let mut ladder = Ladder::with_drive(2.0);
    let mut shaper = Adaa::<Tanh>::default();
    let mut os = Oversampled::new(|x: f32| (3.0 * x).tanh());
    let mut uni = Unison::new(7, 110.0, 20.0, 1.0, fs);
    let mut voices = Allocator::<8>::default();
    let mut voice = Voice::new(fs);
    let mut buf = [0.0f32; 256];

    let sum = assert_no_alloc(|| {
        let mut sum = 0.0f32;
        voice.start(220.0);
        for n in 0..4800 {
            let f0 = 100.0 + n as f32;
            wt.position = (n as f32 / 4800.0).fract();
            wt.set_freq(f0, fs);
            dpw.set_freq(f0, fs);
            svf.set(f0 * 4.0, 2.0, fs);
            ladder.set(f0 * 4.0, 0.8, fs);
            let x = naive.next() + blep.next() + pulse.next() + dpw.next() + wt.next();
            sum += svf.tick(x).lp + ladder.tick(x) + shaper.tick(x) + os.tick(x);
            let (l, r) = uni.tick();
            sum += l + r;
            let v = voices.note_on((n % 128) as u8);
            voices.note_off((n % 128) as u8);
            voices.free(v);
        }
        voice.render(&mut buf);
        voice.stop();
        sum + buf.iter().sum::<f32>()
    });

    black_box(sum);
    let n = violation_count();
    assert_eq!(n, 0, "rendering allocated or freed {n} times");
}
