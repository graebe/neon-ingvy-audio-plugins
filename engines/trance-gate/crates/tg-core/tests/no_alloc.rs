/*
 * The audio thread allocates nothing, asserted rather than claimed.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * Every entry point of this engine runs on an audio callback -- on the Move,
 * set_param and get_param as much as process -- so "no allocation outside
 * Instance::new" is a claim about all three. It stays true only until somebody
 * collects a String in a parser that looked harmless; `rates::index_from` did
 * exactly that for a numeric rate, on every write from the Move's knob.
 *
 * The allocator counts allocations AND frees: a free is the same lock as a
 * malloc, and a temporary that is allocated before the window and dropped
 * inside it is still a lock taken on the audio thread.
 *
 * THIS FILE MUST HOLD EXACTLY ONE TEST. The counter is global and cargo runs
 * tests in threads, so a second test in this binary could allocate inside the
 * measured window and the failure would look like a real regression.
 */


use tg_core::params::Param;
use tg_core::{Instance, Transport};

#[global_allocator]
static ALLOCATOR: ni_testkit::Counting = ni_testkit::Counting;

/* Every key the string door serves, with a value of each shape it takes. */
const SETS: &[(&str, &str)] = &[
    ("rate", "1/32T"),
    ("rate", "3"),
    ("rate", " 11 junk"),
    ("rate", "-4"),
    ("slot", "2"),
    ("length", "31"),
    ("attack", "12.5"),
    ("decay", "40"),
    ("sustain", "0.4"),
    ("hold", "0.6"),
    ("release", "30"),
    ("amount", "0.8"),
    ("cursor", "5"),
    ("step", "Tie"),
    ("step", "1"),
    ("step_amount", "0.5"),
    ("step_order", "2"),
    ("fade", "0.7"),
    ("fade_soft", "On"),
    ("fade_dir", "Out"),
    ("randomize", "Roll"),
    ("randomize", "1234"),
    ("legato", "1"),
    ("curve", "S-Curve"),
    ("time_mode", "%"),
    ("pattern", "F0F0"),
    ("ties", "0100"),
    ("slot", "0"),
];

const GETS: &[&str] = &[
    "name", "slot", "length", "rate", "attack", "decay", "sustain", "release", "hold",
    "amount", "legato", "fade", "fade_soft", "fade_dir", "step_order", "time_mode", "curve",
    "ms_per_step", "width_ms", "cursor", "step", "step_amount", "pattern", "ties", "phase",
    "phase:effective", "params", "ui", "state", "no-such-key",
];

#[test]
fn process_set_param_and_get_param_allocate_nothing() {
    let mut p = Instance::new(44100.0); /* allocates, and is allowed to */

    /* Everything the measured window touches is built before it opens. */
    let mut inter = vec![0.25f32; 256 * 2];
    let mut l = vec![0.25f32; 256];
    let mut r = vec![0.25f32; 256];
    let mut i16s = vec![8000i16; 256 * 2];
    let mut out = vec![0u8; 8192];
    let mut state = vec![0u8; 8192];
    let n = p.get_param("state", &mut state) as usize;
    let state = String::from_utf8(state[..n].to_vec()).unwrap();

    ni_testkit::arm();
    let mut beats = 0.0;
    for block in 0..64 {
        let t = Transport { running: block % 16 != 15, beats, bpm: 123.0 };
        match block % 3 {
            0 => p.process_f32(&mut inter, 256, Some(&t)),
            1 => p.process_f32_split(&mut l, &mut r, 256, Some(&t)),
            _ => p.process_i16(&mut i16s, 256, Some(&t)),
        }
        beats += 256.0 / 44100.0 * 123.0 / 60.0;
        let (k, v) = SETS[block % SETS.len()];
        p.set_param(k, v);
        p.set_num(Param::from_i32((block % 15) as i32).unwrap(), 0.5);
        for k in GETS {
            p.get_param(k, &mut out);
        }
    }
    p.set_param("state", &state);
    ni_testkit::disarm();

    let (a, f) = (ni_testkit::allocs(), ni_testkit::frees());
    assert_eq!((a, f), (0, 0), "the audio path allocated {a} times and freed {f} times");
}
