// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The shape of a parameter table, and the arithmetic every shell reads one
with: what each product's engine used to declare, and each product's C ABI
used to copy, in a file of its own.

WHAT A PRODUCT KEEPS. Its parameters' keys, names, ranges and units, and the
words a value is printed and read in -- which differ from product to product
("12.0 kHz", "E>I 30 %"). WHAT LIVES HERE: what a parameter IS ([`Kind`]: a
number on a travel, or a choice of labels), the clamping, snapping and
normalising every shell applies to one, and the C struct a shell declares a
host parameter from ([`NiParamInfo`]), with the helpers a product's
`*_param_*` entry points are one line each of.

A product's C ABI crate names this file as a further source of the header that
declares its table (its build.rs), so `NiParamInfo` is in that header, and the
shells' one C++ adapter (plugins/_shared/juce/EngineParameter.h) reads it.

It knows no product, and allocates nothing: a table is read on any thread.

[`host`] is the other column two products keep: a parameter as an iPlug2
build declared it to hosts, which their saved sets still hold.
*/

pub mod chain;
pub mod host;

use core::fmt::{self, Write};
use std::os::raw::{c_char, c_int};

use ni_dsp::ffi::copy_cstr;

/// How a number's knob travels from `min` to `max`.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Travel {
    Linear,
    /// Equal ratios in equal turns. Only for a range above zero.
    Log,
}

/// What a parameter is.
#[derive(Clone, Copy, PartialEq, Debug)]
pub enum Kind {
    /// A number from `min` to `max`, kept to `resolution`, on `travel`;
    /// `bipolar` when it runs both ways about zero (a symmetric range, so the
    /// centre of the travel IS zero). Always automatable.
    Float {
        min: f64,
        max: f64,
        default: f64,
        resolution: f64,
        travel: Travel,
        bipolar: bool,
    },
    /// One of `labels`, by index. Two labels make a switch.
    Choice {
        labels: &'static [&'static str],
        default: usize,
        automatable: bool,
    },
}

impl Kind {
    /// The value a new instance starts at.
    pub const fn default_value(self) -> f64 {
        match self {
            Kind::Float { default, .. } => default,
            Kind::Choice { default, .. } => default as f64,
        }
    }

    /// The nearest value it can hold: a number inside its range, a choice's
    /// nearest index. NaN is not a value; callers drop it first.
    pub fn clamp(self, v: f64) -> f64 {
        match self {
            Kind::Float { min, max, .. } => v.clamp(min, max),
            Kind::Choice { labels, .. } => v.round().clamp(0.0, (labels.len() - 1) as f64),
        }
    }

    /// The nearest value on the resolution, inside the range: what a host that
    /// keeps values as floats should keep, so a value on it comes back
    /// exactly. A choice's index.
    pub fn snap(self, v: f64) -> f64 {
        match self {
            Kind::Float { resolution, .. } => {
                let per = 1.0 / resolution;
                self.clamp((v * per).round() / per)
            }
            Kind::Choice { .. } => self.clamp(v),
        }
    }

    /// The value as a fraction of the knob's travel, 0..1.
    pub fn normalise(self, v: f64) -> f64 {
        match self {
            Kind::Float { min, max, travel, .. } => {
                let v = v.clamp(min, max);
                match travel {
                    Travel::Linear => (v - min) / (max - min),
                    Travel::Log => (v / min).ln() / (max / min).ln(),
                }
            }
            Kind::Choice { labels, .. } => self.clamp(v) / (labels.len() - 1) as f64,
        }
    }

    /// The value at a fraction 0..1 of the travel: the inverse of
    /// `normalise`; out-of-range and NaN fractions land on the ends.
    pub fn denormalise(self, n: f64) -> f64 {
        let n = if n.is_nan() { 0.0 } else { n.clamp(0.0, 1.0) };
        match self {
            Kind::Float { min, max, travel, .. } => match travel {
                Travel::Linear => (min + n * (max - min)).clamp(min, max),
                Travel::Log => (min * (max / min).powf(n)).clamp(min, max),
            },
            Kind::Choice { labels, .. } => self.clamp(n * (labels.len() - 1) as f64),
        }
    }

    /// A choice's label number `option`; `None` for a number or an option out
    /// of range.
    pub fn label(self, option: i32) -> Option<&'static str> {
        match self {
            Kind::Choice { labels, .. } => usize::try_from(option).ok().and_then(|o| labels.get(o)).copied(),
            Kind::Float { .. } => None,
        }
    }

    /// The label a choice's value prints as; `None` for a number.
    pub fn choice_text(self, v: f64) -> Option<&'static str> {
        match self {
            Kind::Choice { labels, .. } => Some(labels[self.clamp(v) as usize]),
            Kind::Float { .. } => None,
        }
    }

    /// The index of the label `text` names, ignoring case; `None` for a
    /// number or text that names none.
    pub fn choice_of(self, text: &str) -> Option<f64> {
        match self {
            Kind::Choice { labels, .. } => labels.iter().position(|l| l.eq_ignore_ascii_case(text.trim())).map(|i| i as f64),
            Kind::Float { .. } => None,
        }
    }
}

/* ------------------------------------------------------------- the C side */

/// What a parameter is, in C.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum NiParamKind {
    /// A number from `min` to `max` in its unit.
    Float = 0,
    /// One of `max + 1` labels, by index from 0.
    Choice = 1,
}

/// What a shell declares one host parameter with. A choice's range is its
/// indices and its resolution 1.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct NiParamInfo {
    pub kind: NiParamKind,
    pub min: f64,
    pub max: f64,
    pub default_value: f64,
    /// True when the knob travels in equal ratios.
    pub log_travel: bool,
    /// True when the value runs both ways about zero.
    pub bipolar: bool,
    /// The finest step a host should keep.
    pub resolution: f64,
    /// False for a parameter a host may not automate.
    pub automatable: bool,
}

impl From<Kind> for NiParamInfo {
    fn from(kind: Kind) -> NiParamInfo {
        match kind {
            Kind::Float { min, max, default, resolution, travel, bipolar } => NiParamInfo {
                kind: NiParamKind::Float,
                min,
                max,
                default_value: default,
                log_travel: travel == Travel::Log,
                bipolar,
                resolution,
                automatable: true,
            },
            Kind::Choice { labels, default, automatable } => NiParamInfo {
                kind: NiParamKind::Choice,
                min: 0.0,
                max: (labels.len() - 1) as f64,
                default_value: default as f64,
                log_travel: false,
                bipolar: false,
                resolution: 1.0,
                automatable,
            },
        }
    }
}

/// `*_param_info`'s body: `kind` (a product's lookup, `None` for an index out
/// of range) into `out`. False when there is no parameter or nowhere to write.
///
/// # Safety
/// `out` is null or writable.
pub unsafe fn c_info(kind: Option<Kind>, out: *mut NiParamInfo) -> bool {
    match (kind, out.as_mut()) {
        (Some(k), Some(out)) => {
            *out = k.into();
            true
        }
        _ => false,
    }
}

/// `*_param_key`, `_name` and `_unit`'s body: `text` (`None` for an index out
/// of range) into `out` with `cap` bytes; its length, or -1 when there is no
/// text or no room for all of it.
///
/// # Safety
/// `out` is null or has room for `cap` bytes.
pub unsafe fn c_text(text: Option<&str>, out: *mut c_char, cap: c_int) -> c_int {
    match text {
        Some(t) => copy_cstr(t, out, cap),
        None => -1,
    }
}

/// `*_param_text`'s body: what `write` prints, into `out`, as [`c_text`].
///
/// # Safety
/// As [`c_text`].
pub unsafe fn c_written(write: Option<impl FnOnce(&mut dyn Write) -> fmt::Result>, out: *mut c_char, cap: c_int) -> c_int {
    let Some(write) = write else { return -1 };
    let mut text = ShortText::default();
    if write(&mut text).is_err() {
        return -1;
    }
    copy_cstr(text.as_str(), out, cap)
}

/// A short text on the stack, for a value before it is copied out.
#[derive(Default)]
pub struct ShortText {
    buf: [u8; 32],
    len: usize,
}

impl ShortText {
    pub fn as_str(&self) -> &str {
        core::str::from_utf8(&self.buf[..self.len]).unwrap_or("")
    }
}

impl Write for ShortText {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        let end = self.len + s.len();
        let dst = self.buf.get_mut(self.len..end).ok_or(fmt::Error)?;
        dst.copy_from_slice(s.as_bytes());
        self.len = end;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::CStr;

    const LEVEL: Kind = Kind::Float { min: -24.0, max: 24.0, default: 0.0, resolution: 0.01, travel: Travel::Linear, bipolar: true };
    const RATE: Kind = Kind::Float { min: 50.0, max: 96_000.0, default: 12_000.0, resolution: 0.1, travel: Travel::Log, bipolar: false };
    const MODE: Kind = Kind::Choice { labels: &["Time", "Spectral"], default: 0, automatable: false };

    #[test]
    fn numbers_clamp_snap_and_travel() {
        assert_eq!(LEVEL.clamp(30.0), 24.0);
        assert_eq!(LEVEL.snap(3.004), 3.0);
        assert_eq!(LEVEL.normalise(0.0), 0.5);
        assert_eq!(LEVEL.denormalise(f64::NAN), -24.0);
        assert_eq!(LEVEL.default_value(), 0.0);
        let mid = (50.0f64 * 96_000.0).sqrt();
        assert!((RATE.normalise(mid) - 0.5).abs() < 1e-12);
        assert!((RATE.denormalise(0.5) - mid).abs() < 1e-6);
        assert_eq!(RATE.snap(7_000.04), 7_000.0);
        assert_eq!(LEVEL.label(0), None);
        assert_eq!(LEVEL.choice_text(1.0), None);
        assert_eq!(LEVEL.choice_of("Time"), None);
    }

    #[test]
    fn choices_are_their_nearest_index_and_their_labels() {
        assert_eq!(MODE.clamp(0.49), 0.0);
        assert_eq!(MODE.clamp(7.0), 1.0);
        assert_eq!(MODE.snap(0.7), 1.0);
        assert_eq!(MODE.normalise(1.0), 1.0);
        assert_eq!(MODE.denormalise(0.8), 1.0);
        assert_eq!(MODE.label(1), Some("Spectral"));
        assert_eq!(MODE.label(2), None);
        assert_eq!(MODE.label(-1), None);
        assert_eq!(MODE.choice_text(0.2), Some("Time"));
        assert_eq!(MODE.choice_of(" spectral "), Some(1.0));
        assert_eq!(MODE.choice_of("fast"), None);
        assert_eq!(MODE.default_value(), 0.0);
    }

    #[test]
    fn the_c_struct_says_what_the_kind_says() {
        let i = NiParamInfo::from(RATE);
        assert_eq!((i.kind, i.log_travel, i.automatable, i.resolution), (NiParamKind::Float, true, true, 0.1));
        let m = NiParamInfo::from(MODE);
        assert_eq!((m.kind, m.min, m.max, m.default_value, m.automatable), (NiParamKind::Choice, 0.0, 1.0, 0.0, false));
        assert!(NiParamInfo::from(LEVEL).bipolar);
        let mut out = m;
        assert!(unsafe { c_info(Some(LEVEL), &mut out) });
        assert_eq!(out, NiParamInfo::from(LEVEL));
        assert!(unsafe { !c_info(None, &mut out) });
        assert!(unsafe { !c_info(Some(LEVEL), std::ptr::null_mut()) });
    }

    #[test]
    fn text_crosses_whole_or_not_at_all() {
        let mut buf = [0 as c_char; 16];
        assert_eq!(unsafe { c_text(Some("Spectral"), buf.as_mut_ptr(), 16) }, 8);
        assert_eq!(unsafe { CStr::from_ptr(buf.as_ptr()) }.to_str().unwrap(), "Spectral");
        assert_eq!(unsafe { c_text(None, buf.as_mut_ptr(), 16) }, -1);
        assert_eq!(unsafe { c_text(Some("Spectral"), buf.as_mut_ptr(), 4) }, -1);
        let n = unsafe { c_written(Some(|w: &mut dyn Write| w.write_str("+3.0 dB")), buf.as_mut_ptr(), 16) };
        assert_eq!(n, 7);
        let none: Option<fn(&mut dyn Write) -> fmt::Result> = None;
        assert_eq!(unsafe { c_written(none, buf.as_mut_ptr(), 16) }, -1);
        let long = unsafe { c_written(Some(|w: &mut dyn Write| w.write_str(&"x".repeat(40))), buf.as_mut_ptr(), 16) };
        assert_eq!(long, -1);
        let mut t = ShortText::default();
        assert!(t.write_str(&"y".repeat(33)).is_err());
        assert_eq!(t.as_str(), "");
    }
}
