// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The parameters: one table every shell declares from.

ONE TABLE, THREE READERS. The JUCE shell declares its host parameters from it
(bc_param_* in bc-capi), the Move module's `chain_params` is tested against it
(bc-move), and the engine clamps with it. A range, a default or a unit is
written here and nowhere else.

THE FILTER is the last four, appended: a resonant low-pass after the crusher,
off by default, so a set saved before it existed sounds as it did.

THREE KINDS. A `Float` runs from `min` to `max` in its own unit -- dB, bits,
%, Hz, octaves -- on a linear or a logarithmic travel; a `Toggle` is off or
on; a `Choice` is one of its words, by index. The engine takes every value in
the unit the table names, so a host's value, a typed one and a Move knob's are
the same number.

ONE IS THE WINDOW'S. Refresh -- how often the editor's pictures take a new
look -- changes nothing the engine does. It is in the table so that it is
declared once, saved with a set like the rest, and marked for a host not to
automate (`Info::automatable`), as NI Chord-Detector's window settings are.

THE TRAVEL IS THE TABLE'S TOO. Rate runs over more than three decades, and a
linear knob would spend nine tenths of its turn above 10 kHz, so the table says
it is logarithmic and `normalise`/`denormalise` are the one mapping every shell
uses.

THE TEXT IS THE ENGINE'S. `write_value` prints a value the way every shell
shows it, and `parse_value` reads what a user types, locale-free (ni_dsp::fmt):
"12k" and "12 kHz" are both 12000 Hz.

APPEND ONLY. The index is what a host stores in a session, so inserting a
parameter would re-point every set saved before it.
*/

use core::fmt::{self, Write};
use ni_dsp::fmt::{atof, f};

/// The parameters, in the order the shell declares them: a host index IS this
/// value.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
#[repr(i32)]
pub enum Param {
    /// Gain into the crusher, before the pre-filter and the quantiser.
    Drive = 0,
    /// The quantiser's resolution: 2^bits steps across full scale.
    Bits,
    /// From even steps (0 %) to µ-law steps, fine near silence (100 %).
    Shape,
    /// Mid-rise steps: no level at zero, so silence becomes a half step.
    MidRise,
    /// Triangular noise of up to a step, added before the quantiser.
    Dither,
    /// First-order antiderivative antialiasing of the quantiser.
    AntiAlias,
    /// The sample-and-hold's rate.
    Rate,
    /// Random deviation of the hold's clock.
    Jitter,
    /// A low-pass at Rate/2 before the hold.
    PreFilter,
    /// A low-pass after the hold, at Rate/2 moved by `PostOctave`.
    PostFilter,
    /// Where the post-filter sits, in octaves from Rate/2.
    PostOctave,
    /// Dry/wet.
    Mix,
    /// Gain after the mix.
    Output,
    /// How often the editor's pictures take a new look. The window's alone.
    Refresh,
    /// The resonant low-pass after the crusher, before the mix: on or off.
    Filter,
    /// Its cutoff.
    Cutoff,
    /// How far its response rises at the cutoff over a flat (Butterworth) one.
    Peak,
    /// Its slope: 12, 24 or 48 dB per octave.
    Slope,
}

/// How many parameters there are.
pub const PARAM_COUNT: usize = 18;

/// Slope's choices, in index order: two, four and eight poles.
pub const SLOPE_LABELS: [&str; 3] = ["12 dB", "24 dB", "48 dB"];

/// Refresh's choices, in index order, and the pictures' period for each in
/// milliseconds -- 0 is every frame the editor draws.
pub const REFRESH_LABELS: [&str; 4] = ["Live", "10/s", "4/s", "1/s"];
pub const REFRESH_PERIOD_MS: [f64; 4] = [0.0, 100.0, 250.0, 1000.0];

/// A float's unit: what it is printed with and how typing is read.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Unit {
    Db,
    Bits,
    Percent,
    Hz,
    Octaves,
}

impl Unit {
    /// The finest step a host keeps of a value in this unit: well below
    /// what a knob's drag can tell apart, and coarse enough that a value on
    /// it -- 5.5 bits, 7000 Hz -- survives a host's float normalisation and
    /// comes back exactly, so a session reopens with the numbers it saved.
    pub const fn resolution(self) -> f64 {
        match self {
            Unit::Db | Unit::Percent => 0.01,
            Unit::Bits | Unit::Octaves => 0.001,
            Unit::Hz => 0.1,
        }
    }

    /// The unit as a host lists it beside the value.
    pub const fn label(self) -> &'static str {
        match self {
            Unit::Db => "dB",
            Unit::Bits => "bits",
            Unit::Percent => "%",
            Unit::Hz => "Hz",
            Unit::Octaves => "oct",
        }
    }
}

/// How a float's knob travels from `min` to `max`.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Travel {
    Linear,
    /// Equal ratios in equal turns. Only for a range above zero.
    Log,
}

/// What a parameter is.
#[derive(Clone, Copy, PartialEq, Debug)]
pub enum Kind {
    Float {
        min: f64,
        max: f64,
        default: f64,
        unit: Unit,
        travel: Travel,
        /// Whether the value runs both ways about zero, so a knob draws its
        /// arc from the centre -- which is why every such range is symmetric:
        /// the centre of the travel IS zero.
        bipolar: bool,
    },
    Toggle {
        default: bool,
    },
    /// One of `choices`, by index.
    Choice {
        choices: &'static [&'static str],
        default: u8,
    },
}

/// What a shell needs to declare one parameter.
#[derive(Clone, Copy, Debug)]
pub struct Info {
    /// The stable identifier: what a saved state and the Move call it.
    pub key: &'static str,
    /// The name a host shows.
    pub name: &'static str,
    pub kind: Kind,
    /// Whether a host should offer it for automation: every control of the
    /// sound is; a setting of the window is not.
    pub automatable: bool,
}

/// A toggle's two words, in index order.
pub const TOGGLE_LABELS: [&str; 2] = ["Off", "On"];

const fn float(min: f64, max: f64, default: f64, unit: Unit, travel: Travel, bipolar: bool) -> Kind {
    Kind::Float { min, max, default, unit, travel, bipolar }
}

impl Param {
    /// Every parameter, in index order: `ALL[i] as i32 == i`.
    pub const ALL: [Param; PARAM_COUNT] = [
        Param::Drive,
        Param::Bits,
        Param::Shape,
        Param::MidRise,
        Param::Dither,
        Param::AntiAlias,
        Param::Rate,
        Param::Jitter,
        Param::PreFilter,
        Param::PostFilter,
        Param::PostOctave,
        Param::Mix,
        Param::Output,
        Param::Refresh,
        Param::Filter,
        Param::Cutoff,
        Param::Peak,
        Param::Slope,
    ];

    /// The parameter at a host index, if there is one.
    pub fn from_i32(index: i32) -> Option<Param> {
        usize::try_from(index).ok().and_then(|i| Param::ALL.get(i)).copied()
    }

    /// The parameter a saved state or the Move calls `key`, if any.
    pub fn from_key(key: &str) -> Option<Param> {
        Param::ALL.into_iter().find(|p| p.info().key == key)
    }

    /// Everything a shell declares this parameter with.
    pub const fn info(self) -> Info {
        use Travel::{Linear, Log};
        let (key, name, kind) = match self {
            Param::Drive => ("drive", "Drive", float(-24.0, 24.0, 0.0, Unit::Db, Linear, true)),
            Param::Bits => ("bits", "Bits", float(1.0, 16.0, 8.0, Unit::Bits, Linear, false)),
            Param::Shape => ("shape", "Shape", float(0.0, 100.0, 0.0, Unit::Percent, Linear, false)),
            Param::MidRise => ("mid_rise", "Mid-Rise", Kind::Toggle { default: false }),
            Param::Dither => ("dither", "Dither", float(0.0, 100.0, 0.0, Unit::Percent, Linear, false)),
            Param::AntiAlias => ("anti_alias", "Anti-Alias", Kind::Toggle { default: false }),
            Param::Rate => ("rate", "Rate", float(50.0, 96_000.0, 12_000.0, Unit::Hz, Log, false)),
            Param::Jitter => ("jitter", "Jitter", float(0.0, 100.0, 0.0, Unit::Percent, Linear, false)),
            Param::PreFilter => ("pre_filter", "Pre-Filter", Kind::Toggle { default: false }),
            Param::PostFilter => ("post_filter", "Post-Filter", Kind::Toggle { default: false }),
            Param::PostOctave => ("post_octave", "Octave", float(-3.0, 3.0, 0.0, Unit::Octaves, Linear, true)),
            Param::Mix => ("mix", "Mix", float(0.0, 100.0, 100.0, Unit::Percent, Linear, false)),
            Param::Output => ("output", "Output", float(-24.0, 24.0, 0.0, Unit::Db, Linear, true)),
            Param::Refresh => ("refresh", "Refresh", Kind::Choice { choices: &REFRESH_LABELS, default: 3 }),
            Param::Filter => ("filter", "Filter", Kind::Toggle { default: false }),
            Param::Cutoff => ("cutoff", "Cutoff", float(20.0, 20_000.0, 2_000.0, Unit::Hz, Log, false)),
            Param::Peak => ("peak", "Peak", float(0.0, 24.0, 0.0, Unit::Db, Linear, false)),
            Param::Slope => ("slope", "Slope", Kind::Choice { choices: &SLOPE_LABELS, default: 1 }),
        };
        Info { key, name, kind, automatable: !matches!(self, Param::Refresh) }
    }

    /// Whether it shapes what an editor's live pictures of the two
    /// discretizations show -- the quantiser's input and levels, the hold's
    /// takes -- so a look made before it moved is not shown on the new
    /// settings. The filters after the hold, the mix and the window's own do
    /// not.
    pub fn shapes_the_looks(self) -> bool {
        matches!(
            self,
            Param::Drive
                | Param::Bits
                | Param::Shape
                | Param::MidRise
                | Param::Dither
                | Param::AntiAlias
                | Param::Rate
                | Param::Jitter
                | Param::PreFilter
        )
    }

    /// The value a new instance starts at, in the parameter's unit.
    pub const fn default_value(self) -> f64 {
        match self.info().kind {
            Kind::Float { default, .. } => default,
            Kind::Toggle { default } => default as i32 as f64,
            Kind::Choice { default, .. } => default as f64,
        }
    }

    /// The nearest value the parameter can hold: a float inside its range, a
    /// toggle on from one half up, a choice's nearest index. NaN is not a
    /// value; callers drop it first.
    pub fn clamp(self, v: f64) -> f64 {
        match self.info().kind {
            Kind::Float { min, max, .. } => v.clamp(min, max),
            Kind::Toggle { .. } => (v >= 0.5) as i32 as f64,
            Kind::Choice { choices, .. } => v.round().clamp(0.0, (choices.len() - 1) as f64),
        }
    }

    /// The nearest value on the unit's resolution, for a host that keeps a
    /// value as a float; a toggle's 0 or 1. The engine itself takes any
    /// value: this is the shells'.
    pub fn snap(self, v: f64) -> f64 {
        match self.info().kind {
            Kind::Float { unit, .. } => {
                let per = 1.0 / unit.resolution();
                self.clamp((v * per).round() / per)
            }
            Kind::Toggle { .. } | Kind::Choice { .. } => self.clamp(v),
        }
    }

    /// The value as a fraction of the knob's travel, 0..1.
    pub fn normalise(self, v: f64) -> f64 {
        match self.info().kind {
            Kind::Float { min, max, travel, .. } => {
                let v = v.clamp(min, max);
                match travel {
                    Travel::Linear => (v - min) / (max - min),
                    Travel::Log => (v / min).ln() / (max / min).ln(),
                }
            }
            Kind::Toggle { .. } => self.clamp(v),
            Kind::Choice { choices, .. } => self.clamp(v) / (choices.len() - 1) as f64,
        }
    }

    /// The value at a fraction 0..1 of the knob's travel. The inverse of
    /// `normalise`; out-of-range fractions land on the ends.
    pub fn denormalise(self, n: f64) -> f64 {
        let n = if n.is_nan() { 0.0 } else { n.clamp(0.0, 1.0) };
        match self.info().kind {
            Kind::Float { min, max, travel, .. } => match travel {
                Travel::Linear => (min + n * (max - min)).clamp(min, max),
                Travel::Log => (min * (max / min).powf(n)).clamp(min, max),
            },
            Kind::Toggle { .. } => self.clamp(n),
            Kind::Choice { choices, .. } => self.clamp(n * (choices.len() - 1) as f64),
        }
    }

    /// The value as every shell prints it: "+3.0 dB", "7.5 bits", "40 %",
    /// "12.0 kHz", "-1.5 oct", "On".
    pub fn write_value(self, out: &mut dyn Write, v: f64) -> fmt::Result {
        let v = if v.is_nan() { self.default_value() } else { self.clamp(v) };
        let unit = match self.info().kind {
            Kind::Float { unit, .. } => unit,
            Kind::Toggle { .. } => return out.write_str(TOGGLE_LABELS[(v >= 0.5) as usize]),
            Kind::Choice { choices, .. } => return out.write_str(choices[v as usize]),
        };
        match unit {
            Unit::Db | Unit::Octaves => {
                /* Signed, so a cut and a boost read apart at a glance; and no
                 * "-0.0", which a value a hair under zero would round to. */
                let r = (v * 10.0).round() / 10.0;
                if r > 0.0 {
                    out.write_char('+')?;
                }
                f(out, if r == 0.0 { 0.0 } else { r }, 1)?;
                write!(out, " {}", unit.label())
            }
            Unit::Bits => {
                f(out, v, 1)?;
                out.write_str(" bits")
            }
            Unit::Percent => {
                f(out, v, 0)?;
                out.write_str(" %")
            }
            Unit::Hz => {
                if v < 1000.0 {
                    f(out, v, 0)?;
                    out.write_str(" Hz")
                } else {
                    f(out, v / 1000.0, if v < 10_000.0 { 2 } else { 1 })?;
                    out.write_str(" kHz")
                }
            }
        }
    }

    /// What a user typed, read as this parameter's value: the number at the
    /// front, as C's `atof` reads it, in the parameter's unit -- with a `k`
    /// after a rate meaning thousands. A toggle also takes its words. `None`
    /// for text with no number in it, which then changes nothing.
    pub fn parse_value(self, text: &str) -> Option<f64> {
        let t = text.trim();
        if let Kind::Toggle { .. } = self.info().kind {
            if let Some(i) = TOGGLE_LABELS.iter().position(|l| l.eq_ignore_ascii_case(t)) {
                return Some(i as f64);
            }
        }
        /* A choice by its word only: "1/s" has a digit in it, and read as a
         * number it would be the index of "10/s". */
        if let Kind::Choice { choices, .. } = self.info().kind {
            return choices.iter().position(|l| l.eq_ignore_ascii_case(t)).map(|i| i as f64);
        }
        if !t.bytes().any(|b| b.is_ascii_digit()) {
            return None;
        }
        let mut v = atof(t);
        if matches!(self.info().kind, Kind::Float { unit: Unit::Hz, .. }) {
            /* The first letter after the number, whatever spacing: "12k",
             * "12 kHz", "12.5K". */
            let unit = t.trim_start_matches(|c: char| c.is_ascii_digit() || matches!(c, '.' | '+' | '-' | ' '));
            if unit.starts_with(['k', 'K']) {
                v *= 1000.0;
            }
        }
        Some(self.clamp(v))
    }
}

#[cfg(test)]
mod tests;
