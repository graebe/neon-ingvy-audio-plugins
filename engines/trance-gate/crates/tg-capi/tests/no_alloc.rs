// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The plugin's audio thread allocates nothing through this ABI, asserted:
 * a block as TranceGate.cpp runs it -- tg_shell_begin, the host's fifteen
 * pushed, the meter, the gate with and without the scope's tap, and the
 * frame published at tg_shell_end. The host's Slot moves, so a switch and the
 * publish it forces are in it too.
 *
 * What begin applies is everything a message thread posts: edits (a roll and
 * a whole patch among them), a rate, a paste of a slot, a bank and a patch,
 * an import, a host's state load, and the follow tg_shell_take_params posts
 * after a switch. Posting, reading and taking are the message thread's, run
 * between the measured blocks so every command really crosses, and are not
 * measured: they parse, format, and collect the frees the audio thread
 * deferred, which is allowed to allocate. tg-core's own test covers the
 * engine's half.
 *
 * THE SHELL'S ENTRY POINTS ARE DECLARED HERE, as tg_shell.h declares them:
 * src/shell.rs is a private module, so Rust cannot name them -- the plugin
 * reaches them through C as well. The signatures are the header's.
 *
 * The guard is assert_no_alloc's: it watches the thread that runs the
 * closure, and counts rather than aborts.
 */

use std::ffi::{c_char, c_int, CString};
use std::ptr::null_mut;

use tg_capi::shell::*;
use tg_capi::{tg_core_process_f32_split_tap, tg_core_set_meter, TgParam, TgTransport};

use assert_no_alloc::{assert_no_alloc, violation_count, AllocDisabler};

#[global_allocator]
static ALLOCATOR: AllocDisabler = AllocDisabler;

const NUMS: usize = TgParam::TG_P_COUNT as usize;
const FRAMES: usize = 256;

/* What the editor posts, one post at a time. */
const EDITS: &[&[&str]] = &[
    &["cursor", "3", "step", "Tie"],
    &["rate", "1/8T"],
    &["randomize", "Roll"],
    &["pattern", "F0F0", "ties", "0100"],
    &["fade", "0.7", "fade_dir", "Out"],
    &["length", "31"],
];

/* The host's fifteen on the numeric wire: slot 1, then slot 2 every other 32
 * blocks, with the levels moving every block. */
fn host(block: u32) -> [f64; NUMS] {
    let mut v = [0.0, 15.0, 7.0, 0.0, 0.0, 0.0, 0.8, 0.5, 10.0, 40.0, 0.5, 30.0, 0.0, 0.0, 0.0];
    v[TgParam::TG_P_SLOT as usize] = f64::from(block / 32 % 2);
    v[TgParam::TG_P_AMOUNT as usize] = f64::from(block % 5) / 4.0;
    v[TgParam::TG_P_ATTACK as usize] = f64::from(block % 7) * 5.0;
    v
}

fn post(sh: *mut TgShell, kv: &[&str]) {
    let owned: Vec<CString> = kv.iter().map(|s| CString::new(*s).unwrap()).collect();
    let ptrs: Vec<*const c_char> = owned.iter().map(|c| c.as_ptr()).collect();
    assert_eq!(unsafe { tg_shell_post(sh, ptrs.as_ptr(), (ptrs.len() / 2) as c_int) }, 1, "{kv:?} was queued");
}

fn read(sh: *mut TgShell, key: &str) -> String {
    let mut buf = vec![0u8; tg_capi::TG_STATE_MAX];
    let k = CString::new(key).unwrap();
    let n = unsafe { tg_shell_read(sh, k.as_ptr(), buf.as_mut_ptr().cast(), buf.len() as c_int) };
    assert!(n >= 0, "{key} is served");
    String::from_utf8(buf[..n as usize].to_vec()).unwrap()
}

fn export(sh: *mut TgShell, values: &[f64; NUMS], all: bool) -> String {
    /* TG_SLOTFILE_MAX, which lives with the shell. */
    let mut buf = vec![0u8; 16384];
    let n = unsafe {
        tg_shell_export(sh, values.as_ptr(), NUMS as c_int, c_int::from(all), buf.as_mut_ptr().cast(), buf.len() as c_int)
    };
    assert!(n > 0, "the slot exports");
    String::from_utf8(buf[..n as usize].to_vec()).unwrap()
}

#[test]
fn a_block_through_the_shell_allocates_nothing() {
    unsafe {
        let sh = tg_shell_create(48_000.0);
        let mut l = vec![0.25f32; FRAMES];
        let mut r = vec![-0.25f32; FRAMES];
        let mut sweep = vec![0f32; FRAMES];
        let mut taken = [0f64; NUMS];
        let mut followed = 0;

        /* What a copy put on the clipboard: a slot, a bank, a whole patch. */
        let slot = export(sh, &host(0), false);
        let bank = export(sh, &host(0), true);
        let state = read(sh, "state");
        let clips = [&slot, &bank, &state];

        let mut beats = 0.0;
        for block in 0..256u32 {
            let values = host(block);
            /* The message thread, between blocks. */
            match block % 16 {
                1 => post(sh, EDITS[(block / 16) as usize % EDITS.len()]),
                5 => {
                    let clip = clips[(block / 16) as usize % clips.len()];
                    let held = tg_shell_paste(sh, -1, clip.as_ptr().cast(), clip.len() as c_int, null_mut(), 0);
                    assert!(held > 0, "the paste was queued");
                }
                9 => tg_shell_post_sample_rate(sh, if block % 32 == 9 { 44_100.0 } else { 48_000.0 }),
                13 => match block / 16 % 3 {
                    0 => {
                        let file = CString::new(slot.as_str()).unwrap();
                        assert!(tg_shell_import(sh, -1, file.as_ptr(), null_mut(), 0) > 0, "the import was queued");
                    }
                    1 => {
                        let blob = CString::new(state.as_str()).unwrap();
                        assert_eq!(tg_shell_load(sh, blob.as_ptr(), values.as_ptr(), NUMS as c_int), 1);
                    }
                    _ => post(sh, &["state", &state]),
                },
                _ => {}
            }
            if block % 4 == 3 {
                for key in ["ui", "params", "state", "length"] {
                    read(sh, key);
                }
                followed += tg_shell_take_params(sh, taken.as_mut_ptr(), NUMS as c_int);
            }

            assert_no_alloc(|| {
                let core = tg_shell_begin(sh);
                tg_shell_push(sh, core, values.as_ptr(), NUMS as c_int);
                let (num, den) = if block % 64 < 32 { (4, 4) } else { (7, 8) };
                tg_core_set_meter(core, num, den);
                let t = TgTransport { running: c_int::from(block % 16 != 15), beats, bpm: 128.0 };
                let tap = if block % 2 == 0 { sweep.as_mut_ptr() } else { null_mut() };
                tg_core_process_f32_split_tap(core, l.as_mut_ptr(), r.as_mut_ptr(), tap, FRAMES as c_int, &t);
                if block % 8 == 0 {
                    tg_shell_touch(sh);
                }
                tg_shell_end(sh, FRAMES as c_int);
            });
            beats += FRAMES as f64 / 48_000.0 * 128.0 / 60.0;
            l.fill(0.25);
            r.fill(-0.25);
        }

        let n = violation_count();
        assert_eq!(n, 0, "the audio path allocated or freed {n} times");
        assert!(followed > 0, "a switch was announced, so the follow crossed too");
        tg_shell_destroy(sh);
    }
}
