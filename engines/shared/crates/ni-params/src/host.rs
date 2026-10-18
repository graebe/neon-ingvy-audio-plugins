// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
A parameter as a host saw it in an iPlug2 build: the host column of the
products that took one over (Trance Gate, Side-Chain).

A project stores PLAIN host values by index, and a host shows the names, so
those products keep what the iPlug2 build declared -- its IDs, names, kinds
and units, and its numbers, which are not always the engine's: a level the
engine holds as 0..1 was a percentage at the host, a slot counted from 1.
[`Host`] is that column, `host = engine * scale + offset`, beside the engine's
own range in each product's schema.

The C side is one row per parameter ([`NiHostParam`]), written by a product's
`*_param_spec` entry point, and the words of a choice or a toggle one at a
time (`*_param_option`): plugins/_shared/juce/HostTable.h reads both into the
`ni::ParamSpec` table the shell and its state codec share.
*/

use core::ffi::CStr;
use std::os::raw::{c_char, c_int};

/// How a host stores and steps a value: iPlug2's four kinds, which the
/// shell's `ni::ParamSpec::Kind` mirrors. The discriminants are the C ABI's.
#[repr(i32)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum HostKind {
    /// A whole number from min to max.
    Integer = 0,
    /// An index into the options.
    Choice = 1,
    /// 0 or 1, the options its two words.
    Toggle = 2,
    /// Any value from min to max, kept as it comes.
    Continuous = 3,
}

/// One parameter as a host sees it. Every number is the HOST's plain value:
/// `host = engine * scale + offset`.
#[derive(Clone, Copy, Debug)]
pub struct Host {
    /// The JUCE parameter ID. With legacy IDs the index is the VST3 ID, so
    /// this only names it.
    pub id: &'static CStr,
    /// What a host lists.
    pub name: &'static CStr,
    pub kind: HostKind,
    /// The VST3 units string.
    pub units: &'static CStr,
    pub scale: f64,
    pub offset: f64,
    /// A choice's or a toggle's words; empty where the product supplies them
    /// at run time (a rate's label, a note's name).
    pub options: &'static [&'static CStr],
}

impl Host {
    /// The host's plain value for an engine value, and back.
    #[inline]
    pub fn to_host(&self, engine: f64) -> f64 {
        engine * self.scale + self.offset
    }
    #[inline]
    pub fn from_host(&self, host: f64) -> f64 {
        (host - self.offset) / self.scale
    }
}

/// A host parameter of `kind`, the same number as the engine's.
pub const fn host(id: &'static CStr, name: &'static CStr, kind: HostKind) -> Host {
    Host { id, name, kind, units: c"", scale: 1.0, offset: 0.0, options: &[] }
}

/// A level the engine holds as 0..1, as a percentage at the host.
pub const fn percent(id: &'static CStr, name: &'static CStr) -> Host {
    Host { scale: 100.0, ..host(id, name, HostKind::Continuous) }
}

/// `h` with its words.
pub const fn words(h: Host, options: &'static [&'static CStr]) -> Host {
    Host { options, ..h }
}

/// A C string a table holds, as Rust text. Every one is ASCII.
pub fn text(c: &CStr) -> &str {
    c.to_str().unwrap_or("")
}

/// The index of the word `val` is among `options`, exactly as spelled.
pub fn option_of(options: &[&CStr], val: &str) -> Option<usize> {
    options.iter().position(|o| o.to_bytes() == val.as_bytes())
}

/* ------------------------------------------------------------- the C side */

/// One host parameter, as a product's schema declares it: the row a plugin
/// builds its host parameter from. Every number is the HOST's plain value,
/// `host = engine * scale + offset`. `kind` is a `HostKind`: 0 integer,
/// 1 choice, 2 toggle, 3 continuous. A choice or a toggle has `num_texts`
/// words (`*_param_option`), or none where the product supplies them at run
/// time. Every pointer is the engine's and lives as long as the program.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct NiHostParam {
    pub id: *const c_char,
    pub name: *const c_char,
    pub kind: c_int,
    pub min: f64,
    pub max: f64,
    pub def: f64,
    pub units: *const c_char,
    pub num_texts: c_int,
    pub scale: f64,
    pub offset: f64,
}

/// `*_param_spec`'s body: `host` and the engine's range and default (`None`
/// for an index out of range) into `out`, in host values. False when there is
/// no parameter or nowhere to write.
///
/// # Safety
/// `out` is null or writable.
pub unsafe fn c_host_param(row: Option<(&Host, f64, f64, f64)>, out: *mut NiHostParam) -> bool {
    let (Some((h, min, max, default)), Some(out)) = (row, out.as_mut()) else {
        return false;
    };
    *out = NiHostParam {
        id: h.id.as_ptr(),
        name: h.name.as_ptr(),
        kind: h.kind as c_int,
        min: h.to_host(min),
        max: h.to_host(max),
        def: h.to_host(default),
        units: h.units.as_ptr(),
        num_texts: h.options.len() as c_int,
        scale: h.scale,
        offset: h.offset,
    };
    true
}

/// `*_param_option`'s body: word `option` of `host` (`None` for an index out
/// of range), or null when it has no such word.
pub fn c_host_option(host: Option<&Host>, option: c_int) -> *const c_char {
    let word = host.and_then(|h| usize::try_from(option).ok().and_then(|o| h.options.get(o)));
    word.map_or(core::ptr::null(), |w| w.as_ptr())
}

#[cfg(test)]
mod tests {
    use super::*;

    const OFF_ON: &[&CStr] = &[c"Off", c"On"];
    const SLOT: Host = Host { offset: 1.0, ..host(c"slot", c"Slot", HostKind::Integer) };
    const DEPTH: Host = percent(c"depth", c"Depth");
    const HOLD: Host = words(host(c"hold", c"Hold", HostKind::Toggle), OFF_ON);

    #[test]
    fn a_host_value_is_the_engines_scaled_and_offset() {
        assert_eq!(SLOT.to_host(0.0), 1.0);
        assert_eq!(SLOT.from_host(8.0), 7.0);
        assert_eq!(DEPTH.to_host(0.35), 35.0);
        assert_eq!(DEPTH.from_host(35.0), 0.35);
        assert_eq!(text(DEPTH.name), "Depth");
    }

    #[test]
    fn a_word_is_found_as_spelled() {
        assert_eq!(option_of(OFF_ON, "On"), Some(1));
        assert_eq!(option_of(OFF_ON, "on"), None);
        assert_eq!(option_of(&[], "On"), None);
    }

    #[test]
    fn the_row_is_written_in_host_values() {
        unsafe {
            let mut row: NiHostParam = core::mem::zeroed();
            assert!(c_host_param(Some((&DEPTH, 0.0, 1.0, 1.0)), &mut row));
            assert_eq!((row.kind, row.min, row.max, row.def, row.scale), (3, 0.0, 100.0, 100.0, 100.0));
            assert_eq!(CStr::from_ptr(row.id), c"depth");
            assert_eq!(CStr::from_ptr(row.units), c"");
            assert!(c_host_param(Some((&HOLD, 0.0, 1.0, 0.0)), &mut row));
            assert_eq!((row.kind, row.num_texts), (2, 2));
            assert!(!c_host_param(None, &mut row));
            assert!(!c_host_param(Some((&SLOT, 0.0, 7.0, 0.0)), core::ptr::null_mut()));
        }
    }

    #[test]
    fn the_words_are_read_one_at_a_time() {
        assert_eq!(unsafe { CStr::from_ptr(c_host_option(Some(&HOLD), 1)) }, c"On");
        assert!(c_host_option(Some(&HOLD), 2).is_null());
        assert!(c_host_option(Some(&HOLD), -1).is_null());
        assert!(c_host_option(Some(&DEPTH), 0).is_null());
        assert!(c_host_option(None, 0).is_null());
    }
}
