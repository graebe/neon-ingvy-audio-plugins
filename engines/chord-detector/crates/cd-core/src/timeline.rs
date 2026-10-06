// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The history's clock: quarter notes that only ever go forward.

THE HOST'S POSITION IS NOT A TIMELINE. It jumps back at every loop and every
seek, and it stands still while the transport is stopped -- and a history that
followed it would fold a looping clip onto itself and freeze whenever playback
stops, which is exactly when somebody plays a chord to see its name. So the
history keeps its own clock: it advances by every block at the host's tempo,
playing or not, and never goes back.

WHAT IT TAKES FROM THE HOST is the tempo and where the bar lines fall. While
the transport plays, the bar line is re-anchored every block from the host's
position (`ppq` modulo the bar, bars counted from 0, as the ground counts
them), so the history's bars are the song's bars. When it stops, the last tempo
and the last bar line carry on, so live playing still scrolls in time.

A host that reports no tempo is at the ground's default, 120.
*/

use ground_core::{bar_quarters, Transport, DEFAULT_BPM};

/// The history's clock.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Timeline {
    sample_rate: f64,
    /// Quarters since the instance began, at the current block's first sample.
    now: f64,
    bpm: f64,
    /// The bar's length in quarters.
    bar: f64,
    /// A point on the clock where a bar begins.
    bar_origin: f64,
    playing: bool,
}

impl Timeline {
    /// A clock at 0, 120 BPM, 4/4.
    pub fn new(sample_rate: f64) -> Timeline {
        Timeline {
            sample_rate,
            now: 0.0,
            bpm: DEFAULT_BPM,
            bar: 4.0,
            bar_origin: 0.0,
            playing: false,
        }
    }

    /// A new sample rate. The clock does not jump.
    pub fn set_sample_rate(&mut self, sample_rate: f64) {
        self.sample_rate = sample_rate;
    }

    /// The top of a block: take the host's tempo and bar line, if it gave any.
    pub fn begin(&mut self, transport: Option<&Transport>) {
        let Some(t) = transport else {
            self.playing = false;
            return;
        };
        if t.bpm.is_finite() && t.bpm > 0.0 {
            self.bpm = t.bpm;
        }
        self.bar = bar_quarters(t.num, t.den);
        self.playing = t.playing && t.ppq.is_finite();
        if self.playing {
            self.bar_origin = self.now - t.ppq.rem_euclid(self.bar);
        }
    }

    /// The clock at `offset` samples into the current block.
    pub fn at(&self, offset: u32) -> f64 {
        self.now + self.quarters(offset as usize)
    }

    /// The bottom of a block of `frames` samples.
    pub fn end(&mut self, frames: usize) {
        self.now += self.quarters(frames);
    }

    fn quarters(&self, frames: usize) -> f64 {
        if self.sample_rate.is_finite() && self.sample_rate > 0.0 {
            frames as f64 * self.bpm / (60.0 * self.sample_rate)
        } else {
            0.0
        }
    }

    /// Quarters since the instance began.
    pub fn now(&self) -> f64 {
        self.now
    }

    /// The tempo the clock runs at.
    pub fn bpm(&self) -> f64 {
        self.bpm
    }

    /// The bar's length in quarters.
    pub fn bar(&self) -> f64 {
        self.bar
    }

    /// A point on the clock where a bar begins; every bar line is this plus a
    /// whole number of bars.
    pub fn bar_origin(&self) -> f64 {
        self.bar_origin
    }

    /// Whether the host's transport was playing at the last block.
    pub fn playing(&self) -> bool {
        self.playing
    }
}
