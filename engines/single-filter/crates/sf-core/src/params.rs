// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The parameters: one table per product, every shell declares from it.

ONE FIELD LIST, NINE LAYOUTS. A single filter has at most the fields of one
NI Filter Designer node and the levels around it ([`Field`]). Each product
declares only the ones its type uses -- NI High Pass has no Gain, NI Tilt no
Q -- in the order of its layout ([`layout`]), and the host index is the
position there:

```text
  High Pass, Low Pass    freq q order family ripple stopband matched channel output analyzer
  Band Pass, Notch       freq q order family ripple stopband channel output analyzer
  Bell                   freq gain q adaptive_q channel output analyzer
  Low Shelf, High Shelf  freq gain q order channel output analyzer
  Tilt                   freq gain channel output analyzer
  All Pass               freq q order channel output analyzer
```

APPEND ONLY. The index is what a host stores in a session, so a layout only
ever grows at its end; tests/params freeze each one.

Ranges, defaults and words are Filter Designer's for the same field (ripple
1 dB, stopband 60 dB, Q 0.7071 ...), so a single filter sounds and reads as
the node it was. What differs: Freq's default is the product's
([`freq_default`]); Band Pass and Notch offer five families, since a
Linkwitz-Riley band-pass plays as a Butterworth; a shelf's slopes stop at
24 dB/oct; an All Pass has orders, not slopes.

THE CHOICES' LABELS ARE FROZEN (ni_filter_node::words).
*/

use core::fmt;
use ni_filter_node::words::{self, Unit, ANALYZER_LABELS, CHANNEL_LABELS, FAMILY_LABELS, ORDER_LABELS, SWITCH_LABELS};
use ni_filter_node::Shape;
pub use ni_params::{Kind, Travel};

/// A single filter's fields: every one any product has.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Field {
    Freq,
    Gain,
    Q,
    /// A Bell narrows as it boosts or cuts.
    AdaptiveQ,
    /// The slope (an All Pass's order).
    Order,
    Family,
    Ripple,
    Stopband,
    /// A high-pass or low-pass matched to its analog magnitude.
    Matched,
    Channel,
    /// Gain after the filter.
    Output,
    /// What the editor shows behind the curve: a view, not the sound.
    Analyzer,
}

/// How many fields there are: an engine keeps a value for each.
pub const FIELDS: usize = 12;

impl Field {
    pub const ALL: [Field; FIELDS] = [
        Field::Freq,
        Field::Gain,
        Field::Q,
        Field::AdaptiveQ,
        Field::Order,
        Field::Family,
        Field::Ripple,
        Field::Stopband,
        Field::Matched,
        Field::Channel,
        Field::Output,
        Field::Analyzer,
    ];

    /// Where an engine keeps its value.
    pub const fn slot(self) -> usize {
        self as usize
    }
}

use Field::*;

const CUT: &[Field] = &[Freq, Q, Order, Family, Ripple, Stopband, Matched, Channel, Output, Analyzer];
const BAND: &[Field] = &[Freq, Q, Order, Family, Ripple, Stopband, Channel, Output, Analyzer];
const BELL: &[Field] = &[Freq, Gain, Q, AdaptiveQ, Channel, Output, Analyzer];
const SHELF: &[Field] = &[Freq, Gain, Q, Order, Channel, Output, Analyzer];
const TILT: &[Field] = &[Freq, Gain, Channel, Output, Analyzer];
const ALL_PASS: &[Field] = &[Freq, Q, Order, Channel, Output, Analyzer];

/// The products' shapes: every one but Empty.
pub const SHAPES: [Shape; 9] = [
    Shape::HighPass,
    Shape::LowPass,
    Shape::BandPass,
    Shape::Notch,
    Shape::Bell,
    Shape::LowShelf,
    Shape::HighShelf,
    Shape::Tilt,
    Shape::AllPass,
];

/// A product's parameters, in host order. Empty has none.
pub fn layout(shape: Shape) -> &'static [Field] {
    match shape {
        Shape::HighPass | Shape::LowPass => CUT,
        Shape::BandPass | Shape::Notch => BAND,
        Shape::Bell => BELL,
        Shape::LowShelf | Shape::HighShelf => SHELF,
        Shape::Tilt => TILT,
        Shape::AllPass => ALL_PASS,
        Shape::Empty => &[],
    }
}

/// The field at host index `i` of `shape`'s table.
pub fn field_at(shape: Shape, i: usize) -> Option<Field> {
    layout(shape).get(i).copied()
}

/// `field`'s host index in `shape`'s table, if the product has it.
pub fn index_of(shape: Shape, field: Field) -> Option<usize> {
    layout(shape).iter().position(|&f| f == field)
}

/// The field of `shape` a saved state or the Move calls `key`.
pub fn from_key(shape: Shape, key: &str) -> Option<Field> {
    layout(shape).iter().copied().find(|&f| info(shape, f).key == key)
}

/// Freq when nothing has set it: where the product is first heard doing
/// something useful.
pub fn freq_default(shape: Shape) -> f64 {
    match shape {
        Shape::HighPass => 100.0,
        Shape::LowPass => 10_000.0,
        Shape::LowShelf => 200.0,
        Shape::HighShelf => 5_000.0,
        _ => 1_000.0,
    }
}

/// A shelf's slopes: its orders stop at 4.
const SHELF_ORDER_LABELS: &[&str] = {
    let all: &[&str] = &ORDER_LABELS;
    all.split_at(4).0
};

/// A band-pass's and a notch's families: Linkwitz-Riley plays as
/// Butterworth there, so it is not offered.
const BAND_FAMILY_LABELS: &[&str] = {
    let all: &[&str] = &FAMILY_LABELS;
    all.split_at(5).0
};

/// An All Pass's orders: it has no slope to name them by.
const ORDINAL_LABELS: [&str; 8] = ["1st order", "2nd order", "3rd order", "4th order", "5th order", "6th order", "7th order", "8th order"];

/// What a shell needs to declare one parameter.
#[derive(Clone, Copy, Debug)]
pub struct Info {
    /// The stable identifier: what a saved state and the Move call it.
    pub key: &'static str,
    /// The name a host shows.
    pub name: &'static str,
    pub kind: Kind,
    /// A number's unit; `None` for a choice.
    pub unit: Option<Unit>,
}

const fn float(min: f64, max: f64, default: f64, resolution: f64, travel: Travel, bipolar: bool, unit: Unit) -> (Kind, Option<Unit>) {
    (Kind::Float { min, max, default, resolution, travel, bipolar }, Some(unit))
}

const fn choice(labels: &'static [&'static str], default: usize, automatable: bool) -> (Kind, Option<Unit>) {
    (Kind::Choice { labels, default, automatable }, None)
}

/// Everything a shell declares `field` of `shape` with.
pub fn info(shape: Shape, field: Field) -> Info {
    let (key, name, (kind, unit)) = match field {
        Freq => ("freq", "Freq", float(10.0, 22_000.0, freq_default(shape), 0.01, Travel::Log, false, Unit::Hz)),
        Gain => ("gain", "Gain", float(-30.0, 30.0, 0.0, 0.01, Travel::Linear, true, Unit::Db)),
        /* 0.7071, on the resolution a host keeps, rather than 1/√2, which
         * is not: the design reads it as 1/√2. */
        #[allow(clippy::approx_constant, reason = "the nearest value a host can keep")]
        Q => ("q", "Q", float(0.1, 40.0, 0.7071, 0.0001, Travel::Log, false, Unit::Q)),
        AdaptiveQ => ("adaptive_q", "Adaptive Q", choice(&SWITCH_LABELS, 0, true)),
        Order => match shape {
            Shape::AllPass => ("order", "Order", choice(&ORDINAL_LABELS, 1, true)),
            Shape::LowShelf | Shape::HighShelf => ("order", "Slope", choice(SHELF_ORDER_LABELS, 1, true)),
            _ => ("order", "Slope", choice(&ORDER_LABELS, 1, true)),
        },
        Family => match shape {
            Shape::BandPass | Shape::Notch => ("family", "Family", choice(BAND_FAMILY_LABELS, 0, true)),
            _ => ("family", "Family", choice(&FAMILY_LABELS, 0, true)),
        },
        Ripple => ("ripple", "Ripple", float(0.01, 3.0, 1.0, 0.01, Travel::Linear, false, Unit::Db)),
        Stopband => ("stopband", "Stopband", float(20.0, 120.0, 60.0, 0.1, Travel::Linear, false, Unit::Db)),
        Matched => ("matched", "Matched", choice(&SWITCH_LABELS, 0, true)),
        Channel => ("channel", "Channel", choice(&CHANNEL_LABELS, 0, true)),
        Output => ("output", "Output", float(-24.0, 24.0, 0.0, 0.01, Travel::Linear, true, Unit::Db)),
        Analyzer => ("analyzer", "Analyzer", choice(&ANALYZER_LABELS, 3, false)),
    };
    Info { key, name, kind, unit }
}

/// Every field's default for `shape`, by [`Field::slot`]: the ones the
/// product lacks too, so an engine's design reads a whole set of settings.
pub fn defaults(shape: Shape) -> [f64; FIELDS] {
    Field::ALL.map(|f| info(shape, f).kind.default_value())
}

/// `v` of `field` as every shell prints it ("+3.0 dB", "1.20 kHz", "0.71",
/// "Elliptic").
pub fn write_value(shape: Shape, field: Field, out: &mut dyn fmt::Write, v: f64) -> fmt::Result {
    let i = info(shape, field);
    words::write(i.kind, i.unit, out, v)
}

/// What a user typed, read as a value of `field`: a choice's label, or the
/// number at the front, in the field's unit ("1.2k"). `None` for text with
/// no value in it.
pub fn parse_value(shape: Shape, field: Field, text: &str) -> Option<f64> {
    let i = info(shape, field);
    words::parse(i.kind, i.unit, text)
}
