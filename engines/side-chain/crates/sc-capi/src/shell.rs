// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The `sc_shell_*` C ABI: the engine as a plugin shell must hold it.

The same arrangement as the Trance Gate's tg_shell_* (see shell-core): the
audio thread takes the engine for one block with [`sc_shell_begin`], and every
other thread reads what it published with [`sc_shell_read`].

Side-Chain has no pattern and no edit that is not a host parameter, so its only
command is the sample rate and it keeps no view: a reader is always answered
from the latest frame, which is at most one publish interval old.

THIS FILE IS sc_shell.h: build.rs generates the header from it (cbindgen's
configuration is cbindgen/sc_shell.toml), and the doc comments on the items
below are the header's comments. What a C caller needs before any of them:

THE ENGINE BELONGS TO THE AUDIO THREAD; the Trance Gate's tg_shell.h states
the rule and this is the same arrangement. The audio thread calls sc_core_*
only on the pointer sc_shell_begin lends it, between sc_shell_begin and
sc_shell_end. Every other thread reads what the audio thread published, and
never the engine:

  audio thread                        any other thread
  ------------                        ----------------
  c = sc_shell_begin(s);              sc_shell_read(s, "ui", buf, n);
  sc_core_set_num(c, ...);
  sc_core_process_f32_split_tap(c,...)
  sc_shell_end(s, frames);

Readouts are republished a hundred times a second. begin/end allocate
nothing, take no lock and never wait. begin may be called again before end
within one block -- for MIDI, which the host delivers ahead of the block.

The Move module does not use this: Schwung calls its module on one thread.
*/

use crate::ScCore;
use shell_core::{publish_every, Bridge, Model, Text};
use std::ffi::{c_char, c_int, CStr};
use sc_core::Instance;

/* What a non-audio thread may ask for, in frame order. */
const KEYS: [&str; 3] = ["ui", "params", "stage_ms"];
/* Every readout fits. */
const TEXT_MAX: usize = crate::SC_STATE_MAX;
/* Commands the ring holds between two blocks. The sample rate is the only
 * one, and a host sets it a handful of times a session. */
const QUEUE: usize = 16;

/// What another thread may ask of the engine: the one edit the Side-Chain has
/// that is not a host parameter.
#[derive(Clone, Copy)]
pub enum Command {
    /// The host's rate, applied at the top of the next block.
    SampleRate(f64),
}

impl Model for ScCore {
    type Command = Command;
    type Frame = [Text; 3];

    fn new_frame(&self) -> [Text; 3] {
        [Text::new(TEXT_MAX), Text::new(TEXT_MAX), Text::new(TEXT_MAX)]
    }

    fn apply(&mut self, cmd: &Command) {
        match *cmd {
            Command::SampleRate(sr) => self.0.set_sample_rate(sr),
        }
    }

    fn publish(&self, f: &mut [Text; 3]) {
        for (i, key) in KEYS.iter().enumerate() {
            f[i].fill(|out| self.0.get_param(key, out));
        }
    }

    /* No view is kept; see the module comment. */
    fn restore(&mut self, _: &[Text; 3]) {}
}

pub struct ScShell(Bridge<ScCore>);

/// Allocates; the main thread only.
#[no_mangle]
pub extern "C" fn sc_shell_create(sample_rate: f64) -> *mut ScShell {
    let sr = if sample_rate > 0.0 { sample_rate } else { 44100.0 };
    let bridge = Bridge::new(ScCore(Instance::new(sr)), None, QUEUE, publish_every(sr));
    Box::into_raw(Box::new(ScShell(bridge)))
}

/// # Safety
/// `sh` is null or from `sc_shell_create`, and no thread uses it afterwards.
#[no_mangle]
pub unsafe extern "C" fn sc_shell_destroy(sh: *mut ScShell) {
    if !sh.is_null() {
        drop(Box::from_raw(sh));
    }
}

/// The host's rate, applied at the top of the next block. Any non-audio
/// thread.
///
/// # Safety
/// `sh` is null or live.
#[no_mangle]
#[allow(clippy::neg_cmp_op_on_partial_ord, reason = "a NaN rate must take the guard, and is dropped with it")]
pub unsafe extern "C" fn sc_shell_post_sample_rate(sh: *mut ScShell, sample_rate: f64) {
    let Some(sh) = sh.as_ref() else { return };
    if !(sample_rate > 0.0) {
        return;
    }
    sh.0.post(Command::SampleRate(sample_rate));
    sh.0.set_publish_every(publish_every(sample_rate));
}

/// `ui`, `params` or `stage_ms`, exactly as `sc_core_get_param` formats them,
/// NUL-terminated. Returns the length written, or -1 for any other key. Size
/// `buf` with SC_STATE_MAX. Any non-audio thread; never touches the engine.
///
/// # Safety
/// `buf` holds `buf_len` bytes; `key` is null or NUL-terminated.
#[no_mangle]
pub unsafe extern "C" fn sc_shell_read(
    sh: *mut ScShell,
    key: *const c_char,
    buf: *mut c_char,
    buf_len: c_int,
) -> c_int {
    let Some(sh) = sh.as_ref() else { return -1 };
    if key.is_null() || buf.is_null() || buf_len <= 0 {
        return -1;
    }
    let Ok(key) = CStr::from_ptr(key).to_str() else { return -1 };
    let Some(i) = KEYS.iter().position(|k| *k == key) else { return -1 };
    let out = std::slice::from_raw_parts_mut(buf as *mut u8, buf_len as usize);
    sh.0.read(|r| r.frame[i].copy_to(out))
}

/// The audio thread, at the top of a block. Allocation-free and wait-free.
///
/// # Safety
/// The audio thread only, and the pointer must not be used after
/// `sc_shell_end`.
#[no_mangle]
pub unsafe extern "C" fn sc_shell_begin(sh: *mut ScShell) -> *mut ScCore {
    match sh.as_ref() {
        Some(sh) => sh.0.begin() as *mut ScCore,
        None => std::ptr::null_mut(),
    }
}

/// The audio thread, at the end of a block of `frames`.
///
/// # Safety
/// As `sc_shell_begin`.
#[no_mangle]
pub unsafe extern "C" fn sc_shell_end(sh: *mut ScShell, frames: c_int) {
    if let Some(sh) = sh.as_ref() {
        sh.0.end(frames.max(0) as u32);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::CString;

    fn read(sh: *mut ScShell, key: &str) -> Option<String> {
        let mut buf = vec![0u8; TEXT_MAX];
        let k = CString::new(key).unwrap();
        let n = unsafe { sc_shell_read(sh, k.as_ptr(), buf.as_mut_ptr() as *mut c_char, buf.len() as c_int) };
        (n >= 0).then(|| String::from_utf8(buf[..n as usize].to_vec()).unwrap())
    }

    #[test]
    fn readouts_are_the_engines_own_and_follow_it() {
        let sh = sc_shell_create(48000.0);
        let reference = ScCore(Instance::new(48000.0));
        for key in KEYS {
            let mut want = vec![0u8; TEXT_MAX];
            let n = reference.0.get_param(key, &mut want);
            assert_eq!(read(sh, key).as_deref(), std::str::from_utf8(&want[..n as usize]).ok());
        }
        assert_eq!(read(sh, "phase"), None, "only the published keys");

        let before = read(sh, "params").unwrap();
        unsafe {
            let c = sc_shell_begin(sh);
            crate::sc_core_set_num(c, sc_core::params::Param::Depth as c_int, 0.25);
            sc_shell_end(sh, 1);
        }
        assert_eq!(read(sh, "params").unwrap(), before, "not due yet");
        unsafe {
            sc_shell_begin(sh);
            sc_shell_end(sh, 48000);
        }
        assert_ne!(read(sh, "params").unwrap(), before, "republished on the cadence");
        unsafe { sc_shell_destroy(sh) };
    }

    #[test]
    fn the_sample_rate_arrives_with_the_next_block() {
        let sh = sc_shell_create(44100.0);
        unsafe {
            sc_shell_post_sample_rate(sh, 96000.0);
            let c = sc_shell_begin(sh);
            assert_eq!(crate::sc_core_get_sample_rate(c), 96000.0);
            sc_shell_end(sh, 1);
            sc_shell_destroy(sh);
        }
    }
}
