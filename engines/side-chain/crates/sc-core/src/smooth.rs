/*!
Parameter glides: the one-pole Depth moves through.

`tg-core/src/smooth.rs` is the same glide for the Trance Gate's Amount and
Sustain; this is its double-precision twin, because this engine's gain law is
in f64. The two are kept in step deliberately and are the first thing a shared
DSP crate should absorb.

A parameter that multiplies the signal is heard the instant it is written, so a
jump in the value is a step in the gain, which is a click. 5 ms is fast enough
that a knob still feels attached to what it does and slow enough that a
full-scale jump spreads over a couple of hundred samples. The glide LANDS -- it
snaps to the target once close enough, or once a step would no longer move it
-- so a value that stops moving is exactly the value that was written.
*/

/// The glide's time constant.
pub const SMOOTH_MS: f64 = 5.0;

/// Closer than this and the glide is over.
const SNAP: f64 = 1.0e-9;

/// The per-sample coefficient of a one-pole with time constant [`SMOOTH_MS`].
#[inline]
pub fn coef(sample_rate: f64) -> f64 {
    1.0 - (-1000.0 / (SMOOTH_MS * sample_rate)).exp()
}

/// One sample of the glide from `x` towards `target`. See `tg-core`'s for why
/// "a step would no longer move it" is a separate test.
#[inline]
pub fn glide(x: f64, target: f64, c: f64) -> f64 {
    let d = target - x;
    let y = x + d * c;
    if d.abs() < SNAP || y == x {
        target
    } else {
        y
    }
}
