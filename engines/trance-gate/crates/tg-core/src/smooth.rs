/*!
Parameter glides: the one-pole every gain-bearing parameter moves through.

A parameter that multiplies the signal -- Amount, and Sustain while the
envelope is sitting on it -- is heard the instant it is written, so a jump in
the value is a step in the gain, which is a click. Hosts write values between
blocks and a knob on the Move writes whenever it is turned, so the jump is the
normal case, not the edge case.

5 ms is the usual answer: fast enough that a knob still feels attached to what
it does, slow enough that a full-scale jump spreads over a couple of hundred
samples. The glide LANDS -- it snaps to the target once within `SNAP` -- so a
value that stops moving is exactly the value that was written, and a patch that
is not being touched renders the same bits it always did.

Allocation-free and branch-light: one compare and one multiply-add per sample.
*/

/// The glide's time constant.
pub const SMOOTH_MS: f64 = 5.0;

/// Closer than this and the glide is over.
const SNAP: f32 = 1.0e-6;

/// The per-sample coefficient of a one-pole with time constant [`SMOOTH_MS`].
#[inline]
pub fn coef(sample_rate: f64) -> f32 {
    (1.0 - (-1000.0 / (SMOOTH_MS * sample_rate)).exp()) as f32
}

/// One sample of the glide from `x` towards `target`.
///
/// It lands when it is within `SNAP` OR when a step would no longer move it.
/// The second test is not redundant: near the end, `d * c` drops below half
/// an ulp of `x` and the sum rounds back to `x`, so a glide that waited only
/// for `SNAP` could sit a few ulps short forever.
#[inline]
pub fn glide(x: f32, target: f32, c: f32) -> f32 {
    let d = target - x;
    let y = x + d * c;
    if d.abs() < SNAP || y == x {
        target
    } else {
        y
    }
}
