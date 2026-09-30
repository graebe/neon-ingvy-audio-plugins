/*
 * The C ABI: what every plugin in this repository links to see a kick.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * `engines/ground/include/ground_detect.h` is the contract; this is the half
 * that implements it. The header is written by hand rather than generated, for
 * the reason the other engines here state: a generator hides a disagreement by
 * overwriting it, so the two would drift silently.
 *
 * Scalars and float buffers cross the boundary. No callbacks, no shared
 * structs, no ownership passing either way except the one opaque handle.
 *
 * THE SURFACE IS EIGHT FUNCTIONS because the consumer is four plugins that want
 * the same three lines of code each. Everything interesting -- the band, the
 * envelope, the relative threshold -- is in ground-core, and none of it is
 * configurable: the numbers belong to the design system, and a plugin that
 * could tune them would be a plugin whose background disagreed with the others.
 */

use ground_core::Ground;

/// The opaque handle. One per plugin instance.
pub struct GndDetector(Ground);

/// Create a detector for a sample rate. Never returns null for a sane rate; a
/// nonsensical one still allocates, because the alternative is a plugin that
/// has to branch on it in `ProcessBlock`.
///
/// Allocates. The main thread only.
#[no_mangle]
pub extern "C" fn gnd_new(sample_rate: f64) -> *mut GndDetector {
    Box::into_raw(Box::new(GndDetector(Ground::new(sample_rate))))
}

/// Release a detector. Null is a no-op.
///
/// # Safety
/// `g` must come from `gnd_new` and must not be used afterwards.
#[no_mangle]
pub unsafe extern "C" fn gnd_free(g: *mut GndDetector) {
    if !g.is_null() {
        drop(Box::from_raw(g));
    }
}

/// Ask for a new sample rate and a clean detector. The onset count is
/// deliberately NOT reset -- see `gnd_fires`.
///
/// Any thread: this stores a request, and the audio thread applies it at the
/// top of its next `gnd_push`. It never touches the detector itself, so it
/// cannot race a block in progress -- which matters because hosts do not
/// promise to call `OnReset` off the audio thread.
///
/// # Safety
/// `g` must be a live handle from `gnd_new`, or null.
#[no_mangle]
pub unsafe extern "C" fn gnd_set_sample_rate(g: *const GndDetector, sample_rate: f64) {
    if let Some(d) = g.as_ref() {
        d.0.set_sample_rate(sample_rate);
    }
}

/// Ask for the detector's state to be forgotten, without touching the count,
/// so that a transport stop does not fire an onset on the stale hump when
/// playback resumes.
///
/// Any thread; applied by the next `gnd_push`, as `gnd_set_sample_rate` is.
///
/// # Safety
/// `g` must be a live handle from `gnd_new`, or null.
#[no_mangle]
pub unsafe extern "C" fn gnd_reset(g: *const GndDetector) {
    if let Some(d) = g.as_ref() {
        d.0.reset();
    }
}

/// Switch the detector on (nonzero) or off (zero). A new detector is OFF.
///
/// The detector only drives an editor, so a plugin switches it on when its
/// editor opens and off when it closes; while off, `gnd_push` returns at once.
/// Switching on also asks for a reset, so a reopened editor starts from
/// silence rather than from whatever hump the detector held when it closed.
///
/// Any thread.
///
/// # Safety
/// `g` must be a live handle from `gnd_new`, or null.
#[no_mangle]
pub unsafe extern "C" fn gnd_set_active(g: *const GndDetector, active: i32) {
    if let Some(d) = g.as_ref() {
        d.0.set_active(active != 0);
    }
}

/// Feed one block of stereo. A mono plugin passes the same pointer twice.
///
/// DOUBLES, because iPlug2's `sample` is one and this only reads -- so a plugin
/// hands over `inputs[0]` and `inputs[1]` as they arrive, with no scratch buffer
/// and no conversion. See `Ground::push`.
///
/// THE AUDIO THREAD, AND ONLY IT -- one caller at a time, which is what makes
/// the detector it owns safe to mutate through a shared handle. Allocates
/// nothing, takes no lock, makes no system call. Null pointers or a
/// non-positive `frames` are a no-op rather than undefined: a host handing us an
/// empty block is ordinary.
///
/// # Safety
/// `left` and `right` must each be readable for `frames` doubles, and no other
/// `gnd_push` on the same handle may be running.
#[no_mangle]
pub unsafe extern "C" fn gnd_push(
    g: *const GndDetector,
    left: *const f64,
    right: *const f64,
    frames: i32,
) {
    if frames <= 0 || left.is_null() || right.is_null() {
        return;
    }
    let Some(d) = g.as_ref() else { return };
    let n = frames as usize;
    d.0.push(
        core::slice::from_raw_parts(left, n),
        core::slice::from_raw_parts(right, n),
    );
}

/// The monotonic onset count. WATCH IT CHANGE -- its absolute value means
/// nothing, and it is not reset by `gnd_reset` or `gnd_set_sample_rate`
/// precisely so that a reader comparing it against what it last saw cannot be
/// tricked into seeing an onset that never happened.
///
/// Any thread. Null reads 0.
///
/// # Safety
/// `g` must be a live handle from `gnd_new`, or null.
#[no_mangle]
pub unsafe extern "C" fn gnd_fires(g: *const GndDetector) -> u32 {
    g.as_ref().map_or(0, |d| d.0.fires())
}

/// The most recent onset's strength, 0.3..1 -- what the editor passes to the
/// field. Meaningless until `gnd_fires` has moved, where it reads 0.
///
/// Any thread. Null reads 0.
///
/// # Safety
/// `g` must be a live handle from `gnd_new`, or null.
#[no_mangle]
pub unsafe extern "C" fn gnd_strength(g: *const GndDetector) -> f32 {
    g.as_ref().map_or(0.0, |d| d.0.strength())
}
