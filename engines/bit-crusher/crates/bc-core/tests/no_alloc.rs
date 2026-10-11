// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The audio thread allocates nothing, asserted rather than claimed.
 *
 * On the Move every entry point runs on the audio callback -- set_param,
 * get_param and on_midi as much as process -- so the claim covers all of
 * them, with every control moving so that each glide, retune and crossfade
 * runs inside the guard. The guard refuses allocations and frees on the thread
 * that runs the closure, and counts rather than aborts (warn_debug,
 * warn_release), so the assertion below can say how many.
 */

use bc_core::{Engine, Param, PARAM_COUNT};

use assert_no_alloc::{assert_no_alloc, violation_count, AllocDisabler};

#[global_allocator]
static ALLOCATOR: AllocDisabler = AllocDisabler;

const SETS: &[(&str, &str)] = &[
    ("bits", "3.5"),
    ("rate", "4k"),
    ("rate", " 900 junk"),
    ("mid_rise", "On"),
    ("anti_alias", "1"),
    ("pre_filter", "on"),
    ("post_filter", "On"),
    ("post_octave", "-1.5"),
    ("jitter", "60"),
    ("dither", "40"),
    ("shape", "75"),
    ("drive", "+9"),
    ("mix", "50"),
    ("output", "-3 dB"),
    ("bits", "words"),
    ("panic", "1"),
];

const GETS: &[&str] = &["bits", "rate", "mid_rise", "no-such-key"];

#[test]
fn process_params_and_midi_allocate_nothing() {
    let mut e = Engine::new(44_100.0); /* allocates, and is allowed to */

    let mut l = vec![0.25f32; 256];
    let mut r = vec![-0.25f32; 256];
    let mut i16s = vec![8000i16; 256 * 2];
    let mut out = vec![0u8; 64];

    assert_no_alloc(|| {
        for block in 0..96 {
            match block % 3 {
                0 => e.process_f32(&mut l, Some(&mut r)),
                1 => e.process_f32(&mut l, None),
                _ => e.process_i16(&mut i16s, 256),
            }
            let (k, v) = SETS[block % SETS.len()];
            e.set_param(k, v);
            let p = Param::from_i32(block as i32 % PARAM_COUNT as i32).unwrap();
            e.set_num(p, p.denormalise((block % 7) as f64 / 6.0));
            e.on_midi(&[0xB0, 64, 127]);
            if block % 16 == 9 {
                e.on_midi(&[0xB0, 123, 0]);
            }
            for k in GETS {
                e.get_param(k, &mut out);
            }
        }
    });

    let n = violation_count();
    assert_eq!(n, 0, "the audio path allocated or freed {n} times");
}
