// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The plugin's audio thread allocates nothing through this ABI, asserted:
 * a block as SpectrogramProcessor.cpp runs it -- the receiver acquired from
 * the handoff, the block's mono sum copied into its ring, the handoff let go
 * of -- and the analyzer's own push, which runs the transforms on the audio
 * thread and rebuilds its band table there after a range change. Blocks of
 * every length, at a pitch that moves, and a receiver replaced mid-run as a
 * rate change replaces it.
 *
 * The message thread's work runs between the measured blocks and is not
 * measured: pumping the receiver (the transforms, which its worker would run),
 * drawing a frame, draining the analyzer, changing a range, and building,
 * installing and collecting a fresh receiver. All of it is allowed to
 * allocate.
 *
 * The guard is assert_no_alloc's: it watches the thread that runs the
 * closure, and counts rather than aborts.
 */

use std::ffi::{c_int, c_void};
use std::ptr::null_mut;

use shell_capi::{shell_handoff_acquire, shell_handoff_collect, shell_handoff_free, shell_handoff_new,
                 shell_handoff_release, shell_handoff_set};
use spectro_capi::*;

use assert_no_alloc::{assert_no_alloc, violation_count, AllocDisabler};

#[global_allocator]
static ALLOCATOR: AllocDisabler = AllocDisabler;

const RATE: f32 = 48_000.0;
const MAX_FRAMES: usize = 1024;
const MAX_COLS: usize = 64;

unsafe extern "C" fn free_receiver(r: *mut c_void) {
    srecv_free(r.cast());
}

fn new_receiver() -> *mut Srecv {
    let fft = spectro_pick_fft_size(RATE);
    srecv_new(RATE, fft, spectro_pick_hop(RATE, fft), SPECTRO_BANDS, SPECTRO_F_MIN, SPECTRO_F_MAX, SPECTRO_DB_FLOOR,
              SPECTRO_DB_CEIL)
}

#[test]
fn a_block_through_the_c_abi_allocates_nothing() {
    unsafe {
        let lend = shell_handoff_new(Some(free_receiver));
        let mut recv = new_receiver();
        shell_handoff_set(lend, recv.cast());
        let analyzer = spectro_new();

        let mut mono = vec![0f32; MAX_FRAMES];
        let mut picture = vec![0u8; MAX_COLS * SPECTRO_BANDS as usize];
        let (mut drawn, mut drained) = (0, 0);

        let mut t = 0usize;
        for block in 0..512usize {
            /* The host's block: any length up to what was reserved. */
            let frames = 64 + block * 97 % (MAX_FRAMES - 64);
            let hz = 55.0 * (1.0 + (block % 48) as f32 / 12.0);
            for (i, s) in mono[..frames].iter_mut().enumerate() {
                *s = 0.5 * (std::f32::consts::TAU * hz * (t + i) as f32 / RATE).sin();
            }
            t += frames;

            assert_no_alloc(|| {
                let r = shell_handoff_acquire(lend).cast::<Srecv>();
                if !r.is_null() {
                    srecv_push_own(r, mono.as_ptr(), frames as c_int);
                }
                shell_handoff_release(lend);
                spectro_push_f32(analyzer, mono.as_ptr(), frames as c_int);
            });

            /* The message thread, between blocks. */
            if block % 2 == 1 {
                srecv_pump(recv);
                drawn += srecv_frame(recv, &SRECV_OWN, 1, -1, -1, picture.as_mut_ptr(), null_mut(), MAX_COLS as c_int,
                                     null_mut());
                drained += spectro_take_columns(analyzer, picture.as_mut_ptr(), MAX_COLS as c_int);
            }
            if block % 128 == 64 {
                let (lo, hi) = if block % 256 == 64 { (40.0, 16_000.0) } else { (SPECTRO_F_MIN, SPECTRO_F_MAX) };
                spectro_set_range(analyzer, lo, hi);
                srecv_set_range(recv, lo, hi);
            }
            if block == 300 {
                recv = new_receiver();
                shell_handoff_set(lend, recv.cast());
            }
            shell_handoff_collect(lend);
        }

        let n = violation_count();
        assert_eq!(n, 0, "the audio path allocated or freed {n} times");
        assert!(drawn > 0 && drained > 0, "the pushes made columns: {drawn} drawn, {drained} drained");
        spectro_free(analyzer);
        shell_handoff_free(lend);
    }
}
