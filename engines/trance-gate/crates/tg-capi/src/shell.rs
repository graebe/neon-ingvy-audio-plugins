// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The `tg_shell_*` C ABI: the engine as a plugin shell must hold it.

The engine is owned by the audio thread (see shell-core). The shell edits it
by posting commands and reads it through a published frame; the audio thread
takes it for one block at a time with [`tg_shell_begin`].

WHAT IS POSTED. Every text that arrives through this C ABI -- an edit's
key/value pairs, a host's blob, the clipboard, a slot file -- is read and
checked here, on the posting thread, and what crosses to the audio thread is
the result: an `Edit`, a `Patch`, a `Clip` (see [`Command`]). The audio thread
applies values; it never parses.

WHAT IS PUBLISHED. The four readouts the plugin reads off the audio thread --
`ui`, `params`, `state`, `length` -- formatted by the engine's own
non-allocating `get_param` into preallocated text, plus the handful of runtime
values a view needs to draw the same picture.

WHICH READOUT ANSWERS WHILE AN EDIT IS IN FLIGHT. `ui`, `state` and `length`
come from the view that includes it: a save straight after an edit must write
that edit, with or without an audio thread. `params` always comes from the
frame -- the engine's live values, host pushes included.

THE HOST'S PARAMETERS MIRROR THE CURRENT SLOT. Every parameter but Slot is per
slot in the engine, so the host's fifteen are a window onto one slot, and the
two have to be kept pointing at the same one. See `Mirror`.
*/

use crate::TgCore;
use atomic_float::AtomicF64;
use shell_core::{publish_every, Bridge, Model, Shared, Text};
use std::ffi::{c_char, c_int, CStr};
use std::sync::atomic::{AtomicBool, AtomicI32, AtomicU32, AtomicU64, Ordering};
use tg_core::edit::Edit;
use tg_core::params::Param;
use tg_core::paste::{Clip, Holds, Refused};
use tg_core::slotfile::{Error as SlotFileError, Kind, SlotFile};
use tg_core::state::Patch;
use tg_core::Playhead;

/* What a non-audio thread may ask for, in frame order. */
const KEYS: [&str; 4] = ["ui", "params", "state", "length"];
const PARAMS: usize = 1;
const STATE: usize = 2;

/* TG_STATE_MAX: every readout fits, the state blob being the longest. */
const TEXT_MAX: usize = 8192;
/* Commands the ring holds between two blocks: a burst of edits and a load,
 * with room. More wait on the posting side, in order. */
const QUEUE: usize = 256;

/// The host parameters, TG_P_COUNT of them.
pub(crate) const NUMS: usize = 15;

/// What the shell's other threads ask of the engine, each one read and
/// checked where it was posted. Anything longer than a few words rides in a
/// `Shared`, which the bridge's collector frees off the audio thread.
#[derive(Clone)]
pub enum Command {
    /// One post's pairs, read, applied together and in order: a cursor move
    /// and the step it aims at land in the same block.
    Edits(Shared<Vec<Change>>),
    /// The host's rate.
    SampleRate(f64),
    /// A host's state load; see [`Load`].
    Load(Shared<Load>),
    /// The editor's paste: a slot, a bank or a whole patch -- or an imported
    /// file, whose text is pasted as its clip. A slot goes into `slot`
    /// (0-based), carried here because the host's Slot may move in the very
    /// block the paste is applied in, after it; out of range is the engine's
    /// current slot. The host then follows the current slot.
    Paste { slot: usize, clip: Shared<Clip> },
}

/// One pair of a post, read.
pub enum Change {
    Edit(Edit),
    /// `state`: a whole patch. The editor never posts one -- a host's goes
    /// through `tg_shell_load` -- but `tg_shell_post` takes every key
    /// `tg_core_set_param` does.
    Patch(Box<Patch>),
}

/*
 * A STATE LOAD, AND THE ORDER IS THE POINT. The blob first -- every slot's
 * pattern and sound -- then the host's own values, which are the current
 * slot's and win over the blob's rounded copy of them, so a project reopens
 * with exactly the parameters it saved. A blob from before slots had sounds of
 * their own carries ONE sound, and the host's values are that sound: it goes
 * into all eight slots.
 */
/// A host's state load, read: what [`Command::Load`] carries.
pub struct Load {
    /// The blob, read; `None` when there was none or it was no patch.
    patch: Option<Patch>,
    /// The host's fifteen, on the numeric wire.
    values: [f64; NUMS],
    /// The blob predates every slot having its own sound -- or could not be
    /// read at all, which has always counted the same.
    spread: bool,
}

#[derive(Clone)]
pub struct TgFrame {
    text: [Text; 4],
    rt: Playhead,
    /* One cycle in ms, for the scope's axis. */
    cycle_ms: f64,
    /* The engine's state revision `text[STATE]` was formatted at; None until
     * it has been. Per frame, because each of the three is refreshed in turn. */
    state_rev: Option<u64>,
    /* The current slot's values on the numeric wire: what the host's
     * parameters are told after a switch. */
    nums: [f64; NUMS],
    /* The engine's paste count, so a view replaying a paste counts on from it. */
    recalls: u32,
}

impl Model for TgCore {
    type Command = Command;
    type Frame = TgFrame;

    fn new_frame(&self) -> TgFrame {
        TgFrame {
            text: [Text::new(TEXT_MAX), Text::new(TEXT_MAX), Text::new(TEXT_MAX), Text::new(TEXT_MAX)],
            rt: Playhead::default(),
            cycle_ms: 0.0,
            state_rev: None,
            nums: [0.0; NUMS],
            recalls: 0,
        }
    }

    fn apply(&mut self, cmd: &Command) {
        match cmd {
            Command::Edits(changes) => {
                for change in changes.iter() {
                    match change {
                        Change::Edit(edit) => self.engine.apply_edit(edit),
                        Change::Patch(patch) => self.engine.load(patch),
                    }
                }
            }
            Command::SampleRate(sample_rate) => self.engine.set_sample_rate(*sample_rate),
            Command::Load(load) => {
                if let Some(patch) = &load.patch {
                    self.engine.load(patch);
                }
                for (i, &v) in load.values.iter().enumerate() {
                    if let Some(p) = Param::from_i32(i as i32) {
                        self.engine.set_num(p, v);
                    }
                }
                if load.spread {
                    self.engine.spread_sound();
                }
            }
            Command::Paste { slot, clip } => {
                self.engine.apply_clip(*slot, clip);
                self.recalls = self.recalls.wrapping_add(1);
            }
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
        let rev = self.engine.state_rev();
        for (i, key) in KEYS.iter().enumerate() {
            if i == STATE && f.state_rev == Some(rev) {
                continue;
            }
            f.text[i].fill(|out| self.engine.get_param(key, out));
        }
        f.state_rev = Some(rev);
        f.nums = self.engine.numbers();
        f.recalls = self.recalls;
        f.rt = self.engine.playhead();
        f.cycle_ms = crate::scope_cycle_ms(&self.engine);
    }

    fn restore(&mut self, f: &TgFrame) {
        self.engine.mirror(f.text[STATE].as_str(), &f.rt);
        self.recalls = f.recalls;
    }
}

pub struct TgShell {
    bridge: Bridge<TgCore>,
    seed: AtomicU64,
    mirror: Mirror,
}

/*
 * THE HOST'S PARAMETERS AND THE CURRENT SLOT, KEPT ON THE SAME SLOT.
 *
 * Every parameter but Slot belongs to a slot, and the host's are pushed into
 * the engine at the top of every block. Two rules keep them from writing one
 * slot's values over another's:
 *
 * - A VALUE IS PUSHED WHEN THE HOST MOVED IT, not every block. Automation, a
 *   knob, a typed value: each moves the host's parameter and so lands in the
 *   current slot. A parameter the host has not moved is never re-asserted, so
 *   the slot just switched to keeps its own values while the host still holds
 *   the last slot's.
 * - ON A SWITCH THE ENGINE WINS, and the host follows. The block the host's
 *   Slot moves -- automation, the editor, the host's own UI -- pushes nothing
 *   else: whatever the host holds is the slot being left. The frame with the
 *   new slot is published at the block's end and the main thread is told
 *   (`tg_shell_take_params`) to move every host parameter to it. A paste is the
 *   same: the pasted patch's current slot is what the host must show.
 *
 * A state load is not a switch: it is a command (`Command::Load`) carrying the
 * host's values with the blob, so the next block finds nothing the host moved.
 *
 * The audio thread's own fields are atomics only because the shell is shared;
 * `sync` is the one that crosses threads.
 */
struct Mirror {
    pushed_slot: AtomicI32,
    pushed: [AtomicF64; NUMS],
    recalls: AtomicU32,
    moved: AtomicBool,
    sync: AtomicBool,
}

impl Mirror {
    fn new() -> Self {
        Mirror {
            pushed_slot: AtomicI32::new(-1),
            /* NaN: nothing pushed yet, so the first block pushes everything. */
            pushed: core::array::from_fn(|_| AtomicF64::new(f64::NAN)),
            recalls: AtomicU32::new(0),
            moved: AtomicBool::new(false),
            sync: AtomicBool::new(false),
        }
    }
}

/// What one push of the host's values does to the engine: worked out apart
/// from doing it, so a save can write what the next block WILL hold.
struct Plan {
    switched: bool,
    pasted: bool,
    /// Which values the host moved since the last push.
    moved: [bool; NUMS],
}

impl Plan {
    fn apply(&self, inst: &mut tg_core::Instance, values: &[f64; NUMS]) {
        inst.set_num(Param::Slot, values[Param::Slot as usize]);
        /* On a switch or a paste what the host holds is stale -- the slot being
         * left, or the patch before the paste -- so nothing else is written. */
        if self.switched || self.pasted {
            return;
        }
        for (i, &v) in values.iter().enumerate().skip(1) {
            if let (true, Some(p)) = (self.moved[i], Param::from_i32(i as i32)) {
                inst.set_num(p, v);
            }
        }
    }
}

impl Mirror {
    fn plan(&self, values: &[f64; NUMS], recalls: u32) -> Plan {
        let mut pushed_slot = self.pushed_slot.load(Ordering::Relaxed);
        /* Compared as bits, as the engine compares a sound: a change the
         * saved blob could show is never missed, and `-0.0 == 0.0` would. */
        let mut moved = [false; NUMS];
        for (i, v) in values.iter().enumerate() {
            moved[i] = self.pushed[i].load(Ordering::Relaxed).to_bits() != v.to_bits();
        }
        Plan {
            switched: slot_moved(&mut pushed_slot, values[Param::Slot as usize] as i32),
            pasted: self.recalls.load(Ordering::Relaxed) != recalls,
            moved,
        }
    }

    /// The audio thread, after a push: these values are now what the host
    /// held when the engine last heard from it.
    fn commit(&self, values: &[f64; NUMS], recalls: u32) {
        self.pushed_slot.store(values[Param::Slot as usize] as i32, Ordering::Relaxed);
        for (i, &v) in values.iter().enumerate() {
            self.pushed[i].store(v, Ordering::Relaxed);
        }
        self.recalls.store(recalls, Ordering::Relaxed);
    }
}

/// Whether the slot moved since the last push. A first push is a starting
/// point rather than a move.
pub(crate) fn slot_moved(pushed: &mut i32, slot: i32) -> bool {
    let moved = *pushed >= 0 && slot != *pushed;
    *pushed = slot;
    moved
}

impl TgShell {
    /// One pair of a post, read: `None` for a pair that changes nothing -- a
    /// key the engine does not know, a roll that holds, a `state` that is no
    /// patch.
    fn change(&self, key: &str, val: &str) -> Option<Change> {
        if key == "state" {
            return Patch::parse(val).ok().map(|patch| Change::Patch(Box::new(patch)));
        }
        Some(Change::Edit(match Edit::parse(key, val)? {
            Edit::Randomize(None) => Edit::Randomize(Some(self.next_seed())),
            edit => edit,
        }))
    }

    /// A payload for a command, freed by the bridge's collector.
    fn shared<T: Send + Sync + 'static>(&self, value: T) -> Shared<T> {
        Shared::new(self.bridge.handle(), value)
    }

    /*
     * AN UNSEEDED ROLL IS GIVEN A SEED HERE, on the posting side.
     *
     * The engine's own generator would roll differently in the view than in
     * the engine, and the patch a save wrote would not be the one that
     * played. A seed picked once and carried in the command makes both roll
     * the same pattern. What counts as a hold is the engine's own answer
     * (`Edit::parse`), not a copy of it.
     */
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
    let bridge = Bridge::new(TgCore::new(sr), Some(TgCore::new(sr)), QUEUE, publish_every(sr));
    let entropy = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_nanos() as u64)
        .unwrap_or(0);
    let shell = Box::new(TgShell { bridge, seed: AtomicU64::new(0), mirror: Mirror::new() });
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

/// Queue one edit: `n_pairs` key/value pairs, read here and applied together
/// in order. Returns 1 when queued, 0 when refused (a null, or a key or value
/// that is not text). Any non-audio thread.
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
    let mut changes = Vec::with_capacity(kv.len() / 2);
    for p in kv.chunks(2) {
        let (Some(k), Some(v)) = (s(p[0]), s(p[1])) else { return 0 };
        changes.extend(sh.change(k, v));
    }
    sh.bridge.post(Command::Edits(sh.shared(changes)));
    1
}

/// The host's rate, applied at the top of the next block. Any non-audio
/// thread.
///
/// # Safety
/// `sh` is null or live.
#[no_mangle]
#[allow(clippy::neg_cmp_op_on_partial_ord, reason = "a NaN rate must take the guard, and is dropped with it")]
pub unsafe extern "C" fn tg_shell_post_sample_rate(sh: *const TgShell, sample_rate: f64) {
    let Some(sh) = sh.as_ref() else { return };
    if !(sample_rate > 0.0) {
        return;
    }
    sh.bridge.post(Command::SampleRate(sample_rate));
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
        Some(view) if i != PARAMS => view.engine.get_param(key, out),
        _ => r.frame.text[i].copy_to(out),
    })
}

/// The state blob a save must write: the engine's, every queued edit
/// included, and the host's `values` (`n` of them, as `tg_shell_push` takes
/// them) applied as the next block will apply them -- so a project saved
/// before any audio has run still holds the parameters the host shows, in the
/// slot they belong to. Returns the length written (NUL-terminated), or -1.
/// Any non-audio thread; allocates.
///
/// # Safety
/// `values` holds `n` doubles; `buf` holds `buf_len` bytes.
#[no_mangle]
pub unsafe extern "C" fn tg_shell_save(
    sh: *const TgShell,
    values: *const f64,
    n: c_int,
    buf: *mut c_char,
    buf_len: c_int,
) -> c_int {
    let Some(sh) = sh.as_ref() else { return -1 };
    if values.is_null() || n < NUMS as c_int || buf.is_null() || buf_len <= 0 {
        return -1;
    }
    let values: &[f64; NUMS] = &*(values as *const [f64; NUMS]);
    let out = std::slice::from_raw_parts_mut(buf as *mut u8, buf_len as usize);
    match sh.next(values) {
        Some(next) => next.get_param("state", out),
        None => -1,
    }
}

/*
 * THE ENGINE AS THE NEXT BLOCK WILL HOLD IT: what it published, every queued
 * edit, and the host's values pushed as the next push will push them. Built on
 * the calling thread, from text, for a reader that needs more than a readout --
 * a save, an export. Allocates.
 */
impl TgShell {
    fn next(&self, values: &[f64; NUMS]) -> Option<tg_core::Instance> {
        let mut state = vec![0u8; TEXT_MAX];
        let (len, rt, recalls) = self.bridge.read(|r| match r.pending {
            Some(view) => (view.engine.get_param("state", &mut state), view.engine.playhead(), view.recalls),
            None => (r.frame.text[STATE].copy_to(&mut state), r.frame.rt, r.frame.recalls),
        });
        let text = state_text(&state, len)?;
        let mut next = tg_core::Instance::new(rt.sample_rate);
        next.mirror(text, &rt);
        self.mirror.plan(values, recalls).apply(&mut next, values);
        Some(next)
    }
}

/* A negative length is the reader's "nothing", never an empty patch: mirrored,
 * "" leaves a default Instance, and a save or an export would write that over
 * the person's patch. Nothing to read is nothing to write. */
fn state_text(state: &[u8], len: c_int) -> Option<&str> {
    let len = usize::try_from(len).ok()?;
    core::str::from_utf8(state.get(..len)?).ok()
}

/// The current slot as a slot file (`all` 0), or every slot as a bank (`all`
/// 1), as the next block will hold them -- the host's `values` included, as
/// for `tg_shell_save`. Returns the length written (NUL-terminated), or -1.
/// Any non-audio thread; allocates.
///
/// # Safety
/// `values` holds `n` doubles; `buf` holds `buf_len` bytes.
#[no_mangle]
pub unsafe extern "C" fn tg_shell_export(
    sh: *const TgShell,
    values: *const f64,
    n: c_int,
    all: c_int,
    buf: *mut c_char,
    buf_len: c_int,
) -> c_int {
    let Some(sh) = sh.as_ref() else { return -1 };
    if values.is_null() || n < NUMS as c_int || buf.is_null() || buf_len <= 0 {
        return -1;
    }
    let values: &[f64; NUMS] = &*(values as *const [f64; NUMS]);
    let out = std::slice::from_raw_parts_mut(buf as *mut u8, buf_len as usize);
    let kind = if all != 0 { Kind::Bank } else { Kind::Slot };
    match sh.next(values) {
        Some(next) => next.export(kind, out),
        None => -1,
    }
}

/// Import a slot file: checked here, whole, and queued only when good -- a
/// slot file replaces the current slot, a bank all eight, at the top of the
/// next block, after which the host's parameters follow the current slot
/// (`tg_shell_take_params`). Returns 1 for a slot, 2 for a bank, or 0 with the
/// reason, in words, NUL-terminated in `err` (may be null). Any non-audio
/// thread.
///
/// `slot` (0-based) is where a slot file goes, carried in the command for the
/// reason `tg_shell_paste` carries it. Out of range is the engine's current
/// slot.
///
/// # Safety
/// `text` is null or NUL-terminated; `err` holds `err_len` bytes.
#[no_mangle]
pub unsafe extern "C" fn tg_shell_import(
    sh: *const TgShell,
    slot: c_int,
    text: *const c_char,
    err: *mut c_char,
    err_len: c_int,
) -> c_int {
    let Some(sh) = sh.as_ref() else { return 0 };
    /* Bytes that are not text are not a slot file either. */
    let text = if text.is_null() { "" } else {
        match CStr::from_ptr(text).to_str() {
            Ok(t) => t,
            Err(_) => return refuse(err, err_len, |w| SlotFileError::NotAFile.describe(w)),
        }
    };
    let file = match SlotFile::parse(text) {
        Ok(file) => file,
        Err(e) => return refuse(err, err_len, |w| e.describe(w)),
    };
    let kind = file.kind();
    sh.bridge.post(Command::Paste { slot: slot_index(slot), clip: sh.shared(Clip::File(file)) });
    if kind == Kind::Slot { 1 } else { 2 }
}

/* The slot a paste or an import names, 0-based. Anything out of range -- a
 * negative one included -- the engine reads as its current slot. */
fn slot_index(slot: c_int) -> usize {
    usize::try_from(slot).unwrap_or(usize::MAX)
}

/// Why a text was refused, in words, into `err` (which may be null),
/// NUL-terminated and cut to fit -- and 0, which is how every refusal is
/// answered.
///
/// # Safety
/// `err` is null or holds `err_len` bytes.
unsafe fn refuse(
    err: *mut c_char,
    err_len: c_int,
    why: impl FnOnce(&mut dyn core::fmt::Write) -> core::fmt::Result,
) -> c_int {
    if !err.is_null() && err_len > 0 {
        let out = std::slice::from_raw_parts_mut(err as *mut u8, err_len as usize);
        let mut b = tg_core::fmt::Buf::new(out);
        let _ = why(&mut b);
        b.finish();
    }
    0
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
/// A slot switch pushed this block is announced to the main thread only now,
/// once a frame holding the new slot's values is out.
///
/// # Safety
/// As `tg_shell_begin`.
#[no_mangle]
pub unsafe extern "C" fn tg_shell_end(sh: *const TgShell, frames: c_int) {
    if let Some(sh) = sh.as_ref() {
        sh.bridge.end(frames.max(0) as u32);
        if sh.mirror.moved.swap(false, Ordering::Relaxed) {
            sh.mirror.sync.store(true, Ordering::Release);
        }
    }
}

/// The audio thread, between begin and end: the host's fifteen parameters,
/// in `tg_param_t` order and on the numeric wire (`tg_core_set_num`'s units),
/// into `core`, the engine `tg_shell_begin` lent. See `Mirror`.
/// Allocation-free.
///
/// # Safety
/// As `tg_shell_begin`; `core` is the pointer it returned this block, and
/// `values` holds `n` doubles.
#[no_mangle]
pub unsafe extern "C" fn tg_shell_push(sh: *const TgShell, core: *mut TgCore, values: *const f64, n: c_int) {
    let (Some(sh), Some(core)) = (sh.as_ref(), core.as_mut()) else { return };
    if values.is_null() || n < NUMS as c_int {
        return;
    }
    let values: &[f64; NUMS] = &*(values as *const [f64; NUMS]);
    let m = &sh.mirror;
    let rev = core.engine.state_rev();
    let plan = m.plan(values, core.recalls);
    plan.apply(&mut core.engine, values);
    m.commit(values, core.recalls);
    let (switched, pasted) = (plan.switched, plan.pasted);
    if switched || pasted {
        /* Published at this block's end, whatever the cadence: the main thread
         * reads the new slot's values the moment it hears of the switch. */
        m.moved.store(true, Ordering::Relaxed);
        sh.bridge.touch();
    } else if core.engine.state_rev() != rev {
        /* A value is in the saved blob, so a change to it is published at
         * once: a save straight after it must have it. */
        sh.bridge.touch();
    }
}

/// The main thread: once per published switch (or paste), 1 and the current
/// slot's fifteen values on the numeric wire, into `out` (`n` >= 15) --
/// every host parameter must now be moved to these. Otherwise 0.
///
/// # Safety
/// `sh` is null or live; `out` holds `n` doubles.
#[no_mangle]
pub unsafe extern "C" fn tg_shell_take_params(sh: *const TgShell, out: *mut f64, n: c_int) -> c_int {
    let Some(sh) = sh.as_ref() else { return 0 };
    if out.is_null() || n < NUMS as c_int || !sh.mirror.sync.swap(false, Ordering::AcqRel) {
        return 0;
    }
    let nums = sh.bridge.read(|r| match r.pending {
        Some(view) => view.engine.numbers(),
        None => r.frame.nums,
    });
    std::slice::from_raw_parts_mut(out, NUMS).copy_from_slice(&nums);
    1
}

/// A host's state load, as one edit: `blob` (empty for none) and then the
/// host's own fifteen values -- the current slot's, on the numeric wire --
/// which win over the blob's for that slot. A blob from before every slot
/// had its own sound puts those values in all eight. Any non-audio thread;
/// returns 1 when queued.
///
/// # Safety
/// `blob` is null or NUL-terminated; `values` holds `n` doubles.
#[no_mangle]
pub unsafe extern "C" fn tg_shell_load(sh: *const TgShell, blob: *const c_char, values: *const f64, n: c_int) -> c_int {
    let Some(sh) = sh.as_ref() else { return 0 };
    if values.is_null() || n < NUMS as c_int {
        return 0;
    }
    let values = *(values as *const [f64; NUMS]);
    /* A blob that is not text is no blob, as an empty one is. */
    let blob = s(blob).unwrap_or("");
    let patch = if blob.is_empty() { None } else { Patch::parse(blob).ok() };
    let spread = !blob.is_empty() && !patch.as_ref().is_some_and(|p| p.version().slot_sounds());
    sh.bridge.post(Command::Load(sh.shared(Load { patch, values, spread })));
    1
}

/// The editor's paste of the clipboard's text: classified here, whole, and
/// queued only when good (`tg_core::paste`) -- a slot replaces the current
/// slot, a bank all eight, a whole patch everything -- after which the host's
/// parameters follow the current slot (`tg_shell_take_params`). Returns 1 for
/// a slot, 2 for a bank, 3 for a patch, or 0 with the reason, in words,
/// NUL-terminated in `err` (may be null); nothing changes then. Any non-audio
/// thread.
///
/// `slot` (0-based) is where a slot goes: the host's current slot as the
/// person saw it, carried in the command because the host's Slot may move in
/// the very block the paste is applied in, after it. Out of range is the
/// engine's current slot.
///
/// `text` is `len` bytes, not NUL-terminated: a clipboard holds anything,
/// a NUL included, and that must be refused rather than cut short.
///
/// # Safety
/// `text` is null or holds `len` bytes; `err` holds `err_len` bytes.
#[no_mangle]
pub unsafe extern "C" fn tg_shell_paste(
    sh: *const TgShell,
    slot: c_int,
    text: *const c_char,
    len: c_int,
    err: *mut c_char,
    err_len: c_int,
) -> c_int {
    let Some(sh) = sh.as_ref() else { return 0 };
    /* Bytes that are not text hold no slot either. */
    let text = if text.is_null() || len <= 0 { "" } else {
        match core::str::from_utf8(std::slice::from_raw_parts(text as *const u8, len as usize)) {
            Ok(t) => t,
            Err(_) => return refuse(err, err_len, |w| Refused::NotTranceGate.describe(w)),
        }
    };
    let clip = match Clip::parse(text) {
        Ok(clip) => clip,
        Err(r) => return refuse(err, err_len, |w| r.describe(w)),
    };
    let holds = clip.holds();
    sh.bridge.post(Command::Paste { slot: slot_index(slot), clip: sh.shared(clip) });
    match holds {
        Holds::Slot => 1,
        Holds::Bank => 2,
        Holds::Patch => 3,
    }
}

/// One cycle of the pattern in ms, as last published -- the scope's axis. A
/// second before the engine has a step length. Any non-audio thread.
///
/// # Safety
/// `sh` is null or live.
#[no_mangle]
pub unsafe extern "C" fn tg_shell_cycle_ms(sh: *const TgShell) -> f64 {
    match sh.as_ref() {
        Some(sh) => sh.bridge.read(|r| if r.frame.cycle_ms > 0.0 { r.frame.cycle_ms } else { 1000.0 }),
        None => 1000.0,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::CString;
    use tg_core::Instance;

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

    /*
     * WHAT PushParams COSTS A BLOCK THAT DOES NOT PUBLISH -- every block, not a
     * hundred a second. Run by hand, as publish_cost:
     *
     *     cargo test -p tg-capi --release -- --ignored --nocapture push_cost
     */
    #[test]
    #[ignore = "a measurement; run with --release --ignored --nocapture"]
    fn push_cost() {
        let sh = tg_shell_create(48000.0);
        heavy(sh);
        audio_block(sh, publish_every(48000.0) as c_int);
        let n = 20_000;
        for _ in 0..1000 {
            audio_block(sh, 1);
        }
        let t = std::time::Instant::now();
        for _ in 0..n {
            audio_block(sh, 1);
        }
        let per = t.elapsed().as_nanos() as f64 / n as f64;
        println!("push_cost: {per:.0} ns per block (fifteen set_num, no publish)");
        unsafe { tg_shell_destroy(sh) };
    }

    #[test]
    fn switching_slot_is_a_move_and_holding_it_is_not() {
        let mut pushed = -1;
        assert!(!slot_moved(&mut pushed, 4), "the first block has no slot to leave");
        assert_eq!(pushed, 4);
        assert!(slot_moved(&mut pushed, 3));
        assert!(!slot_moved(&mut pushed, 3));
        assert!(slot_moved(&mut pushed, 6));
    }

    /* The host's fifteen, on the numeric wire: the defaults, with the slot,
     * length and amount given. */
    fn host(slot: f64, length: f64, amount: f64) -> [f64; NUMS] {
        let mut v = STEADY;
        /* As the engine holds it: a float. */
        v[Param::Attack as usize] = 1.6f32 as f64;
        v[Param::Slot as usize] = slot;
        v[Param::Length as usize] = length;
        v[Param::Amount as usize] = amount;
        v
    }

    /* One block as the plugin pushes the host's parameters. */
    fn push_block(sh: *const TgShell, v: &[f64; NUMS]) {
        unsafe {
            let c = tg_shell_begin(sh);
            tg_shell_push(sh, c, v.as_ptr(), NUMS as c_int);
            tg_shell_end(sh, 64);
        }
    }

    fn take(sh: *const TgShell) -> Option<[f64; NUMS]> {
        let mut v = [0.0; NUMS];
        (unsafe { tg_shell_take_params(sh, v.as_mut_ptr(), NUMS as c_int) } == 1).then_some(v)
    }

    #[test]
    fn a_switch_recalls_the_new_slot_and_tells_the_host_every_value() {
        let sh = tg_shell_create(48000.0);
        push_block(sh, &host(0.0, 15.0, 1.0));
        push_block(sh, &host(0.0, 7.0, 0.25));
        assert_eq!(take(sh), None, "an edit is not a switch");
        assert!(read(sh, "state").contains("\"amount\":0.250"));

        /* To slot 2, with the host still holding slot 1's values. */
        push_block(sh, &host(1.0, 7.0, 0.25));
        let told = take(sh).expect("the host is told");
        assert_eq!(told, host(1.0, 15.0, 1.0), "slot 2's own values, every one");
        assert_eq!(take(sh), None, "once");
        push_block(sh, &host(1.0, 7.0, 0.25));
        assert_eq!(read(sh, "length"), "15", "the stale values are not pushed until the host has moved them");

        /* The host follows; then an edit of its own lands in slot 2 only. */
        push_block(sh, &told);
        let mut edit = told;
        edit[Param::Amount as usize] = 0.5;
        push_block(sh, &edit);
        push_block(sh, &host(0.0, 15.0, 0.5));
        assert_eq!(take(sh), Some(host(0.0, 7.0, 0.25)), "slot 1 kept its own");
        unsafe { tg_shell_destroy(sh) };
    }

    #[test]
    fn a_load_takes_the_hosts_values_for_its_slot_and_is_not_a_switch() {
        let sh = tg_shell_create(48000.0);
        push_block(sh, &host(0.0, 15.0, 1.0));
        /* A v6 blob: one sound, amount 0.700 -- and the host saved 0.7123. */
        let blob = CString::new("{\"sv\":6,\"slot\":2,\"amount\":0.700,\"p1\":\"FFFF:0:8\"}").unwrap();
        let mine = host(2.0, 11.0, 0.7123);
        assert_eq!(unsafe { tg_shell_load(sh, blob.as_ptr(), mine.as_ptr(), NUMS as c_int) }, 1);
        /* Readable before any audio has run. */
        let state = read(sh, "state");
        assert!(state.contains("\"amount\":0.712"), "{state}");
        assert!(!state.contains("\"s0\""), "an old blob loads eight alike: {state}");
        push_block(sh, &mine);
        assert_eq!(read(sh, "length"), "11");
        assert_eq!(read(sh, "state"), state);
        let told = take(sh).expect("the host's Slot moved with the load");
        assert_eq!(told[Param::Amount as usize], 0.7123f32 as f64, "and it is told what it already holds");
        unsafe { tg_shell_destroy(sh) };
    }

    #[test]
    fn a_paste_is_followed_by_the_host() {
        let sh = tg_shell_create(48000.0);
        push_block(sh, &host(0.0, 15.0, 1.0));
        let src = Instance::new(48000.0);
        let mut src = src;
        src.set_param("amount", "0.3");
        src.set_param("length", "3");
        let mut buf = vec![0u8; TEXT_MAX];
        let n = src.get_param("state", &mut buf) as usize;
        let blob = std::str::from_utf8(&buf[..n]).unwrap();
        assert_eq!(paste_into(sh, 0, blob), (3, String::new()));
        push_block(sh, &host(0.0, 15.0, 1.0));
        assert_eq!(take(sh), Some(host(0.0, 3.0, 0.3f32 as f64)), "the pasted slot wins over the host");
        assert_eq!(read(sh, "length"), "3");
        assert_eq!(unsafe { tg_shell_paste(sh, 0, std::ptr::null(), 0, std::ptr::null_mut(), 0) }, 0);
        unsafe { tg_shell_destroy(sh) };
    }

    #[test]
    fn a_paste_queued_with_a_slot_switch_lands_in_the_new_slot() {
        /* The Slot moved and the paste was posted before any block ran: the
         * block applies the paste, then sees the switch. The paste goes where
         * the person was looking, not into the slot being left. */
        let sh = tg_shell_create(48000.0);
        let one = host(0.0, 3.0, 0.3);
        push_block(sh, &one);
        let copied = export(sh, &one, false);
        let blank = export(sh, &host(1.0, 15.0, 1.0), false);
        assert_eq!(paste_into(sh, 1, &copied).0, 1);
        push_block(sh, &host(1.0, 15.0, 1.0));
        assert_eq!(take(sh), Some(host(1.0, 3.0, 0.3f32 as f64)));
        push_block(sh, &host(0.0, 3.0, 0.3f32 as f64));
        assert_eq!(take(sh), Some(host(0.0, 3.0, 0.3f32 as f64)), "slot 1 kept its own");
        assert_ne!(copied, blank);
        unsafe { tg_shell_destroy(sh) };
    }

    #[test]
    fn an_import_queued_with_a_slot_switch_lands_in_the_new_slot() {
        /* As for a paste: the Slot moved and the import was posted before any
         * block ran. The import goes into the slot the person selected. */
        let sh = tg_shell_create(48000.0);
        let one = host(0.0, 3.0, 0.3);
        push_block(sh, &one);
        let file = export(sh, &one, false);
        assert_eq!(import_into(sh, 1, &file).0, 1);
        push_block(sh, &host(1.0, 15.0, 1.0));
        assert_eq!(take(sh), Some(host(1.0, 3.0, 0.3f32 as f64)), "slot 2 holds the import");
        push_block(sh, &host(0.0, 3.0, 0.3f32 as f64));
        assert_eq!(take(sh), Some(host(0.0, 3.0, 0.3f32 as f64)), "slot 1 kept its own");
        unsafe { tg_shell_destroy(sh) };
    }

    fn paste_into(sh: *const TgShell, slot: c_int, text: &str) -> (c_int, String) {
        let mut err = [0u8; 256];
        let r = unsafe {
            tg_shell_paste(sh, slot, text.as_ptr() as *const c_char, text.len() as c_int, err.as_mut_ptr() as *mut c_char, 256)
        };
        let n = err.iter().position(|&b| b == 0).unwrap_or(0);
        (r, String::from_utf8(err[..n].to_vec()).unwrap())
    }

    #[test]
    fn a_copied_slot_pastes_into_the_current_slot_and_the_host_follows() {
        /* Copy slot 1, switch to slot 2, paste: slot 2 sounds like slot 1. */
        let sh = tg_shell_create(48000.0);
        let one = host(0.0, 3.0, 0.3);
        push_block(sh, &one);
        let copied = export(sh, &one, false);
        let two = host(1.0, 15.0, 1.0);
        push_block(sh, &two);
        let _ = take(sh);
        assert_eq!(paste_into(sh, 1, &copied), (1, String::new()));
        push_block(sh, &two);
        assert_eq!(take(sh), Some(host(1.0, 3.0, 0.3f32 as f64)), "the host follows the pasted slot");
        assert_eq!(export(sh, &host(1.0, 3.0, 0.3f32 as f64), false), copied);
        /* Slot 1 is as it was. */
        push_block(sh, &host(0.0, 3.0, 0.3f32 as f64));
        assert_eq!(take(sh), Some(host(0.0, 3.0, 0.3f32 as f64)));

        /* Garbage is refused with a reason, and nothing moves. */
        let before = read(sh, "state");
        assert_eq!(paste_into(sh, 0, "hello"), (0, "The clipboard doesn't hold a Trance Gate slot.".into()));
        assert_eq!(paste_into(sh, 0, &copied.replace("\"sound\"", "\"sou\0nd\"")).0, 0, "a NUL is refused, not cut short");
        assert_eq!(paste_into(sh, 0, ""), (0, "The clipboard is empty.".into()));
        assert_eq!(unsafe { tg_shell_paste(sh, 0, std::ptr::null(), 0, std::ptr::null_mut(), 0) }, 0);
        push_block(sh, &host(0.0, 3.0, 0.3f32 as f64));
        assert_eq!(take(sh), None);
        assert_eq!(read(sh, "state"), before);
        unsafe { tg_shell_destroy(sh) };
    }

    fn export(sh: *const TgShell, v: &[f64; NUMS], all: bool) -> String {
        let mut buf = vec![0u8; 16 * 1024];
        let n = unsafe {
            tg_shell_export(sh, v.as_ptr(), NUMS as c_int, all as c_int, buf.as_mut_ptr() as *mut c_char, buf.len() as c_int)
        };
        assert!(n > 0);
        String::from_utf8(buf[..n as usize].to_vec()).unwrap()
    }

    /* Into the engine's current slot, as every caller before the slot did. */
    fn import(sh: *const TgShell, text: &str) -> (c_int, String) {
        import_into(sh, -1, text)
    }

    fn import_into(sh: *const TgShell, slot: c_int, text: &str) -> (c_int, String) {
        let t = CString::new(text).unwrap();
        let mut err = [0u8; 256];
        let r = unsafe { tg_shell_import(sh, slot, t.as_ptr(), err.as_mut_ptr() as *mut c_char, 256) };
        let n = err.iter().position(|&b| b == 0).unwrap_or(0);
        (r, String::from_utf8(err[..n].to_vec()).unwrap())
    }

    #[test]
    fn an_export_holds_what_the_host_shows_and_an_import_is_followed_by_the_host() {
        let a = tg_shell_create(48000.0);
        /* Never run: the host's values reach the export all the same. */
        let shown = host(0.0, 7.0, 0.3f32 as f64);
        let file = export(a, &shown, false);
        assert!(file.contains("\"pattern\": \"5555:0:8"), "{file}");
        assert!(file.contains(":0.300:"), "{file}");

        let b = tg_shell_create(48000.0);
        push_block(b, &host(4.0, 15.0, 1.0));
        assert_eq!(import(b, &file), (1, String::new()));
        assert!(read(b, "state").contains("\"amount\":0.300"), "the view has it before any audio");
        push_block(b, &host(4.0, 15.0, 1.0));
        assert_eq!(take(b), Some(host(4.0, 7.0, 0.3f32 as f64)), "the host follows slot 5, now the file's");
        assert_eq!(export(b, &host(4.0, 7.0, 0.3f32 as f64), false), file);
        unsafe { tg_shell_destroy(a) };
        unsafe { tg_shell_destroy(b) };
    }

    #[test]
    fn a_bank_round_trips_and_a_bad_file_is_refused_with_a_reason() {
        let a = tg_shell_create(48000.0);
        push_block(a, &host(2.0, 3.0, 0.5));
        let bank = export(a, &host(2.0, 3.0, 0.5), true);
        let b = tg_shell_create(48000.0);
        push_block(b, &host(0.0, 15.0, 1.0));
        assert_eq!(import(b, &bank).0, 2);
        push_block(b, &host(0.0, 15.0, 1.0));
        assert_eq!(export(b, &host(0.0, 15.0, 1.0), true), bank);
        assert!(take(b).is_some(), "the host follows the bank's slot 1");

        let before = read(b, "state");
        let (r, why) = import(b, "{\"format\": \"ni-trance-gate-slot\", \"version\": 9}");
        assert_eq!(r, 0);
        assert!(why.contains("newer"), "{why}");
        push_block(b, &host(0.0, 15.0, 1.0));
        assert_eq!(read(b, "state"), before, "and nothing changed");
        assert_eq!(take(b), None);
        assert_eq!(unsafe { tg_shell_import(b, 0, std::ptr::null(), std::ptr::null_mut(), 0) }, 0);
        unsafe { tg_shell_destroy(a) };
        unsafe { tg_shell_destroy(b) };
    }

    #[test]
    fn the_cycle_is_the_step_times_the_steps() {
        let sh = tg_shell_create(48000.0);
        push_block(sh, &host(0.0, 15.0, 1.0));
        /* 1/16 at the engine's resting 120 BPM is 125 ms; sixteen of them. */
        assert_eq!(unsafe { tg_shell_cycle_ms(sh) }, 2000.0);
        push_block(sh, &host(0.0, 3.0, 1.0));
        assert_eq!(unsafe { tg_shell_cycle_ms(sh) }, 500.0);
        unsafe { tg_shell_destroy(sh) };
    }

    #[test]
    fn a_state_that_could_not_be_read_is_no_patch_at_all() {
        let state = b"{\"sv\":7}";
        assert_eq!(state_text(state, -1), None, "not an empty patch to save");
        assert_eq!(state_text(state, state.len() as c_int + 1), None);
        assert_eq!(state_text(state, state.len() as c_int), Some("{\"sv\":7}"));
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
