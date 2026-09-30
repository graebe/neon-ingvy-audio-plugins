/*
 * The audio thread allocates nothing, asserted rather than claimed.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * "It does not allocate" is the kind of claim that stays true until someone
 * adds a `vec![]` inside a loop that looked like a good place for one. A malloc
 * on the audio thread is not a slow path, it is a lock shared with every other
 * thread in the process, and the symptom is a click under load that no profiler
 * catches because it happens once a minute.
 *
 * So the allocator counts, and this test fails if `push` (which runs the FFT)
 * or `take_columns` reaches it even once.
 *
 * THIS FILE MUST HOLD EXACTLY ONE TEST. The counter is global and cargo runs
 * tests in threads, so a second test in this binary could allocate inside the
 * measured window and the failure would look like a real regression.
 */


use spectro_core::{Analyzer, Config};

#[global_allocator]
static ALLOCATOR: ni_testkit::Counting = ni_testkit::Counting;

#[test]
fn push_and_take_allocate_nothing() {
    let cfg = Config::default();
    /* Split, the way the C ABI holds it: the halves the two threads own. */
    let (mut tx, mut rx) = Analyzer::new(cfg).split(); /* allocates, and is allowed to */

    /* Everything the measured window touches is built before it opens: the
     * input block, the output buffer, and whatever the formatter behind a
     * failing assert would want. */
    let block: Vec<f32> = (0..2048)
        .map(|i| (i as f32 * 0.01).sin() * 0.5)
        .collect();
    let mut out = vec![0u8; rx.bands() * 32];

    ni_testkit::arm();
    /* Eight blocks is 16384 samples: several hops, several transforms, and the
     * ring wrapping. */
    for _ in 0..8 {
        tx.push(&block);
        rx.take_columns(&mut out, 32);
    }

    /*
     * AND A RANGE CHANGE, which is the one thing that REBUILDS state rather than
     * only reading it. The dropdown that triggers it is on the message thread,
     * but the table it rewrites belongs to the audio thread and is rewritten
     * there -- in place, into vectors that already have the capacity. A `Vec`
     * that was rebuilt with `Vec::new()` instead of `clear()` would pass every
     * other test in this crate and allocate on the audio thread forever after.
     */
    rx.set_range(200.0, 4000.0);
    for _ in 0..8 {
        tx.push(&block);
        rx.take_columns(&mut out, 32);
    }
    ni_testkit::disarm();

    let n = ni_testkit::allocs() + ni_testkit::frees();
    assert_eq!(n, 0, "the audio path allocated or freed {n} times");
}
