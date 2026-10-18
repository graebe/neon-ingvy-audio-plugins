// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The audio thread allocates nothing through this ABI, asserted, on either
 * end of a bus.
 *
 * The sender's block is ListenIn.cpp's: the pusher acquired from the handoff,
 * the block pushed with and without a timeline stamp, the handoff let go of.
 * A rate change the main thread sets is applied inside the push, so it is
 * measured too. The receiver's is a read and the lookups a reader on an audio
 * thread makes after it -- its position and the stamp behind a frame -- with
 * reads of every size, and a stretch of blocks it skips, so it falls behind
 * and is told how much it missed.
 *
 * The claim, the open, the label and the rate, and collecting the handoff
 * are the main thread's, run outside the measured blocks: they map shared
 * memory and allocate, and are allowed to.
 *
 * The guard is assert_no_alloc's: it watches the thread that runs the
 * closure, and counts rather than aborts.
 */

use std::ffi::{c_int, c_void};
use std::ptr::null_mut;

use bus_capi::*;
use shell_capi::{shell_handoff_acquire, shell_handoff_collect, shell_handoff_free, shell_handoff_new,
                 shell_handoff_release, shell_handoff_set};

use assert_no_alloc::{assert_no_alloc, violation_count, AllocDisabler};

#[global_allocator]
static ALLOCATOR: AllocDisabler = AllocDisabler;

/* Its own slot, clear of the ones src/tests.rs uses: the workspace gives
 * every test this checkout's namespace (.cargo/config.toml). */
const SLOT: u32 = 3;
const FRAMES: usize = 1024;

unsafe extern "C" fn release_pusher(p: *mut c_void) {
    abus_pusher_release(p.cast());
}

#[test]
fn both_ends_of_a_bus_allocate_nothing() {
    unsafe {
        let (mut writer, mut pusher) = (null_mut(), null_mut());
        assert_eq!(abus_writer_claim(SLOT, 48_000, &mut writer, &mut pusher), ABUS_OK, "slot {SLOT} is free");
        let lend = shell_handoff_new(Some(release_pusher));
        shell_handoff_set(lend, pusher.cast());
        let mut reader = null_mut();
        assert_eq!(abus_reader_open(SLOT, &mut reader), ABUS_OK);

        let ch = abus_channels() as usize;
        let block: Vec<f32> = (0..FRAMES * ch).map(|i| (i as f32 * 0.01).sin()).collect();
        let mut out = vec![0f32; 4 * FRAMES * ch];
        let (mut heard, mut missed, mut resyncs) = (0u64, 0u64, 0);

        for b in 0..600u32 {
            /* Behind for a stretch longer than the ring: the sender laps it. */
            let behind = (200..500).contains(&b);
            /* The sender's main thread, between blocks. A rate change restarts
             * the stream, so none while the reader is behind: it would be told
             * of the restart, not of what it missed. */
            if b % 50 == 25 && !behind {
                abus_writer_set_sample_rate(writer, if b % 100 == 25 { 96_000 } else { 48_000 });
                abus_writer_set_label(writer, c"Bass".as_ptr());
            }

            let frames = 64 + (b as usize * 37) % (FRAMES - 64);
            let timed = b % 8 != 7;
            assert_no_alloc(|| {
                let p = shell_handoff_acquire(lend).cast::<AbusPusher>();
                if b % 2 == 0 {
                    abus_pusher_push_at(p, block.as_ptr(), frames as u32, i64::from(b) * FRAMES as i64, c_int::from(timed));
                } else {
                    abus_pusher_push(p, block.as_ptr(), frames as u32);
                }
                shell_handoff_release(lend);
            });
            shell_handoff_collect(lend);

            if behind {
                continue;
            }
            assert_no_alloc(|| {
                let (mut dropped, mut resynced) = (0u64, 0i32);
                let max = (FRAMES / 2 + (b as usize * 53) % (3 * FRAMES)) as u32;
                let got = abus_reader_read(reader, out.as_mut_ptr(), max, &mut dropped, &mut resynced);
                let end = abus_reader_position(reader);
                let mut timeline = 0i64;
                abus_reader_timeline_of(reader, end.saturating_sub(u64::from(got)), &mut timeline);
                heard += u64::from(got);
                missed += dropped;
                resyncs += resynced;
            });
        }

        let n = violation_count();
        assert_eq!(n, 0, "the audio path allocated or freed {n} times");
        assert!(heard > 0 && missed > 0 && resyncs > 0, "the reader heard {heard} frames, missed {missed}, resynced {resyncs} times");
        abus_reader_close(reader);
        shell_handoff_free(lend);
        abus_writer_release(writer);
    }
}
