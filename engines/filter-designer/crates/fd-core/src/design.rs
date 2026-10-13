// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
A node's settings to the cascade it runs: ONE pure function, which the
engine calls with its glided values and the picture with the host's. What
the editor draws is therefore the filter the engine runs -- the same design,
from the same code (ni-filter), never a second model of it.

WHAT EACH TYPE USES (the editor disables what a type does not):

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

Every gain is times Scale.
*/

use core::f64::consts::FRAC_1_SQRT_2;
use crate::params::{Field, Param, NODES};
use ni_filter::{all_pass, bell, family, high_shelf, low_shelf, tilt, Band, Cascade, Family, Prototype};

/// What a node is.
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

/// The Order index (0..7) a node of `shape` in `family` at Order index
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

/// WHERE A NODE'S HANDLE SITS on the picture, (Hz, dB): a Bell's and a
/// shelf's at Freq and its Gain times Scale; a tilt's, a band-pass's, a
/// notch's and an all-pass's at Freq on 0 dB; a high-pass's or a low-pass's
/// on its own curve at
/// Freq (`cascade`, the node's design at `fs`). `None` for Empty.
pub fn handle(s: &Settings, g: Globals, cascade: &ni_filter::Cascade, fs: f64) -> Option<(f64, f64)> {
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

/// What a filter added at `hz` is: a High-Pass below 30 Hz, a Low-Pass above
/// 15 kHz, a Bell between them.
pub fn shape_for_new(hz: f64) -> Shape {
    if hz < 30.0 {
        Shape::HighPass
    } else if hz > 15_000.0 {
        Shape::LowPass
    } else {
        Shape::Bell
    }
}

/// The longest tail [`tail_seconds`] reports.
pub const TAIL_MAX_SECONDS: f64 = 30.0;

/// THE TAIL for `values` at `fs`: the seconds the output goes on once the
/// input stops, until it is 60 dB down -- the isolator's slowest band, then
/// every heard node, in series, each section's decay in turn. An upper bound,
/// at most [`TAIL_MAX_SECONDS`]: a host that renders this long after the
/// input truncates no ringing filter. Pure; not the audio thread.
pub fn tail_seconds(values: &[f64], fs: f64) -> f64 {
    const DB: f64 = 60.0;
    if !fs.is_finite() || fs <= 0.0 || values.len() < crate::PARAM_COUNT {
        return 0.0;
    }
    let iso = if values[Param::IsoOn.index()] >= 0.5 {
        let (bands, _) = crate::isolator::picture(values, fs);
        bands.iter().map(|path| path.iter().map(|c| c.decay_samples(DB)).sum::<f64>()).fold(0.0, f64::max)
    } else {
        0.0
    };
    let g = globals_of(values);
    let nodes: f64 = (0..NODES as u8)
        .filter(|&slot| is_heard(values, slot))
        .map(|slot| {
            let s = settings_of(values, slot);
            design(&s, prototype(&s).as_ref(), g, fs).0.decay_samples(DB)
        })
        .sum();
    ((iso + nodes) / fs).min(TAIL_MAX_SECONDS)
}
