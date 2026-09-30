/*
 * The audio thread allocates nothing, asserted rather than claimed.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * The same guard spectro-core carries, for the same reason: a malloc on the
 * audio thread is not a slow path, it is a lock shared with every other thread
 * in the process, and the symptom is a click under load that no profiler
 * catches because it happens once a minute.
 *
 * It matters more here than it did there. `push` looks like it could not
 * possibly allocate -- it is two memcpys and an atomic store -- but it sits
 * behind a C ABI that builds a slice from a raw pointer, and the day someone
 * adds a `format!` to a debug branch or collects the input into a Vec, nothing
 * else in this repository would notice.
 *
 * THIS FILE MUST HOLD EXACTLY ONE TEST. The counter is global and cargo runs
 * tests in threads, so a second test in this binary could allocate inside the
 * measured window and the failure would look like a real regression.
 */

use std::alloc::{GlobalAlloc, Layout, System};
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};

use bus_core::{Reader, Writer};

struct Counting;

static ARMED: AtomicBool = AtomicBool::new(false);
static ALLOCS: AtomicUsize = AtomicUsize::new(0);

unsafe impl GlobalAlloc for Counting {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        if ARMED.load(Ordering::Relaxed) {
            ALLOCS.fetch_add(1, Ordering::Relaxed);
        }
        System.alloc(layout)
    }
    /* Counted too: a free takes the same allocator lock a malloc does, and a
     * value dropped on the audio thread is the usual way one gets there. */
    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        if ARMED.load(Ordering::Relaxed) {
            ALLOCS.fetch_add(1, Ordering::Relaxed);
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

/* Its own slot, clear of the ones src/slots.rs uses -- this is a separate test
 * binary and may run alongside them. */
const SLOT: u32 = 11;

#[test]
fn push_and_read_allocate_nothing() {
    /* Everything the measured window touches is built before it opens: the
     * claim (which maps a segment and allocates its handles, and is allowed to), the
     * reader, the input block and the output buffer. */
    let (mut writer, mut pusher) = Writer::claim(SLOT, 48_000).expect("claim");
    let mut reader = Reader::open(SLOT).expect("open");

    let block: Vec<f32> = (0..1024 * 2).map(|i| (i as f32 * 0.01).sin()).collect();
    let mut out = vec![0f32; 4096 * 2];

    ARMED.store(true, Ordering::SeqCst);
    /* Two hundred blocks is 204,800 frames: more than a full ring, so the wrap
     * is inside the measured window rather than just after it. */
    for _ in 0..200 {
        pusher.push(&block);
        reader.read(&mut out);
    }
    /* And the label path, which is the one place a string crosses into the
     * segment. It is a message-thread call, not an audio-thread one, but it
     * writes to memory the audio thread is reading and a Vec hiding in it
     * would be a surprise in the worst place. */
    writer.set_label("Bass");
    /* And a rate change, which the audio thread applies inside `push`. */
    writer.set_sample_rate(96_000);
    for _ in 0..8 {
        pusher.push(&block);
        reader.read(&mut out);
    }
    ARMED.store(false, Ordering::SeqCst);

    let n = ALLOCS.load(Ordering::SeqCst);
    assert_eq!(n, 0, "the audio path allocated or freed {n} times");
}
