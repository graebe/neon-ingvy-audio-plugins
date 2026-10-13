// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The parameters: one table every shell declares from.

ONE TABLE, THREE READERS. The JUCE shell declares its host parameters from it
(fd_param_* in fd-capi), the Move module's `chain_params` is tested against it
(fd-move), and the engine clamps with it. A range, a default or a unit is
written here and nowhere else.

THE SHAPE: four global parameters, ten for the isolator, then ten for each of
sixteen nodes, in slot order -- `n01_type` .. `n01_channel`, `n02_type` ..:

```text
   0..3     output, scale, adaptive_q, analyzer
   4..13    iso_on, iso_low, iso_mid, iso_high, the three kills,
            iso_freq_lo, iso_freq_hi, iso_slope
  14 + 10·slot + field     type, on, family, order, freq, gain, q,
                           ripple, stopband, channel
```

EMPTY IS A TYPE AND ON IS THE BYPASS. A node is added by giving it a type and
removed by giving it Empty again -- its other values stay, so undo is one
step -- and switched off and on with On, keeping its place on the curve. An
instance whose every type is Empty, with the isolator off and Output at 0 dB,
is bit-exact.

THE CHOICES' LABELS ARE FROZEN. A host keeps a choice as a fraction of its
labels, so a label added later would move every automation point saved
before it: every type, family, order and channel is here from the first
version.

APPEND ONLY. The index is what a host stores in a session.
*/

use core::fmt::{self, Write};
use ni_dsp::fmt::{atof, f};
pub use ni_params::{Kind, Travel};

/// How many nodes there are.
pub const NODES: usize = 16;

/// How many parameters a node has.
pub const NODE_FIELDS: usize = 10;

/// The first node parameter's index.
pub const NODE_BASE: usize = 14;

/// How many parameters there are.
pub const PARAM_COUNT: usize = NODE_BASE + NODES * NODE_FIELDS;

/// A switch's labels.
pub const SWITCH_LABELS: [&str; 2] = ["Off", "On"];
/// The analyzer's: what it shows behind the curve.
pub const ANALYZER_LABELS: [&str; 4] = ["Off", "Pre", "Post", "Both"];
/// The isolator's crossover slopes: Linkwitz-Riley of order 4 or 8.
pub const SLOPE_LABELS: [&str; 2] = ["LR4 24 dB/oct", "LR8 48 dB/oct"];
/// A node's types; Empty is no filter.
pub const TYPE_LABELS: [&str; 10] =
    ["Empty", "Bell", "Low Shelf", "High Shelf", "High-Pass", "Low-Pass", "Band-Pass", "Notch", "All-Pass", "Tilt"];
/// The families, as ni_filter::Family::ALL.
pub const FAMILY_LABELS: [&str; 6] = ["Butterworth", "Bessel", "Chebyshev I", "Chebyshev II", "Elliptic", "Linkwitz-Riley"];
/// The orders 1..8, by the roll-off each makes.
pub const ORDER_LABELS: [&str; 8] =
    ["6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "30 dB/oct", "36 dB/oct", "42 dB/oct", "48 dB/oct"];
/// Which of a stereo pair a node filters.
pub const CHANNEL_LABELS: [&str; 5] = ["Stereo", "Left", "Right", "Mid", "Side"];

/// Each node's Freq when nothing has set it: spread across the spectrum, so
/// a node switched on without a frequency is not on top of the last one.
pub const FREQ_DEFAULTS: [f64; NODES] =
    [30.0, 45.0, 70.0, 100.0, 150.0, 220.0, 330.0, 500.0, 750.0, 1100.0, 1600.0, 2400.0, 3600.0, 5400.0, 8000.0, 12000.0];

/// A node's parameters, in their order.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Field {
    Type = 0,
    On,
    Family,
    Order,
    Freq,
    Gain,
    Q,
    Ripple,
    Stopband,
    Channel,
}

impl Field {
    pub const ALL: [Field; NODE_FIELDS] = [
        Field::Type,
        Field::On,
        Field::Family,
        Field::Order,
        Field::Freq,
        Field::Gain,
        Field::Q,
        Field::Ripple,
        Field::Stopband,
        Field::Channel,
    ];
}

/// The parameters. A host index is [`Param::index`].
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Param {
    /// Gain after everything.
    Output,
    /// Every gain (Bell, shelves, Tilt) times this.
    Scale,
    /// A Bell narrows as it boosts or cuts.
    AdaptiveQ,
    /// What the editor shows behind the curve: a view, not the sound.
    Analyzer,
    IsoOn,
    IsoLow,
    IsoMid,
    IsoHigh,
    IsoKillLow,
    IsoKillMid,
    IsoKillHigh,
    /// The crossover frequency between the Low and the Mid band.
    IsoFreqLo,
    /// The crossover frequency between the Mid and the High band.
    IsoFreqHi,
    IsoSlope,
    /// Node `slot` (0..16)'s `field`.
    Node(u8, Field),
}

const GLOBAL: [Param; NODE_BASE] = [
    Param::Output,
    Param::Scale,
    Param::AdaptiveQ,
    Param::Analyzer,
    Param::IsoOn,
    Param::IsoLow,
    Param::IsoMid,
    Param::IsoHigh,
    Param::IsoKillLow,
    Param::IsoKillMid,
    Param::IsoKillHigh,
    Param::IsoFreqLo,
    Param::IsoFreqHi,
    Param::IsoSlope,
];

/// A float's unit: what it is printed with and how typing is read.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Unit {
    Db,
    /// dB whose bottom is silence: "-inf dB".
    DbToSilence,
    Hz,
    Percent,
    /// A bare Q.
    Q,
}

impl Unit {
    pub const fn label(self) -> &'static str {
        match self {
            Unit::Db | Unit::DbToSilence => "dB",
            Unit::Hz => "Hz",
            Unit::Percent => "%",
            Unit::Q => "",
        }
    }
}

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

macro_rules! node_names {
    ($($key:literal $name:literal),* $(,)?) => {
        /// Each node's keys and names, field by field.
        const NODE_KEYS: [[&str; NODE_FIELDS]; NODES] = [$([
            concat!("n", $key, "_type"),
            concat!("n", $key, "_on"),
            concat!("n", $key, "_family"),
            concat!("n", $key, "_order"),
            concat!("n", $key, "_freq"),
            concat!("n", $key, "_gain"),
            concat!("n", $key, "_q"),
            concat!("n", $key, "_ripple"),
            concat!("n", $key, "_stopband"),
            concat!("n", $key, "_channel"),
        ]),*];
        const NODE_NAMES: [[&str; NODE_FIELDS]; NODES] = [$([
            concat!("Filter ", $name, " Type"),
            concat!("Filter ", $name, " On"),
            concat!("Filter ", $name, " Family"),
            concat!("Filter ", $name, " Order"),
            concat!("Filter ", $name, " Freq"),
            concat!("Filter ", $name, " Gain"),
            concat!("Filter ", $name, " Q"),
            concat!("Filter ", $name, " Ripple"),
            concat!("Filter ", $name, " Stopband"),
            concat!("Filter ", $name, " Channel"),
        ]),*];
    };
}

node_names!(
    "01" "1", "02" "2", "03" "3", "04" "4", "05" "5", "06" "6", "07" "7", "08" "8",
    "09" "9", "10" "10", "11" "11", "12" "12", "13" "13", "14" "14", "15" "15", "16" "16",
);

impl Param {
    /// The host index.
    pub const fn index(self) -> usize {
        match self {
            Param::Node(slot, field) => NODE_BASE + slot as usize * NODE_FIELDS + field as usize,
            Param::Output => 0,
            Param::Scale => 1,
            Param::AdaptiveQ => 2,
            Param::Analyzer => 3,
            Param::IsoOn => 4,
            Param::IsoLow => 5,
            Param::IsoMid => 6,
            Param::IsoHigh => 7,
            Param::IsoKillLow => 8,
            Param::IsoKillMid => 9,
            Param::IsoKillHigh => 10,
            Param::IsoFreqLo => 11,
            Param::IsoFreqHi => 12,
            Param::IsoSlope => 13,
        }
    }

    /// The parameter at a host index, if there is one.
    pub fn from_index(i: usize) -> Option<Param> {
        if i < NODE_BASE {
            Some(GLOBAL[i])
        } else if i < PARAM_COUNT {
            let n = i - NODE_BASE;
            Some(Param::Node((n / NODE_FIELDS) as u8, Field::ALL[n % NODE_FIELDS]))
        } else {
            None
        }
    }

    pub fn from_i32(index: i32) -> Option<Param> {
        usize::try_from(index).ok().and_then(Param::from_index)
    }

    /// The parameter a saved state or the Move calls `key`, if any.
    pub fn from_key(key: &str) -> Option<Param> {
        (0..PARAM_COUNT).filter_map(Param::from_index).find(|p| p.info().key == key)
    }

    /// Everything a shell declares this parameter with.
    pub fn info(self) -> Info {
        let db = Unit::Db;
        let (key, name, (kind, unit)) = match self {
            Param::Output => ("output", "Output", float(-24.0, 24.0, 0.0, 0.01, Travel::Linear, true, db)),
            Param::Scale => ("scale", "Scale", float(-200.0, 200.0, 100.0, 0.1, Travel::Linear, true, Unit::Percent)),
            Param::AdaptiveQ => ("adaptive_q", "Adaptive Q", choice(&SWITCH_LABELS, 0, true)),
            Param::Analyzer => ("analyzer", "Analyzer", choice(&ANALYZER_LABELS, 3, false)),
            Param::IsoOn => ("iso_on", "Isolator", choice(&SWITCH_LABELS, 0, true)),
            Param::IsoLow => ("iso_low", "Isolator Low", float(-60.0, 6.0, 0.0, 0.01, Travel::Linear, false, Unit::DbToSilence)),
            Param::IsoMid => ("iso_mid", "Isolator Mid", float(-60.0, 6.0, 0.0, 0.01, Travel::Linear, false, Unit::DbToSilence)),
            Param::IsoHigh => ("iso_high", "Isolator High", float(-60.0, 6.0, 0.0, 0.01, Travel::Linear, false, Unit::DbToSilence)),
            Param::IsoKillLow => ("iso_kill_low", "Kill Low", choice(&SWITCH_LABELS, 0, true)),
            Param::IsoKillMid => ("iso_kill_mid", "Kill Mid", choice(&SWITCH_LABELS, 0, true)),
            Param::IsoKillHigh => ("iso_kill_high", "Kill High", choice(&SWITCH_LABELS, 0, true)),
            Param::IsoFreqLo => ("iso_freq_lo", "Crossover Low-Mid", float(30.0, 2000.0, 250.0, 0.1, Travel::Log, false, Unit::Hz)),
            Param::IsoFreqHi => ("iso_freq_hi", "Crossover Mid-High", float(500.0, 16000.0, 2500.0, 0.1, Travel::Log, false, Unit::Hz)),
            Param::IsoSlope => ("iso_slope", "Crossover Slope", choice(&SLOPE_LABELS, 0, true)),
            Param::Node(slot, field) => {
                let s = (slot as usize).min(NODES - 1);
                let (key, name) = (NODE_KEYS[s][field as usize], NODE_NAMES[s][field as usize]);
                let spec = match field {
                    Field::Type => choice(&TYPE_LABELS, 0, true),
                    Field::On => choice(&SWITCH_LABELS, 1, true),
                    Field::Family => choice(&FAMILY_LABELS, 0, true),
                    Field::Order => choice(&ORDER_LABELS, 1, true),
                    Field::Freq => float(10.0, 22_000.0, FREQ_DEFAULTS[s], 0.01, Travel::Log, false, Unit::Hz),
                    Field::Gain => float(-30.0, 30.0, 0.0, 0.01, Travel::Linear, true, db),
                    /* 0.7071, on the resolution a host keeps, rather than
                     * 1/√2, which is not: design.rs reads it as 1/√2. */
                    #[allow(clippy::approx_constant, reason = "the nearest value a host can keep")]
                    Field::Q => float(0.1, 40.0, 0.7071, 0.0001, Travel::Log, false, Unit::Q),
                    Field::Ripple => float(0.01, 3.0, 1.0, 0.01, Travel::Linear, false, db),
                    Field::Stopband => float(20.0, 120.0, 60.0, 0.1, Travel::Linear, false, db),
                    Field::Channel => choice(&CHANNEL_LABELS, 0, true),
                };
                (key, name, spec)
            }
        };
        Info { key, name, kind, unit }
    }

    pub fn default_value(self) -> f64 {
        self.info().kind.default_value()
    }

    pub fn clamp(self, v: f64) -> f64 {
        self.info().kind.clamp(v)
    }

    pub fn snap(self, v: f64) -> f64 {
        self.info().kind.snap(v)
    }

    pub fn normalise(self, v: f64) -> f64 {
        self.info().kind.normalise(v)
    }

    pub fn denormalise(self, n: f64) -> f64 {
        self.info().kind.denormalise(n)
    }

    /// The value as every shell prints it: "+3.0 dB", "-inf dB", "1.20 kHz",
    /// "250 Hz", "0.71", "100 %", "Notch".
    pub fn write_value(self, out: &mut dyn Write, v: f64) -> fmt::Result {
        let v = if v.is_nan() { self.default_value() } else { self.clamp(v) };
        let info = self.info();
        if let Some(label) = info.kind.choice_text(v) {
            return out.write_str(label);
        }
        let signed = matches!(info.kind, Kind::Float { bipolar: true, .. });
        match info.unit.unwrap_or(Unit::Q) {
            Unit::Db | Unit::DbToSilence => {
                if info.unit == Some(Unit::DbToSilence) && v <= -60.0 {
                    return out.write_str("-inf dB");
                }
                let r = (v * 10.0).round() / 10.0;
                if signed && r > 0.0 {
                    out.write_char('+')?;
                }
                f(out, if r == 0.0 { 0.0 } else { r }, 1)?;
                out.write_str(" dB")
            }
            Unit::Hz => {
                if v >= 10_000.0 {
                    f(out, v / 1000.0, 1)?;
                    out.write_str(" kHz")
                } else if v >= 1000.0 {
                    f(out, v / 1000.0, 2)?;
                    out.write_str(" kHz")
                } else if v >= 100.0 {
                    f(out, v, 0)?;
                    out.write_str(" Hz")
                } else {
                    f(out, v, 1)?;
                    out.write_str(" Hz")
                }
            }
            Unit::Percent => {
                let r = v.round();
                if signed && r > 0.0 {
                    out.write_char('+')?;
                }
                f(out, if r == 0.0 { 0.0 } else { r }, 0)?;
                out.write_str(" %")
            }
            Unit::Q => f(out, v, if v < 10.0 { 2 } else { 1 }),
        }
    }

    /// What a user typed, read as this parameter's value: a choice's label,
    /// or the number at the front as C reads it, in the parameter's unit --
    /// a frequency with k after it in kHz ("1.2k", "1.2 kHz"), a level that
    /// says inf at its bottom. `None` for text with no value in it.
    pub fn parse_value(self, text: &str) -> Option<f64> {
        let t = text.trim();
        let info = self.info();
        if let Some(i) = info.kind.choice_of(t) {
            return Some(i);
        }
        if info.unit == Some(Unit::DbToSilence) && t.to_ascii_lowercase().contains("inf") {
            return Some(self.clamp(-60.0));
        }
        if !t.bytes().any(|b| b.is_ascii_digit()) {
            return None;
        }
        let mut v = atof(t);
        if info.unit == Some(Unit::Hz) && t.bytes().any(|b| b == b'k' || b == b'K') {
            v *= 1000.0;
        }
        Some(self.clamp(v))
    }
}

#[cfg(test)]
mod tests;
