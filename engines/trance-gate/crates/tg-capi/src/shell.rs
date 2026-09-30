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
use tg_core::{Instance, Playhead};

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
 * timer reads it. The state blob is formatted only when it moved -- see
 * `publish`. */
const PUBLISHES_PER_SECOND: f64 = 100.0;

const CMD_PARAMS: u8 = b'P';
const CMD_SAMPLE_RATE: u8 = b'S';

pub struct TgFrame {
    text: [Text; 4],
    rt: Playhead,
    /* The engine's state revision `text[STATE]` was formatted at; None until
     * it has been. Per frame, because each of the three is refreshed in turn. */
    state_rev: Option<u64>,
}

impl Model for TgCore {
    type Frame = TgFrame;

    fn new_frame(&self) -> TgFrame {
        TgFrame {
            text: [Text::new(TEXT_MAX), Text::new(TEXT_MAX), Text::new(TEXT_MAX), Text::new(TEXT_MAX)],
            rt: Playhead::default(),
            state_rev: None,
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
                self.0.set_sample_rate(sr);
            }
            _ => {}
        }
    }

    /*
     * THE STATE BLOB IS FORMATTED ONLY WHEN IT CAN HAVE CHANGED.
     *
     * It is the whole patch, up to ~5 KB, and this runs on the audio thread a
     * hundred times a second; formatting it every time was ~40 us a publish for
     * a patch that sits between two edits for most of a session. The engine's
     * revision says when the frame's copy is stale -- tg-core's
     * `an_unmoved_revision_means_an_unchanged_state` holds it to that. The
     * other three readouts move on their own (the playhead, the step
     * duration) and are formatted every time, as before.
     */
    fn publish(&self, f: &mut TgFrame) {
        let rev = self.0.state_rev();
        for (i, key) in KEYS.iter().enumerate() {
            if i == STATE && f.state_rev == Some(rev) {
                continue;
            }
            f.text[i].fill(|out| self.0.get_param(key, out));
        }
        f.state_rev = Some(rev);
        f.rt = self.0.playhead();
    }

    fn restore(&mut self, f: &TgFrame) {
        self.0.mirror(f.text[STATE].as_str(), &f.rt);
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
 * What counts as a hold is the engine's own answer, not a copy of it.
 */
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
        if k == "randomize" && !tg_core::params::randomize_holds(v) && tg_core::fmt::atoi(v) <= 0 {
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

/// The audio thread: publish at the end of this block, off the cadence --
/// for a change a reader must not see late. Allocation-free.
///
/// # Safety
/// As `tg_shell_begin`.
#[no_mangle]
pub unsafe extern "C" fn tg_shell_touch(sh: *const TgShell) {
    if let Some(sh) = sh.as_ref() {
        sh.bridge.touch();
    }
}

/// The audio thread, at the end of a block of `frames`: publishes the
/// readouts when an edit landed, the block was touched, or the cadence is due.
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
    fn a_pushed_change_reaches_the_published_state_and_nothing_else_moves_it() {
        let sh = tg_shell_create(48000.0);
        let period = publish_every(48000.0) as c_int;
        audio_block(sh, period);
        let steady = read(sh, "state");
        for _ in 0..10 {
            audio_block(sh, period);
            assert_eq!(read(sh, "state"), steady);
        }
        unsafe {
            let c = tg_shell_begin(sh);
            for (i, v) in STEADY.iter().enumerate() {
                crate::tg_core_set_num(c, i as c_int, *v);
            }
            crate::tg_core_set_num(c, 6, 0.25); /* Amount */
            tg_shell_end(sh, period);
        }
        let changed = read(sh, "state");
        assert_ne!(changed, steady);
        assert!(changed.contains("\"amount\":0.250"), "{changed}");
        /* All three frames of the triple buffer come round again, each with
         * the change -- not one of them still holding the old blob. */
        audio_block(sh, period);
        for _ in 0..6 {
            unsafe {
                let c = tg_shell_begin(sh);
                crate::tg_core_set_num(c, 6, 0.25);
                tg_shell_end(sh, period);
            }
            assert_eq!(read(sh, "state"), changed);
        }
        unsafe { tg_shell_destroy(sh) };
    }

    /* The fifteen values PushParams writes, in wire order: a steady state, so
     * pushing them again changes nothing. */
    const STEADY: [f64; 15] = [0.0, 127.0, 7.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.6, 16.0, 1.0, 16.0, 1.0, 0.0, 0.0];

    /* The heaviest realistic patch: eight slots of 128 random steps, accented. */
    fn heavy(sh: *const TgShell) {
        for slot in 0..8 {
            let slot = slot.to_string();
            post(sh, &["slot", &slot, "length", "127", "randomize", "12345"]);
            for i in (0..128).step_by(3) {
                post(sh, &["cursor", &i.to_string(), "step_amount", "0.5"]);
            }
        }
        post(sh, &["slot", "0"]);
    }

    /* One audio block as the plugin runs it: edits land, PushParams writes all
     * fifteen, the readouts are published if the cadence says so. */
    fn audio_block(sh: *const TgShell, frames: c_int) {
        unsafe {
            let c = tg_shell_begin(sh);
            for (i, v) in STEADY.iter().enumerate() {
                crate::tg_core_set_num(c, i as c_int, *v);
            }
            tg_shell_end(sh, frames);
        }
    }

    /*
     * WHAT A PUBLISH COSTS THE AUDIO THREAD, with the heaviest patch -- a
     * measurement, not a check, so it is run by hand:
     *
     *     cargo test -p tg-capi --release -- --ignored --nocapture publish_cost
     *
     * Every block here is one publish period long, so each one publishes.
     */
    #[test]
    #[ignore = "a measurement; run with --release --ignored --nocapture"]
    fn publish_cost() {
        let sh = tg_shell_create(48000.0);
        heavy(sh);
        let period = publish_every(48000.0) as c_int;
        audio_block(sh, period);
        let bytes = read(sh, "state").len();
        let n = 20_000;
        for _ in 0..1000 {
            audio_block(sh, period);
        }
        let t = std::time::Instant::now();
        for _ in 0..n {
            audio_block(sh, period);
        }
        let per = t.elapsed().as_nanos() as f64 / n as f64;
        println!("publish_cost: {per:.0} ns per published block, state blob {bytes} bytes");
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
