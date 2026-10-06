// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
Formatting and parsing that match C's, because the wire is C's.

Every value an engine reports crosses a `char*` boundary, and the state blob
is compared byte-for-byte by the test suite and round-tripped through saved
patches. So two things have to hold:

- **Nothing here allocates.** `get_param` runs on the audio callback, so
  `format!` is not available: output goes into the caller's buffer through
  [`Buf`], which truncates the way `snprintf` does rather than growing.
- **Parsing is C's, not Rust's.** `atof("abc")` is 0.0 and `atoi("12ms")` is
  12; `str::parse` is an error in both cases. A patch that fails to parse a
  field must fall back the way the C did, or a blob the old build read
  silently changes meaning.

WRITING IS `core::fmt`'s. Rust's float formatting renders the exact binary
value and rounds it to nearest, ties to even, which is what printf does. So
`%.*f` is `{:.*}`, and `%.*g` is put together from `{:.*e}` and `{:.*}` in
the steps the C standard defines it by, in an `ArrayString` on the stack.

READING IS `lexical-core`'s. It reads the longest number at the front of the
text and says where it stopped, which is `strtod`'s contract, and it does so
without allocating, at any length. `C_DECIMAL` below narrows its grammar to
the one this wire has always accepted.

lexical-core writes floats too, and is deliberately not asked to. It rounds
the shortest digits that read back as the value, not the value itself, and
the two part wherever those digits end in a 5 that the exact value does not.
2.675 is really 2.67499999999999982236431605997495353221893310546875: printf
writes "2.67" at two decimals, and a writer that rounds "2.675" writes
"2.68".
*/

use arrayvec::ArrayString;
use core::fmt::{self, Write};
use lexical_core::{NumberFormatBuilder, ParseFloatOptions};

/// A fixed-capacity writer with `snprintf` semantics: writes what fits, drops
/// the rest, and reports the length as if it had all fit -- which is what the
/// C ABI's callers expect back.
///
/// The buffer is the CALLER's -- a C entry point's `char *buf, int buf_len`,
/// sized at run time by the other side. That is why this is not an
/// `ArrayString`: its capacity is a compile-time constant, and the text would
/// need a second copy into the caller's memory.
pub struct Buf<'a> {
    out: &'a mut [u8],
    /// Bytes actually written.
    pub len: usize,
    /// Bytes that WOULD have been written. `snprintf` returns this.
    pub wanted: usize,
}

impl<'a> Buf<'a> {
    pub fn new(out: &'a mut [u8]) -> Self {
        Self { out, len: 0, wanted: 0 }
    }

    /// Null-terminate and return the length written, excluding the
    /// terminator -- the value a `*_get_param` C entry point returns.
    pub fn finish(self) -> i32 {
        let n = self.len.min(self.out.len().saturating_sub(1));
        if let Some(end) = self.out.get_mut(n) {
            *end = 0;
        }
        n as i32
    }
}

impl Write for Buf<'_> {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        self.wanted += s.len();
        /* One byte is always held back for the terminator, as snprintf does,
         * and a cut falls between bytes, not characters -- snprintf's again. */
        let room = self.out.len().saturating_sub(1).saturating_sub(self.len);
        let n = s.len().min(room);
        if n > 0 {
            self.out[self.len..self.len + n].copy_from_slice(&s.as_bytes()[..n]);
            self.len += n;
        }
        Ok(())
    }
}

/*
 * C'S `strtod`, IN THE PART OF ITS GRAMMAR THIS WIRE HAS EVER CARRIED: an
 * optional sign, decimal digits with an optional point, an optional exponent.
 * lexical-core's standard format reads exactly that, apart from two places
 * where it and this wire differ, and both are switched here:
 *
 *   - an `e` with no digits after it is no exponent. "1e" is 1 and "5.e-" is
 *     5, to strtod and to every patch this has read; the standard format
 *     calls both an error, which would make them 0.
 *   - no special values. strtod also reads "inf" and "nan", and this wire
 *     never did: a stray "nan" in a patch has always been 0.0, and reading it
 *     as NaN instead would carry it into a parameter.
 *
 * strtod's hex floats are outside the standard format already: "0x10" is the
 * 0 in front of the x, as it always was here.
 */
const C_DECIMAL: u128 = NumberFormatBuilder::new()
    .required_exponent_digits(false)
    .no_special(true)
    .build_strict();

/// lexical-core's defaults: a `.` for the point, as in C's "C" locale.
const FLOAT: ParseFloatOptions = ParseFloatOptions::new();

/// What `atof` and `atoi` skip before the number. C's `isspace` also counts
/// `\v` and `\f`; this wire never has, and a parser is no place to start.
fn after_space(s: &str) -> &[u8] {
    s.trim_start_matches([' ', '\t', '\n', '\r']).as_bytes()
}

/// `strtod`/`atof`: leading space, optional sign, digits with an optional
/// fraction and exponent, and **0.0 rather than an error** for anything it
/// cannot read. Trailing junk is ignored, so "0.5abc" is 0.5 and "abc" is 0.
pub fn atof(s: &str) -> f64 {
    lexical_core::parse_partial_with_options::<f64, C_DECIMAL>(after_space(s), &FLOAT)
        .map_or(0.0, |(v, _)| v)
}

/// `atoi`: the same leniency, truncating rather than rounding -- "12.9" is
/// 12. A number too large for an `i64` is 0: C's `atoi` leaves that case
/// undefined, and 0 is what this has always answered.
pub fn atoi(s: &str) -> i64 {
    lexical_core::parse_partial::<i64>(after_space(s)).map_or(0, |(v, _)| v)
}

/// `%.*f`. Rust's float formatting and printf both render the exact binary
/// value and round half to even, so this is a thin wrapper -- but it is
/// spelled out here so there is one place to change if that ever stops being
/// true, and so the call sites read as the printf they replaced.
///
/// For finite values only: a NaN comes out as Rust's "NaN", not C's "nan". No
/// caller hands it one -- every value it formats is clamped first.
#[inline]
pub fn f(out: &mut dyn Write, v: f64, decimals: usize) -> fmt::Result {
    write!(out, "{v:.decimals$}")
}

/// The most significant digits [`g`] writes. Seventeen decide an f64; C's
/// `%.40g` goes on printing its binary expansion, and so does this, up to
/// here. A larger `sig` writes forty.
const MAX_SIG: usize = 40;

/// Room for either form of `%g` at [`MAX_SIG`] digits: a sign, the digits, a
/// point, and either `%f` style's up to four leading zeros or `%e` style's
/// `e-308`.
type Scratch = ArrayString<{ MAX_SIG + 8 }>;

/// `%.*g`, which Rust has no equivalent of and which the `params` readout
/// needs at nine significant digits -- `FLT_DECIMAL_DIG`, the shortest
/// precision for which `f32 -> decimal -> f32` is the identity.
///
/// `{}` would also round-trip, but it is not the same string: Rust prints the
/// shortest form that recovers the **f64**, so an f32 widened to double comes
/// out as `0.10000000149011612` where C writes `0.100000001`. The wire is
/// C's, so the format is C's.
///
/// The rules, from the C standard: with precision `P` and the value's decimal
/// exponent `X` *after rounding to P significant digits*, use `%f` style with
/// `P-1-X` decimals when `-4 <= X < P` and `%e` style otherwise, then strip
/// trailing zeros from the fraction and the point if nothing survives it.
pub fn g(out: &mut dyn Write, v: f64, sig: usize) -> fmt::Result {
    if v.is_nan() {
        return out.write_str("nan");
    }
    if v.is_infinite() {
        return out.write_str(if v < 0.0 { "-inf" } else { "inf" });
    }
    let p = sig.clamp(1, MAX_SIG);

    /*
     * THE EXPONENT IS READ AFTER ROUNDING, NOT BEFORE.
     *
     * 9.9999999996 at nine significant digits is 1.00000000e1, so its X is 1
     * and not the 0 that a log10 of the input would report -- and X picks the
     * style, so getting it early gets the whole number wrong. Rendering the
     * scientific form first and reading the exponent back off it makes that
     * ordering structural instead of something to remember.
     */
    let mut sci = Scratch::new();
    write!(sci, "{:.*e}", p - 1, v)?;
    let (mantissa, exp) = sci.split_once('e').ok_or(fmt::Error)?;
    let x: i32 = exp.parse().map_err(|_| fmt::Error)?;

    if -4 <= x && x < p as i32 {
        /* The standard's own wording: style f, precision P - (X + 1). It
         * rounds to the same P digits the scientific form did, because X was
         * read after that rounding. */
        let mut fixed = Scratch::new();
        write!(fixed, "{:.*}", (p as i32 - 1 - x) as usize, v)?;
        out.write_str(strip_zeros(&fixed))
    } else {
        /* Rust writes `1.5e3` and `1.5e-3`: no `+`, no zero padding. C wants
         * `1.5e+03`, so the exponent is written here rather than copied. */
        out.write_str(strip_zeros(mantissa))?;
        write!(out, "e{}{:02}", if x < 0 { '-' } else { '+' }, x.unsigned_abs())
    }
}

/// `%g`'s last step: the fraction's trailing zeros go, and the point too if
/// nothing is left after it. A number without a point keeps its zeros --
/// they are the integer's.
fn strip_zeros(s: &str) -> &str {
    if s.contains('.') {
        s.trim_end_matches('0').trim_end_matches('.')
    } else {
        s
    }
}

#[cfg(test)]
mod tests;
