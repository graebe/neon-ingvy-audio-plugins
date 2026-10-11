// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
A channel's recent past: the last `RING` samples, newest at `at(0)`.

Both engines read the source from one: the Time engine's analysis and
flattener the last 20 ms of it, the Spectral engine its frames -- two frames
of history, so a switch to it can be primed. The dry signal keeps one too, for
Mix to line up with a latency.
*/

/// How many samples a ring keeps: two of the largest frames.
pub const RING: usize = 8192;
const MASK: usize = RING - 1;

pub struct Ring {
    buf: Box<[f64; RING]>,
    /// Where the newest sample is.
    pos: usize,
}

impl Ring {
    pub fn new() -> Ring {
        Ring { buf: Box::new([0.0; RING]), pos: 0 }
    }

    /// Silence, all the way back.
    pub fn clear(&mut self) {
        self.buf.fill(0.0);
    }

    #[inline]
    pub fn push(&mut self, x: f64) {
        self.pos = (self.pos + 1) & MASK;
        self.buf[self.pos] = x;
    }

    /// The sample `k` samples ago: 0 is the newest. Past the ring's length
    /// it wraps, which no reader asks for.
    #[inline]
    pub fn at(&self, k: usize) -> f64 {
        self.buf[self.pos.wrapping_sub(k) & MASK]
    }
}

impl Default for Ring {
    fn default() -> Self {
        Ring::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_newest_is_at_zero_and_the_oldest_a_ring_back() {
        let mut r = Ring::new();
        for i in 0..RING + 10 {
            r.push(i as f64);
        }
        assert_eq!(r.at(0), (RING + 9) as f64);
        assert_eq!(r.at(9), RING as f64);
        assert_eq!(r.at(RING - 1), 10.0);
        r.clear();
        assert_eq!(r.at(0), 0.0);
        assert_eq!(Ring::default().at(5), 0.0);
    }
}
