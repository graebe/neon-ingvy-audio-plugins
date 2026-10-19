// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The words an equaliser's parameters are printed and read in: one filter's
Freq, Gain, Q, Ripple and Stopband, and the levels around them. NI Filter
Designer and the single-filter products speak them alike, so a value reads
the same in each ("+3.0 dB", "-inf dB", "1.20 kHz", "250 Hz", "0.71", "100 %",
"Notch").
*/

use core::fmt::{self, Write};
use ni_dsp::fmt::{atof, f};
use ni_params::Kind;

/* THE CHOICES' LABELS ARE FROZEN. A host keeps a choice as a fraction of its
 * labels, so a label added later would move every automation point saved
 * before it. */

/// A switch's labels.
pub const SWITCH_LABELS: [&str; 2] = ["Off", "On"];
/// An analyzer's: what it shows behind the curve.
pub const ANALYZER_LABELS: [&str; 4] = ["Off", "Pre", "Post", "Both"];
/// The families, as ni_filter::Family::ALL.
pub const FAMILY_LABELS: [&str; 6] = ["Butterworth", "Bessel", "Chebyshev I", "Chebyshev II", "Elliptic", "Linkwitz-Riley"];
/// The orders 1..8, by the roll-off each makes.
pub const ORDER_LABELS: [&str; 8] =
    ["6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "30 dB/oct", "36 dB/oct", "42 dB/oct", "48 dB/oct"];
/// Which of a stereo pair a filter filters.
pub const CHANNEL_LABELS: [&str; 5] = ["Stereo", "Left", "Right", "Mid", "Side"];

/// A number's unit: what it is printed with and how typing is read.
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

/// `v` of a parameter of `kind` in `unit` (`None` for a choice), as every
/// shell prints it. NaN prints the default; a value outside the range, the
/// end it is clamped to.
pub fn write(kind: Kind, unit: Option<Unit>, out: &mut dyn Write, v: f64) -> fmt::Result {
    let v = if v.is_nan() { kind.default_value() } else { kind.clamp(v) };
    if let Some(label) = kind.choice_text(v) {
        return out.write_str(label);
    }
    let signed = matches!(kind, Kind::Float { bipolar: true, .. });
    match unit.unwrap_or(Unit::Q) {
        Unit::Db | Unit::DbToSilence => {
            if unit == Some(Unit::DbToSilence) && v <= -60.0 {
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

/// What a user typed, read as a value of `kind` in `unit`: a choice's label,
/// or the number at the front as C reads it, in the unit -- a frequency with
/// k after it in kHz ("1.2k", "1.2 kHz"), a level that says inf at its
/// bottom. Clamped to the range; `None` for text with no value in it.
pub fn parse(kind: Kind, unit: Option<Unit>, text: &str) -> Option<f64> {
    let t = text.trim();
    if let Some(i) = kind.choice_of(t) {
        return Some(i);
    }
    /* "inf" in any case, found in place: the Move sends a knob's text on its
     * audio thread, so reading it allocates nothing. */
    if unit == Some(Unit::DbToSilence) && t.as_bytes().windows(3).any(|w| w.eq_ignore_ascii_case(b"inf")) {
        return Some(kind.clamp(-60.0));
    }
    if !t.bytes().any(|b| b.is_ascii_digit()) {
        return None;
    }
    let mut v = atof(t);
    if unit == Some(Unit::Hz) && t.bytes().any(|b| b == b'k' || b == b'K') {
        v *= 1000.0;
    }
    Some(kind.clamp(v))
}
