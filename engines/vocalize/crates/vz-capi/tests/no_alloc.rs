// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The plugin's audio thread allocates nothing through this ABI, asserted:
 * a block between vz_shell_begin and vz_shell_end -- every parameter, Mode
 * switching back and forth, the process, a panic, and the picture's publish
 * at the end -- with the commands a message thread posts applied at the top
 * of it. Construction allocates, and is outside the guard; so is the posting,
 * which is the message thread's.
 */

use vz_capi::shell::{vz_shell_begin, vz_shell_create, vz_shell_destroy, vz_shell_end, vz_shell_post_reset,
                     vz_shell_post_sample_rate};
use vz_capi::{vz_core_on_midi, vz_core_process_f32, vz_core_set_num, vz_param_count, vz_param_denormalise};

use assert_no_alloc::{assert_no_alloc, violation_count, AllocDisabler};

#[global_allocator]
static ALLOCATOR: AllocDisabler = AllocDisabler;

#[test]
fn a_block_through_the_shell_allocates_nothing() {
    let shell = vz_shell_create(48_000.0);
    let mut l = vec![0.25f32; 512];
    let mut r = vec![-0.25f32; 512];

    for block in 0..96u32 {
        if block % 16 == 3 {
            unsafe {
                vz_shell_post_sample_rate(shell, if block % 32 == 3 { 96_000.0 } else { 48_000.0 });
                vz_shell_post_reset(shell);
            }
        }
        assert_no_alloc(|| unsafe {
            let core = vz_shell_begin(shell);
            for i in 0..vz_param_count() {
                vz_core_set_num(core, i, vz_param_denormalise(i, f64::from((block / 8 + i as u32) % 5) / 4.0));
            }
            if block % 24 == 20 {
                vz_core_on_midi(core, [0xB0, 120, 0].as_ptr(), 3);
            }
            let right = if block % 2 == 0 { r.as_mut_ptr() } else { std::ptr::null_mut() };
            vz_core_process_f32(core, l.as_mut_ptr(), right, 512);
            vz_shell_end(shell, 512);
        });
    }
    unsafe { vz_shell_destroy(shell) };

    let n = violation_count();
    assert_eq!(n, 0, "the audio path allocated or freed {n} times");
}
