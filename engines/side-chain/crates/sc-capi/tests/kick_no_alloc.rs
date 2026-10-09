// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The kick tap's audio-thread half allocates nothing, asserted rather than
 * claimed: sc_kick_note and sc_kick_drain run on the audio callback, on a real
 * bus (bus-core, in the namespace cargo gives every test), through every path
 * -- filed by timeline, waiting, by arrival, a loop, another rate. The guard is
 * assert_no_alloc's, as sc-core's tests/no_alloc.rs explains; it refuses frees
 * too.
 */
#![cfg(feature = "shell")]

use assert_no_alloc::{assert_no_alloc, violation_count, AllocDisabler};
use bus_capi::{abus_reader_close, abus_reader_open, ABUS_OK};
use bus_core::Writer;
use sc_capi::kick::*;

#[global_allocator]
static ALLOCATOR: AllocDisabler = AllocDisabler;

/* Its own slot, clear of src/kick/tests.rs's: a separate test binary. */
const SLOT: u32 = 9;

#[test]
fn note_and_drain_allocate_nothing() {
    let (mut writer, mut pusher) = Writer::claim(SLOT, 48_000).expect("claim");
    let k = sc_kick_create();
    let mut r = std::ptr::null_mut();
    unsafe {
        sc_kick_prepare(k, 48_000);
        assert_eq!(abus_reader_open(SLOT, &mut r), ABUS_OK);
    }
    let block: Vec<f32> = (0..512 * 2).map(|i| (i as f32 * 0.01).sin()).collect();
    let sweep: Vec<f32> = (0..512).map(|i| i as f32 / 512.0).collect();
    let (mut kick, mut sw) = (vec![0f32; 1024], vec![0f32; 1024]);

    assert_no_alloc(|| unsafe {
        for b in 0..400i64 {
            /* A loop every hundred blocks; the sender ahead every other block. */
            let at = (b % 100) * 512;
            if b % 2 == 0 {
                pusher.push_at(&block, Some(at));
            }
            sc_kick_note(k, at, 1, sweep.as_ptr(), 512);
            if b % 2 == 1 {
                pusher.push_at(&block, Some(at));
            }
            while sc_kick_drain(k, r, kick.as_mut_ptr(), sw.as_mut_ptr(), 1024) == 1024 {}
            core::hint::black_box(sc_kick_status(k));
        }
        /* Stopped: by arrival. */
        for _ in 0..8 {
            pusher.push(&block);
            sc_kick_note(k, 0, 0, sweep.as_ptr(), 512);
            sc_kick_drain(k, r, kick.as_mut_ptr(), sw.as_mut_ptr(), 1024);
        }
        /* Another rate, applied by the pusher. */
        writer.set_sample_rate(44_100);
        for _ in 0..4 {
            pusher.push(&block);
            sc_kick_drain(k, r, kick.as_mut_ptr(), sw.as_mut_ptr(), 1024);
        }
    });

    let n = violation_count();
    assert_eq!(n, 0, "the audio path allocated or freed {n} times");
    unsafe {
        assert_eq!(sc_kick_status(k), SC_KICK_OTHER_RATE);
        abus_reader_close(r);
        sc_kick_destroy(k);
    }
}
