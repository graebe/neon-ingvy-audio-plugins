// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The audio thread allocates nothing, asserted rather than claimed.
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
 * The guard is assert_no_alloc's, and it watches one thread: inside the
 * closure, an allocation or a free on the thread that runs it is a violation,
 * while the threads cargo runs other tests on are not watched at all. It
 * counts rather than aborts (warn_debug, warn_release), so the assertion
 * below can say how many.
 */


use bus_core::{Reader, Writer};

use assert_no_alloc::{assert_no_alloc, violation_count, AllocDisabler};

#[global_allocator]
static ALLOCATOR: AllocDisabler = AllocDisabler;

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

    assert_no_alloc(|| {
        /* Two hundred blocks is 204,800 frames: more than a full ring, so the wrap
         * is inside the measured window rather than just after it. */
        for b in 0..200i64 {
            pusher.push_at(&block, Some(b * 1024));
            let got = reader.read(&mut out);
            /* And the stamps, which a reader on an audio thread looks up per run. */
            core::hint::black_box(reader.stamp_at(got.first));
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
    });

    let n = violation_count();
    assert_eq!(n, 0, "the audio path allocated or freed {n} times");
}
