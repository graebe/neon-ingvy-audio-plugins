/*
 * The C ABI: what every plugin in this repository links to ring its ground on
 * the beat.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * `engines/ground/include/ground.h` is the contract; this is the half that
 * implements it. The header is written by hand rather than generated, for the
 * reason the other engines here state: a generator hides a disagreement by
 * overwriting it, so the two would drift silently.
 *
 * Scalars cross the boundary. No callbacks, no shared structs, no ownership
 * passing either way except the one opaque handle.
 *
 * THE SURFACE IS EIGHT FUNCTIONS because the consumer is four plugins that want
 * the same few lines of code each. The musical rule -- a ring a beat, the
 * downbeat strongest, nothing while stopped -- is ground-core's, and none of it
 * is configurable: a plugin that could tune it would be a plugin whose
 * background disagreed with the others.
 */

use ground_core::{Ground, Transport};

/// The opaque handle. One per plugin instance.
pub struct GndGround(Ground);

/// Create a ground for a sample rate. Never returns null; a nonsensical rate
/// still allocates and rings nothing until it is told the truth, because the
/// alternative is a plugin that has to branch on it in `ProcessBlock`.
///
/// Allocates. The main thread only.
#[no_mangle]
pub extern "C" fn gnd_new(sample_rate: f64) -> *mut GndGround {
    Box::into_raw(Box::new(GndGround(Ground::new(sample_rate))))
}

/// Release a ground. Null is a no-op.
///
/// # Safety
/// `g` must come from `gnd_new` and must not be used afterwards.
#[no_mangle]
pub unsafe extern "C" fn gnd_free(g: *mut GndGround) {
    if !g.is_null() {
        drop(Box::from_raw(g));
    }
}

/// Ask for a new sample rate and a fresh clock. The ring count is
/// deliberately NOT reset -- see `gnd_fires`.
///
/// Any thread: this stores a request, and the audio thread applies it at the
/// top of its next `gnd_tick`. It never touches the clock itself, so it
/// cannot race a block in progress -- which matters because hosts do not
/// promise to call `OnReset` off the audio thread.
///
/// # Safety
/// `g` must be a live handle from `gnd_new`, or null.
#[no_mangle]
pub unsafe extern "C" fn gnd_set_sample_rate(g: *const GndGround, sample_rate: f64) {
    if let Some(d) = g.as_ref() {
        d.0.set_sample_rate(sample_rate);
    }
}

/// Ask for the clock to forget the last block, without touching the count:
/// the next playing block is then a fresh start, which rings only what it
/// holds.
///
/// Any thread; applied by the next `gnd_tick`, as `gnd_set_sample_rate` is.
///
/// # Safety
/// `g` must be a live handle from `gnd_new`, or null.
#[no_mangle]
pub unsafe extern "C" fn gnd_reset(g: *const GndGround) {
    if let Some(d) = g.as_ref() {
        d.0.reset();
    }
}

/// Switch the ground on (nonzero) or off (zero). A new ground is OFF.
///
/// The ground only drives an editor, so a plugin switches it on when its
/// editor opens and off when it closes; while off, `gnd_tick` returns at once.
/// Switching on also asks for a reset, so a reopened editor's first block is a
/// fresh start and rings no backlog.
///
/// Any thread.
///
/// # Safety
/// `g` must be a live handle from `gnd_new`, or null.
#[no_mangle]
pub unsafe extern "C" fn gnd_set_active(g: *const GndGround, active: i32) {
    if let Some(d) = g.as_ref() {
        d.0.set_active(active != 0);
    }
}

/// One block of the host's transport: its position at the block's first
/// sample in quarter notes, its tempo, its time signature, whether it is
/// playing, and how many samples the block holds. See `Ground::tick` and
/// ground-core's beat.rs for what rings.
///
/// Every value is taken as the host reported it: a time signature that is not
/// positive is 4/4, a tempo that is not is 120, and a position that is not
/// finite is a stopped transport. A non-positive `frames` is a no-op.
///
/// THE AUDIO THREAD, AND ONLY IT -- one caller at a time, which is what makes
/// the clock it owns safe to mutate through a shared handle. Allocates
/// nothing, takes no lock, makes no system call.
///
/// # Safety
/// `g` must be a live handle from `gnd_new`, or null, and no other `gnd_tick`
/// on the same handle may be running.
#[no_mangle]
pub unsafe extern "C" fn gnd_tick(
    g: *const GndGround,
    ppq: f64,
    bpm: f64,
    num: i32,
    den: i32,
    playing: i32,
    frames: i32,
) {
    if frames <= 0 {
        return;
    }
    let Some(d) = g.as_ref() else { return };
    d.0.tick(&Transport { playing: playing != 0, ppq, bpm, num, den }, frames as usize);
}

/// The monotonic ring count. WATCH IT CHANGE -- its absolute value means
/// nothing, and it is not reset by `gnd_reset` or `gnd_set_sample_rate`
/// precisely so that a reader comparing it against what it last saw cannot be
/// tricked into seeing a ring that never happened.
///
/// Any thread. Null reads 0.
///
/// # Safety
/// `g` must be a live handle from `gnd_new`, or null.
#[no_mangle]
pub unsafe extern "C" fn gnd_fires(g: *const GndGround) -> u32 {
    g.as_ref().map_or(0, |d| d.0.fires())
}

/// The most recent ring's strength -- 1 for a downbeat, 0.4 for any other
/// beat -- which the editor passes to the field. Meaningless until `gnd_fires`
/// has moved, where it reads 0.
///
/// Any thread. Null reads 0.
///
/// # Safety
/// `g` must be a live handle from `gnd_new`, or null.
#[no_mangle]
pub unsafe extern "C" fn gnd_strength(g: *const GndGround) -> f32 {
    g.as_ref().map_or(0.0, |d| d.0.strength())
}

#[cfg(test)]
mod tests;
