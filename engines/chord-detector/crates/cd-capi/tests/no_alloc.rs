// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The audio thread allocates nothing through the C ABI either.
 *
 * cd-core's own test covers the engine; this covers what the shell adds on
 * the same thread: the bridge's begin and end, a frame published with every
 * text rewritten (the reading changes every block here), and the event ring,
 * full or not. Reading and draining are the message thread's, run between
 * the measured blocks so the frames really cross, and are not measured:
 * shell-core's read collects deferred frees there, which is allowed to
 * allocate.
 *
 * The guard is assert_no_alloc's: it watches the thread that runs the
 * closure, and counts rather than aborts.
 */

use cd_capi::*;

use assert_no_alloc::{assert_no_alloc, violation_count, AllocDisabler};

#[global_allocator]
static ALLOCATOR: AllocDisabler = AllocDisabler;

#[test]
fn a_block_through_the_c_abi_allocates_nothing() {
    unsafe {
        let shell = cd_shell_create(48000.0);
        let mut reading: CdReading = std::mem::zeroed();
        let mut events = vec![
            CdNoteEvent {
                at: 0.0,
                note: 0,
                velocity: 0
            };
            256
        ];

        for block in 0..512u32 {
            assert_no_alloc(|| {
                let core = cd_shell_begin(shell);
                cd_core_begin_block(
                    core,
                    1,
                    block as f64 * 0.25,
                    120.0,
                    4,
                    4,
                    (block % 8 != 7) as i32,
                );
                for p in 0..cd_param_count() {
                    cd_core_set_param(core, p, (block as i32 + p) % 5);
                }
                let root = 36 + (block % 48) as u8;
                for (i, n) in [root, root + 4, root + 7, root + 10].iter().enumerate() {
                    cd_core_on_midi(core, [0x90, *n, 100].as_ptr(), 3, i as u32 * 30);
                }
                if block % 3 == 0 {
                    for n in [root, root + 4, root + 7, root + 10] {
                        cd_core_on_midi(core, [0x80, n, 0].as_ptr(), 3, 400);
                    }
                }
                if block % 50 == 49 {
                    cd_core_reset(core);
                }
                cd_core_end_block(core, 512);
                cd_shell_end(shell, 512);
            });
            if block % 4 == 0 {
                cd_shell_read(shell, &mut reading);
                cd_shell_drain(shell, events.as_mut_ptr(), events.len());
            }
        }

        let n = violation_count();
        assert_eq!(n, 0, "the audio path allocated or freed {n} times");
        cd_shell_destroy(shell);
    }
}
