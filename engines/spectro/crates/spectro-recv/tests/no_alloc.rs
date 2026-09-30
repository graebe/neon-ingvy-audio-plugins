/*
 * The receiver allocates nothing once it is running, asserted rather than claimed.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * TWO THREADS ARE BEING PROTECTED HERE, FOR DIFFERENT REASONS.
 *
 * `push_own` runs on the AUDIO thread. A malloc there is not a slow path, it is
 * a lock shared with every other thread in the process, and the symptom is a
 * click under load that no profiler catches because it happens once a minute.
 *
 * `pump` and `take_columns` run on the MESSAGE thread, where an allocation
 * cannot make a noise -- but it can make the editor stutter, and more to the
 * point every buffer they touch is sized up front on purpose. A `vec![]` that
 * appeared inside the pump loop would be a silent undoing of that design, and
 * nothing else in this repository would notice.
 *
 * A real bus is deliberately NOT used: `Reader::read` is a memcpy out of shared
 * memory and is covered by audio-bus's own no_alloc. What is measured here is
 * this crate's own bookkeeping -- the staging, the mono sum, the carry.
 *
 * THIS FILE MUST HOLD EXACTLY ONE TEST. The counter is global and cargo runs
 * tests in threads, so a second test in this binary could allocate inside the
 * measured window and the failure would look like a real regression.
 */

use std::alloc::{GlobalAlloc, Layout, System};
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};

use spectro_core::Config;
use spectro_recv::{Receiver, OWN};

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
    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
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

#[test]
fn pushing_pumping_and_draining_allocate_nothing() {
    let (mut r, mut feed) = Receiver::new(Config::default()); /* allocates, and is allowed to */

    /* Everything the measured window touches is built before it opens: the
     * input block, the output buffer, and whatever the formatter behind a
     * failing assert would want. */
    let block: Vec<f32> = (0..2048).map(|i| (i as f32 * 0.01).sin() * 0.5).collect();
    let mut out = vec![0u8; r.bands() * 32];
    let clash_a = vec![0u8; r.bands() * 4];
    let clash_b = vec![0u8; r.bands() * 4];
    let mut clash_out = vec![0u8; r.bands() * 4];
    let mut cols = 0usize;

    ARMED.store(true, Ordering::Relaxed);
    for _ in 0..64 {
        feed.push(&block);
        r.pump();
        cols += r.take_columns(OWN, &mut out, 32);
        r.clash_into(&clash_a, &clash_b, &mut clash_out);
    }
    /* Read back inside the window too: a lazy counter that only allocates when
     * someone asks would slip past a test that never asked. */
    let dropped = r.own_dropped();
    ARMED.store(false, Ordering::Relaxed);

    let n = ALLOCS.load(Ordering::Relaxed);
    assert_eq!(n, 0, "the receiver allocated {n} times while running");

    /* And it has to have done the work, or zero allocations means nothing. */
    assert!(cols > 0, "no columns were produced, so nothing was measured");
    assert_eq!(dropped, 0, "the ring overflowed during the measurement");

    /* Silence against silence is no clash, which is also the resting state of
     * the buffers above -- asserted so the call is not optimised into nothing. */
    assert!(clash_out.iter().all(|&v| v == 0));
}
