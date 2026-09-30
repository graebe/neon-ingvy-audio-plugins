/*!
Parameter glides: the one-pole a gain-bearing parameter moves through, so a
written jump is not a click.

5 ms: a knob still feels attached, and a full-scale jump spreads over a couple
of hundred samples. The glide LANDS on its target, so a value that stops
moving is exactly the value written and an untouched patch renders the same
bits. Generic over the engine's precision; each has its own landing distance.
*/

/// The glide's time constant.
pub const SMOOTH_MS: f64 = 5.0;

/// A precision a glide runs in.
pub trait Glide: Copy {
    /// Closer than this and the glide is over.
    const SNAP: Self;
    fn from_f64(v: f64) -> Self;
    fn step(x: Self, target: Self, c: Self) -> Self;
}

macro_rules! glide_impl {
    ($t:ty, $snap:expr) => {
        impl Glide for $t {
            const SNAP: $t = $snap;
            #[inline]
            fn from_f64(v: f64) -> $t {
                v as $t
            }
            #[inline]
            fn step(x: $t, target: $t, c: $t) -> $t {
                let d = target - x;
                let y = x + d * c;
                if d.abs() < Self::SNAP || y == x {
                    target
                } else {
                    y
                }
            }
        }
    };
}

glide_impl!(f32, 1.0e-6);
glide_impl!(f64, 1.0e-9);

/// The per-sample coefficient of a one-pole with time constant [`SMOOTH_MS`],
/// computed in f64 and then narrowed.
#[inline]
pub fn coef<T: Glide>(sample_rate: f64) -> T {
    T::from_f64(1.0 - (-1000.0 / (SMOOTH_MS * sample_rate)).exp())
}

/// One sample of the glide from `x` towards `target`.
///
/// It lands within `SNAP` OR when a step would no longer move it: near the
/// end `d * c` drops below half an ulp of `x` and the sum rounds back to `x`,
/// so waiting for `SNAP` alone could sit a few ulps short forever.
#[inline]
pub fn glide<T: Glide>(x: T, target: T, c: T) -> T {
    T::step(x, target, c)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn both_precisions_land_exactly() {
        let c32: f32 = coef(44100.0);
        let (mut x, mut n) = (0.0f32, 0);
        while x != 0.7 {
            x = glide(x, 0.7, c32);
            n += 1;
            assert!(n < 100_000, "f32 never landed");
        }
        let c64: f64 = coef(44100.0);
        let (mut y, mut m) = (1.0f64, 0);
        while y != 0.25 {
            y = glide(y, 0.25, c64);
            m += 1;
            assert!(m < 100_000, "f64 never landed");
        }
        assert_eq!(c32, c64 as f32);
    }
}
