// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The audio thread allocates nothing, asserted rather than claimed.
 *
 * On the Move every entry point runs on the audio callback -- set_param,
 * get_param and on_midi as much as process -- so the claim covers all of
 * them, with every control moving and Mode switching both ways, so that each
 * glide, reshape, analysis, frame, priming and dip runs inside the guard. A
 * new sample rate is applied inside it too: the plugin's shell applies one on
 * the audio thread. The guard refuses allocations and frees on the thread
 * that runs the closure, and counts rather than aborts (warn_debug,
 * warn_release), so the assertion below can say how many.
 */

use vz_core::{Engine, Param, PARAM_COUNT};

use assert_no_alloc::{assert_no_alloc, violation_count, AllocDisabler};

#[global_allocator]
static ALLOCATOR: AllocDisabler = AllocDisabler;

const SETS: &[(&str, &str)] = &[
    ("vocal", "100"),
    ("vowel", "E>I 30 %"),
    ("mode", "Spectral"),
    ("size", "-60"),
    ("resonance", "90"),
    ("excite", "40"),
    ("vowel", "u"),
    ("mode", "0"),
    ("mix", "50"),
    ("output", "-3 dB"),
    ("vocal", "words"),
    ("panic", "1"),
];

const GETS: &[&str] = &["vocal", "vowel", "mode", "no-such-key"];

#[test]
fn process_params_rates_and_midi_allocate_nothing() {
    let mut e = Engine::new(44_100.0); /* allocates, and is allowed to */

    let mut l: Vec<f32> = (0..256).map(|i| (i as f32 * 0.1).sin() * 0.3).collect();
    let mut r = vec![-0.25f32; 256];
    let mut i16s = vec![8000i16; 256 * 2];
    let mut out = vec![0u8; 64];
    let hz: Vec<f64> = (1..=32).map(|i| i as f64 * 300.0).collect();
    let (mut din, mut dout) = (vec![0.0; 32], vec![0.0; 32]);

    assert_no_alloc(|| {
        for block in 0..240 {
            match block % 3 {
                0 => e.process_f32(&mut l, Some(&mut r)),
                1 => e.process_f32(&mut l, None),
                _ => e.process_i16(&mut i16s, 256),
            }
            let (k, v) = SETS[(block / 4) % SETS.len()];
            e.set_param(k, v);
            let p = Param::from_i32(block as i32 % PARAM_COUNT as i32).unwrap();
            if p != Param::Mode {
                e.set_num(p, p.denormalise((block % 7) as f64 / 6.0));
            }
            e.on_midi(&[0xB0, 64, 127]);
            if block % 50 == 49 {
                e.on_midi(&[0xB0, 123, 0]);
            }
            if block == 120 {
                e.set_sample_rate(96_000.0);
            }
            for k in GETS {
                e.get_param(k, &mut out);
            }
            e.envelopes(&hz, &mut din, &mut dout);
        }
    });

    let n = violation_count();
    assert_eq!(n, 0, "the audio path allocated or freed {n} times");
}
