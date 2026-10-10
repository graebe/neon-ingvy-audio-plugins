// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
`bc_shell_*`: the engine as the plugin holds it, behind shell-core's bridge.

THREE THREADS AND WHAT EACH MAY CALL:

```text
  construction   bc_shell_create, _destroy
  the message    bc_shell_post_sample_rate,   applied at the top of the
  thread         bc_shell_post_reset          next block
                 bc_shell_read                the latest scope
  the audio      bc_shell_begin, bc_core_*,   one block, in that order
  thread         bc_shell_end
```

THE WAY OUT IS THE SCOPE: what the editor draws under the staircase, a window
of `BC_SCOPE` samples of the left channel, dry and wet, published about a
hundred times a second (shell-core's cadence). The window starts on the latest
upward zero crossing of the dry signal that leaves room for a whole window
after it -- an oscilloscope's trigger -- so a steady tone stands still on the
screen instead of crawling. With no crossing (silence, DC, a tone below 25 Hz
or so) it is simply the latest samples, and `triggered` says so.

Everything else an editor shows is a host parameter, read by the editor from
the host's side.
*/

use crate::{BcCore, BC_SCOPE_RING};
use bc_core::LATENCY;
use shell_core::{publish_every, Bridge, Model};

/// The scope's window, in samples: about 11 ms at 48 kHz, so a step of the
/// hold is a few pixels wide.
pub const BC_SCOPE: usize = 512;

/// What the editor draws from, published by the audio thread.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct BcScope {
    /// Changes whenever new audio arrived.
    pub serial: u32,
    /// True when the window starts on an upward zero crossing.
    pub triggered: bool,
    /// The host's rate, for the window's length in time.
    pub sample_rate: f64,
    /// The left channel before and after, -1..1 at full scale.
    pub dry: [f32; BC_SCOPE],
    pub wet: [f32; BC_SCOPE],
}

impl BcScope {
    fn empty() -> BcScope {
        BcScope {
            serial: 0,
            triggered: false,
            sample_rate: 0.0,
            dry: [0.0; BC_SCOPE],
            wet: [0.0; BC_SCOPE],
        }
    }
}

/// What another thread may ask of the engine.
#[derive(Clone, Copy)]
pub enum Command {
    SampleRate(f64),
    /// What a shell posts when the host (re)activates it: whatever was held
    /// from before lets go.
    Reset,
}

/// Commands between two blocks.
const QUEUE: usize = 16;

/// Where the window starts among the ring's samples, oldest first: the
/// latest upward zero crossing with a whole window after it, if any.
fn trigger(dry: &[f32]) -> Option<usize> {
    let last = dry.len().checked_sub(BC_SCOPE)?;
    (1..=last).rev().find(|&t| dry[t - 1] < 0.0 && dry[t] >= 0.0)
}

impl Model for BcCore {
    type Command = Command;
    type Frame = BcScope;

    fn new_frame(&self) -> BcScope {
        BcScope::empty()
    }

    fn apply(&mut self, cmd: &Command) {
        match *cmd {
            Command::SampleRate(rate) => self.engine.set_sample_rate(rate),
            Command::Reset => self.engine.reset(),
        }
    }

    fn publish(&self, f: &mut BcScope) {
        f.serial = self.serial;
        f.sample_rate = self.engine.sample_rate();
        const SPAN: usize = BC_SCOPE_RING - LATENCY;
        let mut dry = [0.0f32; SPAN];
        for (slot, (d, _)) in dry.iter_mut().zip(self.recent(SPAN)) {
            *slot = d;
        }
        let start = trigger(&dry);
        f.triggered = start.is_some();
        let start = start.unwrap_or(SPAN - BC_SCOPE);
        for (i, (d, w)) in self.recent(SPAN).skip(start).take(BC_SCOPE).enumerate() {
            f.dry[i] = d;
            f.wet[i] = w;
        }
    }

    /* Every parameter reaches the engine from the shell on the audio
     * thread, so there is no view to rebuild. */
    fn restore(&mut self, _: &BcScope) {}
}

/// The handle a plugin instance holds.
pub struct BcShell {
    bridge: Bridge<BcCore>,
}

/// A new engine at `sample_rate` (44.1 kHz if that is not positive), every
/// parameter at its default. Construction only: it allocates.
#[no_mangle]
pub extern "C" fn bc_shell_create(sample_rate: f64) -> *mut BcShell {
    let core = BcCore::new(sample_rate);
    let rate = core.engine.sample_rate();
    let bridge = Bridge::new(core, None, QUEUE, publish_every(rate));
    Box::into_raw(Box::new(BcShell { bridge }))
}

/// # Safety
/// `shell` is null or from `bc_shell_create`, and no thread uses it after.
#[no_mangle]
pub unsafe extern "C" fn bc_shell_destroy(shell: *mut BcShell) {
    if !shell.is_null() {
        drop(Box::from_raw(shell));
    }
}

/// The host's rate, applied at the top of the next block. Not the audio
/// thread. A rate that is not positive is ignored.
///
/// # Safety
/// `shell` is null or live.
#[no_mangle]
pub unsafe extern "C" fn bc_shell_post_sample_rate(shell: *const BcShell, sample_rate: f64) {
    let Some(shell) = shell.as_ref() else { return };
    if sample_rate.is_nan() || sample_rate <= 0.0 {
        return;
    }
    shell.bridge.post(Command::SampleRate(sample_rate));
    shell.bridge.set_publish_every(publish_every(sample_rate));
}

/// Everything held lets go, at the top of the next block. Not the audio
/// thread.
///
/// # Safety
/// `shell` is null or live.
#[no_mangle]
pub unsafe extern "C" fn bc_shell_post_reset(shell: *const BcShell) {
    if let Some(shell) = shell.as_ref() {
        shell.bridge.post(Command::Reset);
    }
}

/// The audio thread, at the top of a block: the engine, for this block only.
///
/// # Safety
/// `shell` is null or live; the audio thread only, one block at a time; the
/// pointer is not used after `bc_shell_end`.
#[no_mangle]
pub unsafe extern "C" fn bc_shell_begin(shell: *const BcShell) -> *mut BcCore {
    match shell.as_ref() {
        Some(shell) => shell.bridge.begin() as *mut BcCore,
        None => std::ptr::null_mut(),
    }
}

/// The audio thread, at the end of a block of `frames`: publishes the scope
/// when one is due.
///
/// # Safety
/// As `bc_shell_begin`, after it.
#[no_mangle]
pub unsafe extern "C" fn bc_shell_end(shell: *const BcShell, frames: u32) {
    if let Some(shell) = shell.as_ref() {
        shell.bridge.end(frames);
    }
}

/// Copy the latest scope into `out`. Not the audio thread. False when there
/// is nothing to copy into or from.
///
/// # Safety
/// `shell` is null or live; `out` is null or writable.
#[no_mangle]
pub unsafe extern "C" fn bc_shell_read(shell: *const BcShell, out: *mut BcScope) -> bool {
    let (Some(shell), Some(out)) = (shell.as_ref(), out.as_mut()) else {
        return false;
    };
    shell.bridge.read(|r| *out = *r.frame);
    true
}
