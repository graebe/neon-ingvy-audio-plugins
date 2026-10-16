// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What the audio thread calls through the C ABI allocates nothing: a block
 * between fd_shell_begin and fd_shell_end, with the commands it applies and
 * the frame it publishes -- the analyzer's rings copied out included.
 */

use assert_no_alloc::{assert_no_alloc, violation_count, AllocDisabler};
use fd_capi::shell::*;
use fd_capi::*;

#[global_allocator]
static ALLOCATOR: AllocDisabler = AllocDisabler;

#[test]
fn a_block_through_the_shell_allocates_nothing() {
    let shell = fd_shell_create(48_000.0);
    let mut l = vec![0.25f32; 256];
    let mut r = vec![-0.25f32; 256];
    unsafe {
        fd_shell_post_watching(shell, true);
        fd_shell_post_sample_rate(shell, 96_000.0);
        fd_shell_post_reset(shell);
    }
    assert_no_alloc(|| unsafe {
        for block in 0..600 {
            let core = fd_shell_begin(shell);
            fd_core_set_num(core, (FD_NODE_BASE + (block % 16) * FD_NODE_FIELDS) as i32, (block % 10) as f64);
            fd_core_set_num(core, 4, (block / 100 % 2) as f64);
            fd_core_process_f32(core, l.as_mut_ptr(), r.as_mut_ptr(), 256);
            fd_shell_end(shell, 256);
        }
    });
    assert_eq!(violation_count(), 0);
    unsafe { fd_shell_destroy(shell) };
}
