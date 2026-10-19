// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
A filter's settings to the cascade it runs: ONE pure function, which an
engine calls with its glided values and a picture with the host's. What an
editor draws is therefore the filter the engine runs -- the same design, from
the same code (ni-filter), never a second model of it.

WHAT EACH TYPE USES (an editor disables what a type does not):

```text
  High-Pass, Low-Pass  Family, Order 1..8, Q on the highest-Q pole pair
                       (order 2 and up), Ripple and Stopband as the family
                       takes them
  Band-Pass, Notch     Family (Linkwitz-Riley plays as Butterworth), Order
                       1..8 (2N poles), Q as the width between the -3 dB edges
  Bell                 Gain, Q (narrowed by Adaptive Q as it boosts or cuts)
  Low/High Shelf       Gain, Order 1..4, Q on the steepest section
  All-Pass             Order 1..8, Q on the steepest section
  Tilt                 Gain: -Gain/2 at DC, +Gain/2 at the top
```

Every gain is times Scale ([`Globals`]).
*/

use core::f64::consts::FRAC_1_SQRT_2;
use ni_filter::{all_pass, bell, family, family_matched, high_shelf, low_shelf, tilt, Band, Cascade, Family, Prototype};

/// What a filter is.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Shape {
    Empty,
    Bell,
    LowShelf,
    HighShelf,
    HighPass,
    LowPass,
    BandPass,
    Notch,
    AllPass,
    Tilt,
}

impl Shape {
    pub const ALL: [Shape; 10] = [
        Shape::Empty,
        Shape::Bell,
        Shape::LowShelf,
        Shape::HighShelf,
        Shape::HighPass,
        Shape::LowPass,
        Shape::BandPass,
        Shape::Notch,
        Shape::AllPass,
        Shape::Tilt,
    ];

    /// The shape at a Type value.
    pub fn from_value(v: f64) -> Shape {
        Shape::ALL[(v.round().max(0.0) as usize).min(9)]
    }

    /// Whether a family shapes it.
    pub fn uses_family(self) -> bool {
        matches!(self, Shape::HighPass | Shape::LowPass | Shape::BandPass | Shape::Notch)
    }

    /// Whether Gain does anything.
    pub fn uses_gain(self) -> bool {
        matches!(self, Shape::Bell | Shape::LowShelf | Shape::HighShelf | Shape::Tilt)
    }

    /// The highest order it takes; 0 for none.
    pub fn max_order(self) -> usize {
        match self {
            Shape::LowShelf | Shape::HighShelf => 4,
            Shape::HighPass | Shape::LowPass | Shape::BandPass | Shape::Notch | Shape::AllPass => 8,
            _ => 0,
        }
    }

    /// Whether Q does anything at `order`.
    pub fn uses_q(self, order: usize) -> bool {
        match self {
            Shape::Bell | Shape::BandPass | Shape::Notch => true,
            Shape::HighPass | Shape::LowPass | Shape::LowShelf | Shape::HighShelf | Shape::AllPass => order >= 2,
            Shape::Empty | Shape::Tilt => false,
        }
    }

    /// Whether Matched changes its design: a high-pass's or a low-pass's.
    pub fn uses_matched(self) -> bool {
        matches!(self, Shape::HighPass | Shape::LowPass)
    }
}

/// Which of a stereo pair a filter filters.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Channel {
    Stereo,
    Left,
    Right,
    /// (L + R) / 2.
    Mid,
    /// (L - R) / 2.
    Side,
}

impl Channel {
    pub const ALL: [Channel; 5] = [Channel::Stereo, Channel::Left, Channel::Right, Channel::Mid, Channel::Side];

    pub fn from_value(v: f64) -> Channel {
        Channel::ALL[(v.round().max(0.0) as usize).min(4)]
    }
}

/// A filter's settings, in their own units.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Settings {
    pub shape: Shape,
    pub family: Family,
    /// 1..8.
    pub order: usize,
    pub freq: f64,
    pub gain_db: f64,
    pub q: f64,
    pub ripple_db: f64,
    pub stopband_db: f64,
    /// A high-pass or low-pass matched to its analog magnitude rather than
    /// the bilinear transform's.
    pub matched: bool,
}

/// What applies to every filter of an engine.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Globals {
    /// Scale, as a factor (1 is 100 %).
    pub scale: f64,
    /// Adaptive Q, 0 (off) to 1 (on): it glides.
    pub adaptive: f64,
}

impl Globals {
    pub const NEUTRAL: Globals = Globals { scale: 1.0, adaptive: 0.0 };
}

/// WHAT A NODE IS ASKED TO PLAY: its settings -- its shape whether or not it
/// is on, so a bypassed filter still has a curve to draw -- the lane it
/// filters, and whether it is switched on. How an engine's parameters spell
/// these is the engine's business; the node reads only this.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Target {
    pub settings: Settings,
    pub channel: Channel,
    pub on: bool,
}

impl Target {
    /// Whether it is heard: a shape, and switched on.
    pub fn is_heard(&self) -> bool {
        self.on && self.settings.shape != Shape::Empty
    }
}

/// The order a shape plays at.
pub fn order_of(s: &Settings) -> usize {
    s.order.clamp(1, s.shape.max_order().max(1))
}

/// The Order index (0..7) a filter of `shape` in `family` at Order index
/// `order` plays: at most its type's highest, and for a Linkwitz-Riley
/// high-pass or low-pass -- Butterworth squared, so only even orders exist --
/// the even order at or above it. What an editor snaps a choice to.
pub fn order_played(shape: Shape, family: Family, order: usize) -> usize {
    let top = shape.max_order().max(1) - 1;
    let o = order.min(top);
    if family == Family::LinkwitzRiley && matches!(shape, Shape::HighPass | Shape::LowPass) {
        (o | 1).min(top)
    } else {
        o
    }
}

/// The prototype a settings' family design needs; `None` for a shape that
/// has no family.
pub fn prototype(s: &Settings) -> Option<Prototype> {
    s.shape.uses_family().then(|| Prototype::new(s.family, order_of(s), s.ripple_db, s.stopband_db))
}

/// THE DESIGN: the cascade `s` runs at `fs`, with `proto` its family's
/// prototype (prototype()) when it has one, and whether the rate clamped an
/// edge.
pub fn design(s: &Settings, proto: Option<&Prototype>, g: Globals, fs: f64) -> (Cascade, bool) {
    /* Q's default is 0.7071, the nearest a host keeps to 1/√2: read as 1/√2,
     * so a cut at its default is exactly the family's. */
    let s = &Settings { q: if (s.q - FRAC_1_SQRT_2).abs() < 1e-4 { FRAC_1_SQRT_2 } else { s.q }, ..*s };
    let gain = s.gain_db * g.scale;
    let order = order_of(s);
    let top = ni_filter::MAX_HZ_FRACTION * fs;
    match s.shape {
        Shape::Empty => (Cascade::identity(), false),
        Shape::Bell => {
            let q = s.q * 10f64.powf(g.adaptive * gain.abs() / 40.0);
            (bell(s.freq, gain, q, fs), s.freq > top)
        }
        Shape::LowShelf => (low_shelf(s.freq, gain, s.q, order, fs), s.freq > top),
        Shape::HighShelf => (high_shelf(s.freq, gain, s.q, order, fs), s.freq > top),
        Shape::Tilt => (tilt(s.freq, gain, fs), s.freq > top),
        Shape::AllPass => (all_pass(s.freq, s.q, order, fs), s.freq > top),
        Shape::HighPass | Shape::LowPass | Shape::BandPass | Shape::Notch => {
            let band = match s.shape {
                Shape::HighPass => Band::HighPass,
                Shape::LowPass => Band::LowPass,
                Shape::BandPass => Band::BandPass,
                _ => Band::Notch,
            };
            let made;
            let proto = match proto {
                Some(p) => p,
                None => {
                    made = Prototype::new(s.family, order, s.ripple_db, s.stopband_db);
                    &made
                }
            };
            if s.matched {
                family_matched(proto, band, s.freq, s.q, fs)
            } else {
                family(proto, band, s.freq, s.q, fs)
            }
        }
    }
}

/// Samples `s`'s design at `fs` rings on once its input stops, until it is
/// `db` down: what a tail is made of. Pure; not the audio thread.
pub fn decay_samples(s: &Settings, g: Globals, fs: f64, db: f64) -> f64 {
    design(s, prototype(s).as_ref(), g, fs).0.decay_samples(db)
}

/// Whether designing `s` at `fs` may weigh realisations of its sections
/// (ni-filter: a shelf's, a matched cut's), which costs a design far more
/// than mapping a prototype does: from a sixteenth of the rate up, a margin
/// under the eighth where ni-filter starts to weigh, for the sections of a
/// cut that sit above its Freq. A node budgets them (node.rs).
pub fn weighs(s: &Settings, fs: f64) -> bool {
    let shape = matches!(s.shape, Shape::LowShelf | Shape::HighShelf) || s.matched;
    shape && s.freq >= fs / 16.0
}

/// WHERE A FILTER'S HANDLE SITS on a picture, (Hz, dB): a Bell's and a
/// shelf's at Freq and its Gain times Scale; a tilt's, a band-pass's, a
/// notch's and an all-pass's at Freq on 0 dB; a high-pass's or a low-pass's
/// on its own curve at Freq (`cascade`, its design at `fs`). `None` for
/// Empty.
pub fn handle(s: &Settings, g: Globals, cascade: &Cascade, fs: f64) -> Option<(f64, f64)> {
    let db = match s.shape {
        Shape::Empty => return None,
        Shape::Bell | Shape::LowShelf | Shape::HighShelf => s.gain_db * g.scale,
        Shape::HighPass | Shape::LowPass => cascade.db(s.freq.min(0.4999 * fs), fs),
        Shape::Tilt | Shape::BandPass | Shape::Notch | Shape::AllPass => 0.0,
    };
    Some((s.freq, db))
}

/// A Bell's, a band-pass's or a notch's -3 dB edges in Hz, below and above
/// Freq, for its Q (`q`): Freq · 2^(∓N/2), N = 2·asinh(1/2Q)/ln 2 octaves.
/// `None` for a shape whose Q is no width.
pub fn edges(s: &Settings) -> Option<(f64, f64)> {
    if !matches!(s.shape, Shape::Bell | Shape::BandPass | Shape::Notch) {
        return None;
    }
    let half = (1.0 / (2.0 * s.q)).asinh() / core::f64::consts::LN_2;
    Some((s.freq * (-half).exp2(), s.freq * half.exp2()))
}

/// The Q whose edge sits at `edge_hz` for a centre at `freq`: the inverse of
/// [`edges`], 0.1..40.
pub fn q_for_edge(freq: f64, edge_hz: f64) -> f64 {
    let octaves = (2.0 * (edge_hz / freq).log2().abs()).max(0.05);
    (1.0 / (2.0 * (octaves * core::f64::consts::LN_2 / 2.0).sinh())).clamp(0.1, 40.0)
}
