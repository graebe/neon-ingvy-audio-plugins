// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What the audio thread calls through the C ABI allocates nothing: a block
 * between sf_shell_begin and sf_shell_end, for every product, with the
 * commands it applies and the frame it publishes -- the analyzer's rings
 * copied out included.
 */

use assert_no_alloc::{assert_no_alloc, violation_count, AllocDisabler};
use sf_capi::shell::*;
use sf_capi::*;

#[global_allocator]
static ALLOCATOR: AllocDisabler = AllocDisabler;

#[test]
fn a_block_through_the_shell_allocates_nothing() {
    for shape in 1..=9 {
        let shell = sf_shell_create(shape, 48_000.0);
        let mut l = vec![0.25f32; 256];
        let mut r = vec![-0.25f32; 256];
        unsafe {
            sf_shell_post_watching(shell, true);
            sf_shell_post_sample_rate(shell, 96_000.0);
            sf_shell_post_reset(shell);
        }
        assert_no_alloc(|| unsafe {
            for block in 0..300 {
                let core = sf_shell_begin(shell);
                sf_core_set_num(core, 0, 100.0 + (block % 50) as f64 * 100.0);
                sf_core_set_num(core, 2, (block / 30 % 8) as f64);
                sf_core_process_f32(core, l.as_mut_ptr(), r.as_mut_ptr(), 256);
                sf_shell_end(shell, 256);
            }
        });
        assert_eq!(violation_count(), 0, "shape {shape}");
        unsafe { sf_shell_destroy(shell) };
    }
}
