/*!
The `tg_shell_*` C ABI: the engine as a plugin shell must hold it.

The engine is owned by the audio thread (see shell-core). The shell edits it
by posting commands and reads it through a published frame; the audio thread
takes it for one block at a time with [`tg_shell_begin`].

WHAT IS PUBLISHED. The four readouts the plugin reads off the audio thread --
`ui`, `params`, `state`, `length` -- formatted by the engine's own
non-allocating `get_param` into preallocated text, plus the handful of runtime
values a view needs to draw the same picture.

WHICH READOUT ANSWERS WHILE AN EDIT IS IN FLIGHT. `ui`, `state` and `length`
come from the view that includes it: a save straight after an edit must write
that edit, with or without an audio thread. `params` always comes from the
frame, because no command changes a host parameter for longer than the next
block's push -- the engine's own values are the truth there.
*/

use crate::TgCore;
use shell_core::{Bridge, Model, Text};
use std::ffi::{c_char, c_int, CStr};
use std::sync::atomic::{AtomicU64, Ordering};
use tg_core::Instance;

/* What a non-audio thread may ask for, in frame order. */
const KEYS: [&str; 4] = ["ui", "params", "state", "length"];
const PARAMS: usize = 1;
const STATE: usize = 2;

/* TG_STATE_MAX: every readout fits, the state blob being the longest. */
const TEXT_MAX: usize = 8192;
/* A pasted state and its key, with room. */
const MAX_COMMAND: usize = 2 * TEXT_MAX;
const QUEUE_BYTES: usize = 64 * 1024;
/* Republished a hundred times a second without an edit: faster than any idle
 * timer reads it, and the state blob is not formatted every block. */
const PUBLISHES_PER_SECOND: f64 = 100.0;

const CMD_PARAMS: u8 = b'P';
const CMD_SAMPLE_RATE: u8 = b'S';

/// The engine's playhead and clock, which a state blob does not carry and a
/// view needs to draw the same `ui` readout.
#[derive(Default, Clone, Copy)]
struct Runtime {
    step_pos: f64,
    advancing: bool,
    last_bpm: f32,
    ms_per_step: f32,
    sample_rate: f64,
    cursor: usize,
}

pub struct TgFrame {
    text: [Text; 4],
    rt: Runtime,
}

impl Model for TgCore {
    type Frame = TgFrame;

    fn new_frame(&self) -> TgFrame {
        TgFrame {
            text: [Text::new(TEXT_MAX), Text::new(TEXT_MAX), Text::new(TEXT_MAX), Text::new(TEXT_MAX)],
            rt: Runtime::default(),
        }
    }

    fn apply(&mut self, cmd: &[u8]) {
        let Some((&tag, body)) = cmd.split_first() else { return };
        match tag {
            CMD_PARAMS => {
                /* key \0 value \0, repeated: one edit is one command, so a
                 * cursor move and the step it aims at land in the same block. */
                let mut it = body.split(|&b| b == 0);
                while let (Some(k), Some(v)) = (it.next(), it.next()) {
                    let (Ok(k), Ok(v)) = (core::str::from_utf8(k), core::str::from_utf8(v)) else {
                        continue;
                    };
                    if !k.is_empty() {
                        self.0.set_param(k, v);
                    }
                }
            }
            CMD_SAMPLE_RATE => {
                let Some(b) = body.get(..8) else { return };
                let sr = f64::from_le_bytes(b.try_into().unwrap_or([0; 8]));
                if sr > 0.0 {
                    self.0.sample_rate = sr;
                    self.0.recalc_ms_per_step();
                }
            }
            _ => {}
        }
    }

    fn publish(&self, f: &mut TgFrame) {
        for (i, key) in KEYS.iter().enumerate() {
            f.text[i].fill(|out| self.0.get_param(key, out));
        }
        let e = &self.0;
        f.rt = Runtime {
            step_pos: e.step_pos,
            advancing: e.advancing,
            last_bpm: e.last_bpm,
            ms_per_step: e.ms_per_step,
            sample_rate: e.sample_rate,
            cursor: e.cursor,
        };
    }

    fn restore(&mut self, f: &TgFrame) {
        let e = &mut self.0;
        e.sample_rate = f.rt.sample_rate;
        e.last_bpm = f.rt.last_bpm;
        e.set_param("state", f.text[STATE].as_str());
        e.ms_per_step = f.rt.ms_per_step;
        e.step_pos = f.rt.step_pos;
        e.advancing = f.rt.advancing;
        e.cursor = f.rt.cursor.min(e.pattern().length.max(1) - 1);
    }
}

pub struct TgShell {
    bridge: Bridge<TgCore>,
    seed: AtomicU64,
}

fn publish_every(sample_rate: f64) -> u32 {
    let sr = if sample_rate > 0.0 { sample_rate } else { 44100.0 };
    (sr / PUBLISHES_PER_SECOND).max(1.0) as u32
}

/*
 * AN UNSEEDED ROLL IS GIVEN A SEED HERE, on the posting side.
 *
 * The engine's own generator would roll differently in the view than in the
 * engine, and the patch a save wrote would not be the one that played. A seed
 * picked once and carried in the command makes both roll the same pattern.
 * The hold values are the engine's (params.rs, "randomize").
 */
fn is_hold(val: &str) -> bool {
    matches!(val, "Hold" | "hold" | "0" | "Off" | "off")
}

impl TgShell {
    fn next_seed(&self) -> u32 {
        let mut z = self.seed.fetch_add(0x9E37_79B9_7F4A_7C15, Ordering::Relaxed);
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
        z ^= z >> 31;
        /* A positive i32, which is what the engine reads as a seed. */
        (z % 0x7FFF_FFFF) as u32 + 1
    }
}

unsafe fn s<'a>(p: *const c_char) -> Option<&'a str> {
    if p.is_null() {
        return None;
    }
    CStr::from_ptr(p).to_str().ok()
}

/// Allocates; the main thread only.
#[no_mangle]
pub extern "C" fn tg_shell_create(sample_rate: f64) -> *mut TgShell {
    let sr = if sample_rate > 0.0 { sample_rate } else { 44100.0 };
    let bridge = Bridge::new(
        TgCore(Instance::new(sr)),
        Some(TgCore(Instance::new(sr))),
        QUEUE_BYTES,
        MAX_COMMAND,
        publish_every(sr),
    );
    let entropy = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_nanos() as u64)
        .unwrap_or(0);
    let shell = Box::new(TgShell { bridge, seed: AtomicU64::new(0) });
    shell.seed.store(entropy ^ (&*shell as *const TgShell as u64), Ordering::Relaxed);
    Box::into_raw(shell)
}

/// # Safety
/// `sh` is null or from `tg_shell_create`, and no thread uses it afterwards.
#[no_mangle]
pub unsafe extern "C" fn tg_shell_destroy(sh: *mut TgShell) {
    if !sh.is_null() {
        drop(Box::from_raw(sh));
    }
}

/// Queue one edit: `n_pairs` key/value pairs, applied together in order.
/// Returns 1 when queued, 0 when refused (null, malformed, or too long ever
/// to be delivered). Any non-audio thread.
///
/// # Safety
/// `pairs` holds `2 * n_pairs` pointers, each null or a NUL-terminated string.
#[no_mangle]
pub unsafe extern "C" fn tg_shell_post(
    sh: *const TgShell,
    pairs: *const *const c_char,
    n_pairs: c_int,
) -> c_int {
    let Some(sh) = sh.as_ref() else { return 0 };
    if pairs.is_null() || n_pairs <= 0 {
        return 0;
    }
    let kv = std::slice::from_raw_parts(pairs, n_pairs as usize * 2);
    let mut cmd = vec![CMD_PARAMS];
    for p in kv.chunks(2) {
        let (Some(k), Some(v)) = (s(p[0]), s(p[1])) else { return 0 };
        cmd.extend_from_slice(k.as_bytes());
        cmd.push(0);
        if k == "randomize" && !is_hold(v) && tg_core::fmt::atoi(v) <= 0 {
            cmd.extend_from_slice(sh.next_seed().to_string().as_bytes());
        } else {
            cmd.extend_from_slice(v.as_bytes());
        }
        cmd.push(0);
    }
    sh.bridge.post(&cmd) as c_int
}

/// The host's rate, applied at the top of the next block. Any non-audio
/// thread.
///
/// # Safety
/// `sh` is null or live.
#[no_mangle]
pub unsafe extern "C" fn tg_shell_post_sample_rate(sh: *const TgShell, sample_rate: f64) {
    let Some(sh) = sh.as_ref() else { return };
    if !(sample_rate > 0.0) {
        return;
    }
    let mut cmd = vec![CMD_SAMPLE_RATE];
    cmd.extend_from_slice(&sample_rate.to_le_bytes());
    sh.bridge.post(&cmd);
    sh.bridge.set_publish_every(publish_every(sample_rate));
}

/// One readout -- `ui`, `params`, `state` or `length` -- as the engine
/// formats it, NUL-terminated. Returns the length written, or -1 for any
/// other key. Any non-audio thread; never touches the engine.
///
/// # Safety
/// `buf` holds `buf_len` bytes.
#[no_mangle]
pub unsafe extern "C" fn tg_shell_read(
    sh: *const TgShell,
    key: *const c_char,
    buf: *mut c_char,
    buf_len: c_int,
) -> c_int {
    let Some(sh) = sh.as_ref() else { return -1 };
    if buf.is_null() || buf_len <= 0 {
        return -1;
    }
    let Some(key) = s(key) else { return -1 };
    let Some(i) = KEYS.iter().position(|k| *k == key) else { return -1 };
    let out = std::slice::from_raw_parts_mut(buf as *mut u8, buf_len as usize);
    sh.bridge.read(|r| match r.pending {
        Some(view) if i != PARAMS => view.0.get_param(key, out),
        _ => r.frame.text[i].copy_to(out),
    })
}

/// The audio thread, at the top of a block: applies every queued edit and
/// lends out the engine for this block's `tg_core_*` calls. Allocation-free
/// and wait-free.
///
/// # Safety
/// The audio thread only, and the pointer must not be used after
/// `tg_shell_end`.
#[no_mangle]
pub unsafe extern "C" fn tg_shell_begin(sh: *const TgShell) -> *mut TgCore {
    match sh.as_ref() {
        Some(sh) => sh.bridge.begin() as *mut TgCore,
        None => std::ptr::null_mut(),
    }
}

/// The audio thread, at the end of a block of `frames`: publishes the
/// readouts when an edit landed or the cadence is due.
///
/// # Safety
/// As `tg_shell_begin`.
#[no_mangle]
pub unsafe extern "C" fn tg_shell_end(sh: *const TgShell, frames: c_int) {
    if let Some(sh) = sh.as_ref() {
        sh.bridge.end(frames.max(0) as u32);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::CString;

    fn post(sh: *const TgShell, kv: &[&str]) -> c_int {
        let owned: Vec<CString> = kv.iter().map(|s| CString::new(*s).unwrap()).collect();
        let ptrs: Vec<*const c_char> = owned.iter().map(|c| c.as_ptr()).collect();
        unsafe { tg_shell_post(sh, ptrs.as_ptr(), (ptrs.len() / 2) as c_int) }
    }

    fn read(sh: *const TgShell, key: &str) -> String {
        let mut buf = vec![0u8; TEXT_MAX];
        let k = CString::new(key).unwrap();
        let n = unsafe { tg_shell_read(sh, k.as_ptr(), buf.as_mut_ptr() as *mut c_char, buf.len() as c_int) };
        assert!(n >= 0, "{key} is served");
        String::from_utf8(buf[..n as usize].to_vec()).unwrap()
    }

    fn block(sh: *const TgShell) {
        unsafe {
            tg_shell_begin(sh);
            tg_shell_end(sh, 64);
        }
    }

    #[test]
    fn an_edit_is_readable_before_and_after_the_audio_thread_applies_it() {
        let sh = tg_shell_create(48000.0);
        let before = read(sh, "state");
        assert_eq!(post(sh, &["cursor", "0", "step", "0"]), 1);
        let pending = read(sh, "state");
        assert_ne!(pending, before, "the view already has the edit");
        block(sh);
        assert_eq!(read(sh, "state"), pending, "and the engine agrees once it has run");
        unsafe { tg_shell_destroy(sh) };
    }

    #[test]
    fn an_unseeded_roll_plays_the_pattern_the_view_predicted() {
        let sh = tg_shell_create(44100.0);
        post(sh, &["randomize", ""]);
        let predicted = read(sh, "state");
        block(sh);
        assert_eq!(read(sh, "state"), predicted);
        post(sh, &["randomize", "Hold"]);
        block(sh);
        assert_eq!(read(sh, "state"), predicted, "a hold rolls nothing");
        unsafe { tg_shell_destroy(sh) };
    }

    #[test]
    fn only_the_published_keys_are_served() {
        let sh = tg_shell_create(44100.0);
        let mut buf = [0u8; 16];
        let k = CString::new("phase").unwrap();
        assert_eq!(unsafe { tg_shell_read(sh, k.as_ptr(), buf.as_mut_ptr() as *mut c_char, 16) }, -1);
        assert_eq!(read(sh, "length"), "15");
        unsafe { tg_shell_destroy(sh) };
    }

    #[test]
    fn the_sample_rate_arrives_with_the_next_block() {
        let sh = tg_shell_create(44100.0);
        unsafe {
            tg_shell_post_sample_rate(sh, 96000.0);
            let c = tg_shell_begin(sh);
            assert_eq!(crate::tg_core_get_sample_rate(c), 96000.0);
            tg_shell_end(sh, 1);
            tg_shell_destroy(sh);
        }
    }
}
