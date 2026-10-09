// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The audio thread allocates nothing, asserted rather than claimed.
 *
 * Every entry point of this engine runs on an audio callback -- on the Move,
 * set_param, get_param and on_midi as much as process -- so "no allocation
 * outside Instance::new" is a claim about all of them. It stays true only until
 * somebody collects a String in a parser that looked harmless;
 * `rates::index_from` did exactly that for a numeric rate, which is what the
 * Move's knob writes.
 *
 * The guard refuses allocations AND frees: a free is the same lock as a
 * malloc.
 *
 * The guard is assert_no_alloc's, and it watches one thread: inside the
 * closure, an allocation or a free on the thread that runs it is a violation,
 * while the threads cargo runs other tests on are not watched at all. It
 * counts rather than aborts (warn_debug, warn_release), so the assertion
 * below can say how many.
 */


use sc_core::params::{Param, PARAM_COUNT};
use sc_core::{Instance, Transport};

use assert_no_alloc::{assert_no_alloc, violation_count, AllocDisabler};

#[global_allocator]
static ALLOCATOR: AllocDisabler = AllocDisabler;

const SETS: &[(&str, &str)] = &[
    ("rate", "1/8T"),
    ("rate", "3"),
    ("rate", " 7 junk"),
    ("source", "MIDI"),
    ("time_mode", "% of cycle"),
    ("curve", "S-Curve"),
    ("midi_mode", "Gate"),
    ("channel", "Omni"),
    ("delay", "-12.5"),
    ("attack", "4"),
    ("hold", "9"),
    ("release", "40"),
    ("depth", "0.7"),
    ("trigger_note", "36"),
    ("trigger_note", "F#3"),
    ("vel_sens", "0.5"),
    ("threshold", "-18"),
    ("lockout", "30"),
    ("source", "Sidechain"),
    /* A whole patch, as Schwung restores one: a known key, an unknown one,
     * one with no value and one that is no number. */
    (
        "state",
        "sc1;source=1;rate=6;depth=0.5;wobble=2;attack;release=fast;threshold=-18;lockout=30",
    ),
    ("state", "not a blob"),
    ("panic", "1"),
    ("source", "Cycle"),
];

/* What the MIDI decode refuses, or reads and has no use for. A refusal is an
 * error value inside `wmidi`, and it must cost no more than a note does. */
const MIDI_IGNORED: &[&[u8]] = &[
    &[0x90, 36, 0x80],         /* a status byte where the velocity belongs */
    &[0x90, 36],               /* cut short */
    &[36, 100, 0],             /* running status */
    &[0xF0, 0x7E, 0x7F, 0xF7], /* SysEx */
    &[0xB0, 64, 127],          /* a controller that is not a panic */
    &[0xF8],                   /* the clock */
];

const GETS: &[&str] = &[
    "state", "ui", "params", "stage_ms", "phase", "sweep", "ms_per_cycle", "fires", "duck",
    "key_level", "advancing", "dropped", "rate_label", "curve_label", "source_label",
    "source", "rate", "trigger_note", "threshold", "no-such-key",
];

#[test]
fn process_params_and_midi_allocate_nothing() {
    let mut p = Instance::new(44100.0); /* allocates, and is allowed to */

    let mut inter = vec![0.25f32; 256 * 2];
    let mut l = vec![0.25f32; 256];
    let mut r = vec![0.25f32; 256];
    let mut gain = vec![0.0f32; 256];
    let mut sweep = vec![0.0f32; 256];
    let key: Vec<f32> = (0..256).map(|i| if i % 64 < 4 { 0.9 } else { 0.0 }).collect();
    let mut i16s = vec![8000i16; 256 * 2];
    let mut out = vec![0u8; 4096];

    assert_no_alloc(|| {
        let mut beats = 0.0;
        for block in 0..64 {
            let t = Transport { running: block % 16 != 15, beats, bpm: 120.0 };
            p.push_key(&key, &key, 256);
            p.on_midi(&[0x90, 36, 100], block % 256);
            p.on_midi(&[0x80, 36, 0], 200);
            p.on_midi(MIDI_IGNORED[block % MIDI_IGNORED.len()], 100);
            if block % 16 == 7 {
                p.on_midi(&[0xB0, 123, 0], 255); /* a host panic */
            }
            match block % 4 {
                0 => p.process_f32(&mut inter, 256, Some(&t)),
                1 => p.process_f32_split(&mut l, &mut r, 256, Some(&t)),
                2 => p.process_f32_split_tap(&mut l, &mut r, Some(&mut gain), Some(&mut sweep), 256, Some(&t)),
                _ => p.process_i16(&mut i16s, 256, Some(&t)),
            }
            beats += 256.0 / 44100.0 * 2.0;
            let (k, v) = SETS[block % SETS.len()];
            p.set_param(k, v);
            p.set_num(Param::from_i32(block as i32 % PARAM_COUNT).unwrap(), 0.5);
            for k in GETS {
                p.get_param(k, &mut out);
            }
        }
    });

    let n = violation_count();
    assert_eq!(n, 0, "the audio path allocated or freed {n} times");
}
