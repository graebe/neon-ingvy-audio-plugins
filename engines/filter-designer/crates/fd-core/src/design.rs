// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
A node's settings to the cascade it runs: ONE pure function, which the
engine calls with its glided values and the picture with the host's. What
the editor draws is therefore the filter the engine runs -- the same design,
from the same code (ni-filter), never a second model of it.

WHAT EACH TYPE USES (the editor disables what a type does not):

```text
  Low Cut, High Cut    Family, Order 1..8, Q as resonance (order 2 and up),
                       Ripple and Stopband as the family takes them
  Band Pass, Notch     Family (Linkwitz-Riley plays as Butterworth), Order
                       1..8 (2N poles), Q as the width between the -3 dB edges
  Bell                 Gain, Q (narrowed by Adaptive Q as it boosts or cuts)
  Low/High Shelf       Gain, Order 1..4, Q on the steepest section
  All Pass             Order 1..8, Q on the steepest section
  Tilt                 Gain: -Gain/2 at DC, +Gain/2 at the top
```

Every gain is times Scale.
*/

use core::f64::consts::FRAC_1_SQRT_2;
use crate::params::{Field, Param};
use ni_filter::{all_pass, bell, family, high_shelf, low_shelf, tilt, Band, Cascade, Family, Prototype};

/// What a node is.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Shape {
    Empty,
    Bell,
    LowShelf,
    HighShelf,
    LowCut,
    HighCut,
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
        Shape::LowCut,
        Shape::HighCut,
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
        matches!(self, Shape::LowCut | Shape::HighCut | Shape::BandPass | Shape::Notch)
    }

    /// Whether Gain does anything.
    pub fn uses_gain(self) -> bool {
        matches!(self, Shape::Bell | Shape::LowShelf | Shape::HighShelf | Shape::Tilt)
    }

    /// The highest order it takes; 0 for none.
    pub fn max_order(self) -> usize {
        match self {
            Shape::LowShelf | Shape::HighShelf => 4,
            Shape::LowCut | Shape::HighCut | Shape::BandPass | Shape::Notch | Shape::AllPass => 8,
            _ => 0,
        }
    }

    /// Whether Q does anything at `order`.
    pub fn uses_q(self, order: usize) -> bool {
        match self {
            Shape::Bell | Shape::BandPass | Shape::Notch => true,
            Shape::LowCut | Shape::HighCut | Shape::LowShelf | Shape::HighShelf | Shape::AllPass => order >= 2,
            Shape::Empty | Shape::Tilt => false,
        }
    }
}

/// Which of a stereo pair a node filters.
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

/// A node's settings, in their own units.
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
}

/// What applies to every node.
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

/// The order a shape plays at.
pub fn order_of(s: &Settings) -> usize {
    s.order.clamp(1, s.shape.max_order().max(1))
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
        Shape::LowCut | Shape::HighCut | Shape::BandPass | Shape::Notch => {
            let band = match s.shape {
                Shape::LowCut => Band::LowCut,
                Shape::HighCut => Band::HighCut,
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
            family(proto, band, s.freq, s.q, fs)
        }
    }
}

/// Node `slot`'s settings as `values` hold them: its Type whether or not it
/// is on (a bypassed node still has a shape to draw).
pub fn settings_of(values: &[f64], slot: u8) -> Settings {
    let v = |f: Field| values[Param::Node(slot, f).index()];
    Settings {
        shape: Shape::from_value(v(Field::Type)),
        family: Family::from_index(v(Field::Family).round().max(0.0) as usize),
        order: v(Field::Order).round().max(0.0) as usize + 1,
        freq: v(Field::Freq),
        gain_db: v(Field::Gain),
        q: v(Field::Q),
        ripple_db: v(Field::Ripple),
        stopband_db: v(Field::Stopband),
    }
}

/// Node `slot`'s channel as `values` hold it.
pub fn channel_of(values: &[f64], slot: u8) -> Channel {
    Channel::from_value(values[Param::Node(slot, Field::Channel).index()])
}

/// Whether node `slot` is heard: a type, and switched on.
pub fn is_heard(values: &[f64], slot: u8) -> bool {
    values[Param::Node(slot, Field::On).index()] >= 0.5 && settings_of(values, slot).shape != Shape::Empty
}

/// Scale and Adaptive Q as `values` hold them.
pub fn globals_of(values: &[f64]) -> Globals {
    Globals {
        scale: values[Param::Scale.index()] / 100.0,
        adaptive: if values[Param::AdaptiveQ.index()] >= 0.5 { 1.0 } else { 0.0 },
    }
}
