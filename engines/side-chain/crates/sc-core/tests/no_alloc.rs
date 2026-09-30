/*
 * The audio thread allocates nothing, asserted rather than claimed.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * Every entry point of this engine runs on an audio callback -- on the Move,
 * set_param, get_param and on_midi as much as process -- so "no allocation
 * outside Instance::new" is a claim about all of them. It stays true only until
 * somebody collects a String in a parser that looked harmless;
 * `rates::index_from` did exactly that for a numeric rate, which is what the
 * Move's knob writes.
 *
 * The allocator counts allocations AND frees: a free is the same lock as a
 * malloc.
 *
 * THIS FILE MUST HOLD EXACTLY ONE TEST. The counter is global and cargo runs
 * tests in threads, so a second test in this binary could allocate inside the
 * measured window and the failure would look like a real regression.
 */

use std::alloc::{GlobalAlloc, Layout, System};
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};

use sc_core::params::{Param, PARAM_COUNT};
use sc_core::{Instance, Transport};

struct Counting;

static ARMED: AtomicBool = AtomicBool::new(false);
static ALLOCS: AtomicUsize = AtomicUsize::new(0);
static FREES: AtomicUsize = AtomicUsize::new(0);

unsafe impl GlobalAlloc for Counting {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        if ARMED.load(Ordering::Relaxed) {
            ALLOCS.fetch_add(1, Ordering::Relaxed);
        }
        System.alloc(layout)
    }
    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        if ARMED.load(Ordering::Relaxed) {
            FREES.fetch_add(1, Ordering::Relaxed);
        }
        System.dealloc(ptr, layout)
    }
    unsafe fn realloc(&self, ptr: *mut u8, layout: Layout, new_size: usize) -> *mut u8 {
        if ARMED.load(Ordering::Relaxed) {
            ALLOCS.fetch_add(1, Ordering::Relaxed);
        }
        System.realloc(ptr, layout, new_size)
    }
}

#[global_allocator]
static ALLOCATOR: Counting = Counting;

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
    ("panic", "1"),
    ("source", "Cycle"),
];

const GETS: &[&str] = &[
    "ui", "params", "stage_ms", "phase", "sweep", "ms_per_cycle", "fires", "duck",
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

    ARMED.store(true, Ordering::SeqCst);
    let mut beats = 0.0;
    for block in 0..64 {
        let t = Transport { running: block % 16 != 15, beats, bpm: 120.0 };
        p.push_key(&key, &key, 256);
        p.on_midi(&[0x90, 36, 100], block % 256);
        p.on_midi(&[0x80, 36, 0], 200);
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
    ARMED.store(false, Ordering::SeqCst);

    let (a, f) = (ALLOCS.load(Ordering::SeqCst), FREES.load(Ordering::SeqCst));
    assert_eq!((a, f), (0, 0), "the audio path allocated {a} times and freed {f} times");
}
