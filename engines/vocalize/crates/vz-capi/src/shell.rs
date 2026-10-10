// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
`vz_shell_*`: the engine as the plugin holds it, behind shell-core's bridge.

THREE THREADS AND WHAT EACH MAY CALL:

```text
  construction   vz_shell_create, _destroy
  the message    vz_shell_post_sample_rate,   applied at the top of the
  thread         vz_shell_post_reset          next block
                 vz_shell_read                the latest picture
  the audio      vz_shell_begin, vz_core_*,   one block, in that order
  thread         vz_shell_end
```

THE WAY OUT IS THE PICTURE: the left channel's spectral envelope as it comes
in and as the engine makes it, at `VZ_POINTS` frequencies from 50 Hz to
10 kHz, equally spaced in pitch -- vz_core's `Engine::envelopes`, published
about a hundred times a second (shell-core's cadence). With it the mode
running and its latency, so an editor can say which engine it is drawing.

Everything else an editor shows is a host parameter, read by the editor from
the host's side.
*/

use crate::VzCore;
use shell_core::{publish_every, Bridge, Model};
use vz_core::Mode;

/// How many frequencies the picture has.
pub const VZ_POINTS: usize = 96;

/// The picture's lowest and highest frequency, in Hz.
pub const VZ_LOWEST_HZ: f64 = 50.0;
pub const VZ_HIGHEST_HZ: f64 = 10_000.0;

/// What the editor draws from, published by the audio thread.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VzPicture {
    /// Changes whenever new audio arrived.
    pub serial: u32,
    /// The engine running: 0 Time, 1 Spectral.
    pub mode: u32,
    /// Its latency, in samples.
    pub latency: u32,
    /// The host's rate.
    pub sample_rate: f64,
    /// The frequencies, in Hz, lowest first.
    pub hz: [f64; VZ_POINTS],
    /// The envelope coming in and going out, in dB, each with its peak at 0.
    pub input_db: [f64; VZ_POINTS],
    pub output_db: [f64; VZ_POINTS],
}

/// The picture's frequencies: equally spaced in pitch.
fn frequencies() -> [f64; VZ_POINTS] {
    let span = VZ_HIGHEST_HZ / VZ_LOWEST_HZ;
    core::array::from_fn(|i| VZ_LOWEST_HZ * span.powf(i as f64 / (VZ_POINTS - 1) as f64))
}

impl VzPicture {
    fn empty() -> VzPicture {
        VzPicture {
            serial: 0,
            mode: 0,
            latency: 0,
            sample_rate: 0.0,
            hz: frequencies(),
            input_db: [0.0; VZ_POINTS],
            output_db: [0.0; VZ_POINTS],
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

impl Model for VzCore {
    type Command = Command;
    type Frame = VzPicture;

    fn new_frame(&self) -> VzPicture {
        VzPicture::empty()
    }

    fn apply(&mut self, cmd: &Command) {
        match *cmd {
            Command::SampleRate(rate) => self.engine.set_sample_rate(rate),
            Command::Reset => self.engine.reset(),
        }
    }

    fn publish(&self, f: &mut VzPicture) {
        f.serial = self.serial;
        f.mode = (self.engine.mode() == Mode::Spectral) as u32;
        f.latency = self.engine.latency() as u32;
        f.sample_rate = self.engine.sample_rate();
        self.engine.envelopes(&f.hz, &mut f.input_db, &mut f.output_db);
    }

    /* Every parameter reaches the engine from the shell on the audio
     * thread, so there is no view to rebuild. */
    fn restore(&mut self, _: &VzPicture) {}
}

/// The handle a plugin instance holds.
pub struct VzShell {
    bridge: Bridge<VzCore>,
}

/// A new engine at `sample_rate` (44.1 kHz if that is not positive), every
/// parameter at its default. Construction only: it allocates.
#[no_mangle]
pub extern "C" fn vz_shell_create(sample_rate: f64) -> *mut VzShell {
    let core = VzCore::new(sample_rate);
    let rate = core.engine.sample_rate();
    let bridge = Bridge::new(core, None, QUEUE, publish_every(rate));
    Box::into_raw(Box::new(VzShell { bridge }))
}

/// # Safety
/// `shell` is null or from `vz_shell_create`, and no thread uses it after.
#[no_mangle]
pub unsafe extern "C" fn vz_shell_destroy(shell: *mut VzShell) {
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
pub unsafe extern "C" fn vz_shell_post_sample_rate(shell: *const VzShell, sample_rate: f64) {
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
pub unsafe extern "C" fn vz_shell_post_reset(shell: *const VzShell) {
    if let Some(shell) = shell.as_ref() {
        shell.bridge.post(Command::Reset);
    }
}

/// The audio thread, at the top of a block: the engine, for this block only.
///
/// # Safety
/// `shell` is null or live; the audio thread only, one block at a time; the
/// pointer is not used after `vz_shell_end`.
#[no_mangle]
pub unsafe extern "C" fn vz_shell_begin(shell: *const VzShell) -> *mut VzCore {
    match shell.as_ref() {
        Some(shell) => shell.bridge.begin() as *mut VzCore,
        None => std::ptr::null_mut(),
    }
}

/// The audio thread, at the end of a block of `frames`: publishes the
/// picture when one is due.
///
/// # Safety
/// As `vz_shell_begin`, after it.
#[no_mangle]
pub unsafe extern "C" fn vz_shell_end(shell: *const VzShell, frames: u32) {
    if let Some(shell) = shell.as_ref() {
        shell.bridge.end(frames);
    }
}

/// Copy the latest picture into `out`. Not the audio thread. False when
/// there is nothing to copy into or from.
///
/// # Safety
/// `shell` is null or live; `out` is null or writable.
#[no_mangle]
pub unsafe extern "C" fn vz_shell_read(shell: *const VzShell, out: *mut VzPicture) -> bool {
    let (Some(shell), Some(out)) = (shell.as_ref(), out.as_mut()) else {
        return false;
    };
    shell.bridge.read(|r| *out = *r.frame);
    true
}
