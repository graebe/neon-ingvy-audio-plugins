// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
An equaliser's analyzer, in two halves, one on each side of a plugin's
threads:

```text
  audio thread    Rings     the last RING samples in and out, in one Lane,
                            kept only while a window watches
  editor thread   Analyzer  their spectra as dB per band of a picture's
                            frequencies (feature `spectrum`)
```

THE LANE is the signal a picture is of: a stereo pair's left or right, or
its mid ((L + R) / 2, what a mono sum hears) or side ((L - R) / 2) -- so a
Side filter is seen working on the side. A new lane starts from empty rings,
and an [`Analyzer`] asked for one reads only frames of it, so a picture never
shows one lane's spectrum as another's.

PRODUCT-FREE: this crate names no product (the workspace's rule).

THE AUDIO THREAD ALLOCATES NOTHING: the rings are made in [`Rings::new`]
(tests/no_alloc.rs).
*/

/// Samples each ring keeps: a power of two.
pub const RING: usize = 8192;

/// Which signal the rings keep (see the crate's notes).
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Lane {
    Left,
    Right,
    Mid,
    Side,
}

impl Lane {
    /// The lane's sample of the pair `l`, `r`.
    #[inline]
    pub fn of(self, l: f64, r: f64) -> f64 {
        match self {
            Lane::Left => l,
            Lane::Right => r,
            Lane::Mid => 0.5 * (l + r),
            Lane::Side => 0.5 * (l - r),
        }
    }
}

/// The input and the output an engine keeps for the analyzer, in its lane,
/// while a window watches.
pub struct Rings {
    watching: bool,
    lane: Lane,
    pre: Box<[f32; RING]>,
    post: Box<[f32; RING]>,
    pos: usize,
}

impl Default for Rings {
    fn default() -> Rings {
        Rings::new()
    }
}

impl Rings {
    /// Empty rings in the Mid lane, not watching. Allocates: not the audio
    /// thread.
    pub fn new() -> Rings {
        Rings { watching: false, lane: Lane::Mid, pre: Box::new([0.0; RING]), post: Box::new([0.0; RING]), pos: 0 }
    }

    /// Whether a window shows the analyzer: the rings are kept only then.
    pub fn set_watching(&mut self, on: bool) {
        self.watching = on;
    }

    pub fn is_watching(&self) -> bool {
        self.watching
    }

    /// Which lane the rings keep. A new lane starts from empty rings.
    pub fn set_lane(&mut self, lane: Lane) {
        if lane != self.lane {
            self.lane = lane;
            self.pre.fill(0.0);
            self.post.fill(0.0);
        }
    }

    pub fn lane(&self) -> Lane {
        self.lane
    }

    /// A block's input, `l` and `r`, before the engine works on it. Its
    /// [`Rings::output`] follows.
    pub fn input(&mut self, l: &[f64], r: &[f64]) {
        if self.watching {
            for (k, (&x, &y)) in l.iter().zip(r.iter()).enumerate() {
                self.pre[(self.pos + k) & (RING - 1)] = self.lane.of(x, y) as f32;
            }
        }
    }

    /// The same block's output: then the rings move on by its length.
    pub fn output(&mut self, l: &[f64], r: &[f64]) {
        if self.watching {
            for (k, (&x, &y)) in l.iter().zip(r.iter()).enumerate() {
                self.post[(self.pos + k) & (RING - 1)] = self.lane.of(x, y) as f32;
            }
            self.pos = (self.pos + l.len().min(r.len())) & (RING - 1);
        }
    }

    /// A block heard and left alone: the input is both what came in and
    /// what went out. `right` is `None` for a mono track.
    pub fn listen(&mut self, left: &[f32], right: Option<&[f32]>) {
        if self.watching {
            for (i, &l) in left.iter().enumerate() {
                let r = right.and_then(|r| r.get(i)).copied().unwrap_or(l);
                let x = self.lane.of(f64::from(l), f64::from(r)) as f32;
                self.pre[self.pos] = x;
                self.post[self.pos] = x;
                self.pos = (self.pos + 1) & (RING - 1);
            }
        }
    }

    /// The rings, oldest first, into `pre` and `post` (as many as they hold,
    /// up to [`RING`]).
    pub fn copy_out(&self, pre: &mut [f32], post: &mut [f32]) {
        for (k, (a, b)) in pre.iter_mut().zip(post.iter_mut()).take(RING).enumerate() {
            let at = (self.pos + k) & (RING - 1);
            *a = self.pre[at];
            *b = self.post[at];
        }
    }
}

#[cfg(feature = "spectrum")]
mod spectrum;
#[cfg(feature = "spectrum")]
pub use spectrum::{Analyzer, FLOOR_DB, RELEASE_S};

#[cfg(test)]
mod tests;
