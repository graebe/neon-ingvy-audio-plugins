// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The parameters: one table every shell declares from.

ONE TABLE, THREE READERS. The JUCE shell declares its host parameters from it
(vz_param_* in vz-capi), the Move module's `chain_params` is tested against it
(vz-move), and the engine clamps with it. A range, a default or a unit is
written here and nowhere else.

TWO KINDS. A `Float` runs from `min` to `max` in its own unit -- %, dB, or
the vowel path's own; a `Choice` is one of its labels, by index, and says
whether a host may automate it. Mode may not: switching engines changes the
plugin's latency, which a host takes note of between blocks, not inside one.
Ceiling may: it glides.

THE VOWEL IS A PLACE ON A PATH. 0 is A, 1 E, 2 I, 3 O, 4 U, and every value
between is on the way from one to the next: 1.3 is "E>I 30 %". Typing a letter
goes to it; typing a number goes to that place.

THE TEXT IS THE ENGINE'S. `write_value` prints a value the way every shell
shows it, and `parse_value` reads what a user types, locale-free
(ni_dsp::fmt).

APPEND ONLY. The index is what a host stores in a session, so inserting a
parameter would re-point every set saved before it.
*/

use core::fmt::{self, Write};
use ni_dsp::fmt::{atof, f};
pub use ni_params::{Kind, Travel};

/// The parameters, in the order the shell declares them: a host index IS this
/// value.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
#[repr(i32)]
pub enum Param {
    /// Which engine: Time (no latency) or Spectral.
    Mode = 0,
    /// How far the sound has become the vowel: 0 % is the input untouched.
    Vocal,
    /// Where on the path A-E-I-O-U.
    Vowel,
    /// The size of the voice: up is bigger and lower, down smaller and higher.
    Size,
    /// How sharp the formants are: narrow and whistling, or broad and breathy.
    Resonance,
    /// Harmonics added before anything else, for sources too pure to shape.
    Excite,
    /// Dry/wet.
    Mix,
    /// Gain after the mix.
    Output,
    /// Whose voice: the textbook's at 0 %, the one captured from a recording
    /// at 100 % (voice.rs), and the way between.
    Voice,
    /// A soft ceiling after Output: below -0.3 dBFS nothing, above it a curve
    /// that never passes full scale.
    Ceiling,
}

/// How many parameters there are.
pub const PARAM_COUNT: usize = 10;

/// The vowels, in the order the path visits them.
pub const VOWEL_LABELS: [&str; 5] = ["A", "E", "I", "O", "U"];

/// Mode's labels, by index.
pub const MODE_LABELS: [&str; 2] = ["Time", "Spectral"];

/// A switch's labels, by index.
pub const SWITCH_LABELS: [&str; 2] = ["Off", "On"];

/// A float's unit: what it is printed with and how typing is read.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Unit {
    Percent,
    Db,
    /// A place on the vowel path, 0 (A) to 4 (U).
    Vowel,
}

impl Unit {
    /// The finest step a host keeps of a value in this unit: well below what
    /// a knob's drag can tell apart, and coarse enough that a value on it
    /// survives a host's float normalisation and comes back exactly.
    pub const fn resolution(self) -> f64 {
        match self {
            Unit::Percent | Unit::Db => 0.01,
            Unit::Vowel => 0.001,
        }
    }

    /// The unit as a host lists it beside the value; the vowel path has none.
    pub const fn label(self) -> &'static str {
        match self {
            Unit::Percent => "%",
            Unit::Db => "dB",
            Unit::Vowel => "",
        }
    }
}

/// What a shell needs to declare one parameter. What it IS -- a number on a
/// travel or a choice of labels, its range, default and resolution -- is the
/// shared [`Kind`] (ni-params); what is this product's own is the unit a
/// number is printed and read in.
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

/// A number: every one here travels linearly, kept to its unit's resolution;
/// `bipolar` when it runs both ways about zero, so a knob draws its arc from
/// the centre -- which is why every such range is symmetric.
const fn float(min: f64, max: f64, default: f64, unit: Unit, bipolar: bool) -> (Kind, Option<Unit>) {
    (Kind::Float { min, max, default, resolution: unit.resolution(), travel: Travel::Linear, bipolar }, Some(unit))
}

const fn choice(labels: &'static [&'static str], default: usize, automatable: bool) -> (Kind, Option<Unit>) {
    (Kind::Choice { labels, default, automatable }, None)
}

impl Param {
    /// Every parameter, in index order: `ALL[i] as i32 == i`.
    pub const ALL: [Param; PARAM_COUNT] = [
        Param::Mode,
        Param::Vocal,
        Param::Vowel,
        Param::Size,
        Param::Resonance,
        Param::Excite,
        Param::Mix,
        Param::Output,
        Param::Voice,
        Param::Ceiling,
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
        let (key, name, (kind, unit)) = match self {
            Param::Mode => ("mode", "Mode", choice(&MODE_LABELS, 0, false)),
            Param::Vocal => ("vocal", "Vocal", float(0.0, 100.0, 80.0, Unit::Percent, false)),
            Param::Vowel => ("vowel", "Vowel", float(0.0, 4.0, 0.0, Unit::Vowel, false)),
            Param::Size => ("size", "Size", float(-100.0, 100.0, 0.0, Unit::Percent, true)),
            Param::Resonance => ("resonance", "Resonance", float(0.0, 100.0, 50.0, Unit::Percent, false)),
            Param::Excite => ("excite", "Excite", float(0.0, 100.0, 0.0, Unit::Percent, false)),
            Param::Mix => ("mix", "Mix", float(0.0, 100.0, 100.0, Unit::Percent, false)),
            Param::Output => ("output", "Output", float(-24.0, 24.0, 0.0, Unit::Db, true)),
            Param::Voice => ("voice", "Voice", float(0.0, 100.0, 100.0, Unit::Percent, false)),
            Param::Ceiling => ("ceiling", "Ceiling", choice(&SWITCH_LABELS, 1, true)),
        };
        Info { key, name, kind, unit }
    }

    /// The value a new instance starts at, in the parameter's unit.
    pub const fn default_value(self) -> f64 {
        self.info().kind.default_value()
    }

    /// The nearest value the parameter can hold (ni-params). NaN is not a
    /// value; callers drop it first.
    pub fn clamp(self, v: f64) -> f64 {
        self.info().kind.clamp(v)
    }

    /// The nearest value on the unit's resolution, for a host that keeps a
    /// value as a float; a choice's index. The engine itself takes any value:
    /// this is the shells'.
    pub fn snap(self, v: f64) -> f64 {
        self.info().kind.snap(v)
    }

    /// The value as a fraction of the knob's travel, 0..1.
    pub fn normalise(self, v: f64) -> f64 {
        self.info().kind.normalise(v)
    }

    /// The value at a fraction 0..1 of the knob's travel: the inverse of
    /// `normalise`.
    pub fn denormalise(self, n: f64) -> f64 {
        self.info().kind.denormalise(n)
    }

    /// The value as every shell prints it: "80 %", "+40 %", "-6.0 dB", "E",
    /// "E>I 30 %", "Spectral".
    pub fn write_value(self, out: &mut dyn Write, v: f64) -> fmt::Result {
        let v = if v.is_nan() { self.default_value() } else { self.clamp(v) };
        let info = self.info();
        if let Some(label) = info.kind.choice_text(v) {
            return out.write_str(label);
        }
        let bipolar = matches!(info.kind, Kind::Float { bipolar: true, .. });
        let unit = info.unit.unwrap_or(Unit::Percent);
        match unit {
            Unit::Percent => {
                let r = v.round();
                if bipolar && r > 0.0 {
                    out.write_char('+')?;
                }
                f(out, if r == 0.0 { 0.0 } else { r }, 0)?;
                out.write_str(" %")
            }
            Unit::Db => {
                /* Signed, so a cut and a boost read apart at a glance; and no
                 * "-0.0", which a value a hair under zero would round to. */
                let r = (v * 10.0).round() / 10.0;
                if r > 0.0 {
                    out.write_char('+')?;
                }
                f(out, if r == 0.0 { 0.0 } else { r }, 1)?;
                out.write_str(" dB")
            }
            Unit::Vowel => {
                /* On a vowel when the way to the next rounds to 0 % or 100 %. */
                let pct = (v.fract() * 100.0).round();
                let at = v.trunc() as usize + (pct >= 100.0) as usize;
                if pct <= 0.0 || pct >= 100.0 {
                    return out.write_str(VOWEL_LABELS[at]);
                }
                write!(out, "{}>{} ", VOWEL_LABELS[at], VOWEL_LABELS[at + 1])?;
                f(out, pct, 0)?;
                out.write_str(" %")
            }
        }
    }

    /// What a user typed, read as this parameter's value: the number at the
    /// front, as C's `atof` reads it, in the parameter's unit. A choice also
    /// takes its labels; the vowel takes a letter, and a percentage after it
    /// goes that far on to the next ("E 30", and "E>I 30 %" as it is
    /// printed). `None` for text with no value in it, which changes nothing.
    pub fn parse_value(self, text: &str) -> Option<f64> {
        let t = text.trim();
        if let Some(i) = self.info().kind.choice_of(t) {
            return Some(i);
        }
        if self == Param::Vowel {
            if let Some(i) = VOWEL_LABELS.iter().position(|l| t.get(..1).is_some_and(|c| c.eq_ignore_ascii_case(l))) {
                let on = t.trim_start_matches(|c: char| !c.is_ascii_digit() && c != '.');
                let pct = if on.is_empty() { 0.0 } else { atof(on) };
                return Some(self.clamp(i as f64 + pct / 100.0));
            }
        }
        if !t.bytes().any(|b| b.is_ascii_digit()) {
            return None;
        }
        Some(self.clamp(atof(t)))
    }
}

#[cfg(test)]
mod tests;
