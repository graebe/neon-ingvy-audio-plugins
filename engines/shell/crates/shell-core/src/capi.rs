// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The C ABI every product's shell exports, once: create, the host's rate, the
block's engine and the latest frame.

EACH PRODUCT KEEPS ITS OWN `extern "C"` FUNCTIONS -- `bc_shell_create`,
`fd_shell_begin` -- because cbindgen writes a product's header from what is
written in its crate, never from what a macro expands. What they share is
here: each of them is its name, its null check and one call.
*/

use crate::{publish_every, Bridge, Model};

/// A bridge over `engine`, publishing at the cadence of `sample_rate` (the
/// engine's own, after it adopted what the host gave), with room for `queue`
/// commands between two blocks. Construction only: it allocates.
pub fn create<M: Model>(engine: M, sample_rate: f64, queue: usize) -> Bridge<M> {
    Bridge::new(engine, None, queue, publish_every(sample_rate))
}

/// The host's rate: `command(rate)` posted, and the publishing cadence
/// following it. A rate that is not a positive number -- NaN, a host that has
/// not said -- is ignored. Not the audio thread.
pub fn post_sample_rate<M: Model>(bridge: &Bridge<M>, sample_rate: f64, command: impl FnOnce(f64) -> M::Command) {
    if let Some(rate) = ni_dsp::ffi::usable_rate(sample_rate) {
        bridge.post(command(rate));
        bridge.set_publish_every(publish_every(rate));
    }
}

/// The engine for this block, or null without a bridge.
///
/// # Safety
/// As [`Bridge::begin`]: the audio thread only, one block at a time, the
/// pointer not used after the block's `end`.
pub unsafe fn begin<M: Model>(bridge: Option<&Bridge<M>>) -> *mut M {
    match bridge {
        Some(b) => b.begin() as *mut M,
        None => std::ptr::null_mut(),
    }
}

/// The latest frame, copied into `out`. False -- nothing written -- without a
/// bridge or an `out`. Not the audio thread.
pub fn read_into<M: Model>(bridge: Option<&Bridge<M>>, out: Option<&mut M::Frame>) -> bool
where
    M::Frame: Copy,
{
    let (Some(bridge), Some(out)) = (bridge, out) else {
        return false;
    };
    bridge.read(|r| *out = *r.frame);
    true
}
