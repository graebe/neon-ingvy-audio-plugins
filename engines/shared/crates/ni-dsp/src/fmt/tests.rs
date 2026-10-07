// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! The text on the wire, byte for byte: `snprintf`'s truncation, `strtod`'s
//! leniency, `%.*f` and `%.*g`, pinned against what C itself writes and reads.

/* The float literals here are written as C printed them, or as a person types
 * them, and their rounding to the nearest float is what is under test. */
#![allow(clippy::excessive_precision)]

use super::*;

/// What `g` writes, as text.
fn g_of(v: f64, sig: usize) -> ArrayString<64> {
    let mut s = ArrayString::new();
    g(&mut s, v, sig).unwrap();
    s
}

/// What `f` writes, as text. A String, because `%f` of 1e300 is 301 digits.
/// Only the tests held to C's own printf use it, and they run where there is
/// one to call: on unix.
#[cfg(unix)]
fn f_of(v: f64, decimals: usize) -> String {
    let mut s = String::new();
    f(&mut s, v, decimals).unwrap();
    s
}

/* ------------------------------------------------------------------ Buf */

#[test]
fn buf_writes_what_fits_and_counts_what_did_not() {
    let mut out = [0x7fu8; 6];
    let mut b = Buf::new(&mut out);
    let who = "world";
    write!(b, "hello").unwrap();
    write!(b, ", {who}").unwrap();
    assert_eq!((b.len, b.wanted), (5, 12));
    assert_eq!(b.finish(), 5);
    assert_eq!(&out, b"hello\0");
}

#[test]
fn buf_holds_back_the_terminator_and_cuts_between_bytes() {
    /* Exactly full: four bytes and the terminator. */
    let mut out = [0x7fu8; 5];
    let mut b = Buf::new(&mut out);
    b.write_str("1/16").unwrap();
    assert_eq!(b.finish(), 4);
    assert_eq!(&out, b"1/16\0");

    /* One byte is room for the terminator and nothing else. */
    let mut one = [0x7fu8; 1];
    let mut b = Buf::new(&mut one);
    b.write_str("x").unwrap();
    assert_eq!((b.len, b.wanted), (0, 1));
    assert_eq!(b.finish(), 0);
    assert_eq!(one, [0]);

    /* No room at all, as snprintf(NULL, 0, ...): nothing written, not even a
     * terminator, and the length still counted. */
    let mut b = Buf::new(&mut []);
    b.write_str("abc").unwrap();
    assert_eq!(b.wanted, 3);
    assert_eq!(b.finish(), 0);

    /* snprintf counts bytes, so a cut can split a character; so can this. */
    let mut out = [0x7fu8; 3];
    let mut b = Buf::new(&mut out);
    b.write_str("aé").unwrap();
    assert_eq!(b.finish(), 2);
    assert_eq!(out, [b'a', 0xC3, 0]);
}

/* ---------------------------------------------------------- atof, atoi */

#[test]
fn atof_reads_the_number_in_front_and_nothing_else() {
    let cases: &[(&str, f64)] = &[
        ("0.5abc", 0.5),
        ("abc", 0.0),
        ("", 0.0),
        ("  12ms", 12.0),
        ("\t-3.25e-2x", -0.0325),
        ("1E5", 1e5),
        (".5", 0.5),
        ("-.5", -0.5),
        ("5.", 5.0),
        ("1.2.3", 1.2),
        /* An exponent counts only if it is complete. */
        ("1e", 1.0),
        ("1e+", 1.0),
        ("5.e-", 5.0),
        ("1e 5", 1.0),
        /* No mantissa, no number. */
        (".", 0.0),
        ("-", 0.0),
        ("+-1", 0.0),
        (".e5", 0.0),
        /* Out of range is what strtod says it is. */
        ("1e400", f64::INFINITY),
        ("-1e400", f64::NEG_INFINITY),
        ("1e-400", 0.0),
        ("4.9e-324", 5e-324),
        /* Correctly rounded at any length: 0.1's exact value, spelled out. */
        ("0.1000000000000000055511151231257827021181583404541015625", 0.1),
        /*
         * WHERE THIS IS NOT strtod, ON PURPOSE. C reads special values, hex
         * floats and two more kinds of space; this wire never has, and a
         * patch that once read 0 must go on reading 0.
         */
        ("inf", 0.0),
        ("-INF", 0.0),
        ("infinity", 0.0),
        ("nan", 0.0),
        ("0x10", 0.0),
        ("0x1p3", 0.0),
        ("\x0b5", 0.0),
        ("\x0c5", 0.0),
    ];
    for &(s, want) in cases {
        assert_eq!(atof(s).to_bits(), want.to_bits(), "atof({s:?}) = {}", atof(s));
    }
    /* The sign of a zero survives, as strtod's does. */
    assert_eq!(atof("-0").to_bits(), (-0.0f64).to_bits());
}

#[test]
fn atoi_reads_the_integer_in_front_and_nothing_else() {
    let cases: &[(&str, i64)] = &[
        ("12ms", 12),
        ("  -7", -7),
        ("+3", 3),
        ("12.9", 12),
        ("1e5", 1),
        ("abc", 0),
        ("", 0),
        ("-", 0),
        ("--1", 0),
        ("9223372036854775807", i64::MAX),
        ("-9223372036854775808", i64::MIN),
        /* Past the range: 0, where C's atoi has no defined answer. */
        ("9223372036854775808", 0),
        ("-9223372036854775809", 0),
        ("99999999999999999999abc", 0),
    ];
    for &(s, want) in cases {
        assert_eq!(atoi(s), want, "atoi({s:?})");
    }
}

/* ------------------------------------------------------------------- %g */

/*
 * EVERY EXPECTATION HERE WAS PRINTED BY C, not written by hand: a small
 * generator ran `printf("%.9g")` over these values on this machine and
 * its output is pasted below. The point of `g` is to be byte-identical to
 * that function, so the only defensible oracle is that function.
 */
#[test]
fn g_matches_c_printf_at_nine_significant_digits() {
    let cases: &[(f64, &str)] = &[
        (0f64, "0"),
        (-0f64, "-0"),
        (1f64, "1"),
        (-1f64, "-1"),
        (0.5f64, "0.5"),
        (2f64, "2"),
        (10f64, "10"),
        (100f64, "100"),
        (1000f64, "1000"),
        (0.10000000000000001f64, "0.1"),
        (0.25f64, "0.25"),
        (0.33333333333333331f64, "0.333333333"),
        (200f64, "200"),
        (199.99998500000001f64, "199.999985"),
        (123.456789f64, "123.456789"),
        (0.123456789f64, "0.123456789"),
        (0.98765432099999995f64, "0.987654321"),
        (0.0123456789f64, "0.0123456789"),
        (0.333333343f64, "0.333333343"),
        (9.9999999996f64, "10"),
        (99.999999900000006f64, "99.9999999"),
        (9.9999999999000004e-05f64, "0.0001"),
        (0.0001f64, "0.0001"),
        (9.9999000000000003e-05f64, "9.9999e-05"),
        (1.0000000000000001e-05f64, "1e-05"),
        (0.001f64, "0.001"),
        (100000f64, "100000"),
        (100000000f64, "100000000"),
        (1000000000f64, "1e+09"),
        (10000000000f64, "1e+10"),
        (1e-10f64, "1e-10"),
        (123456789f64, "123456789"),
        (1234567890f64, "1.23456789e+09"),
        (999999999f64, "999999999"),
        (999999999.5f64, "1e+09"),
        (1.5000000000000001e+300f64, "1.5e+300"),
        (2.5000000000000171e-310f64, "2.5e-310"),
        (3f64, "3"),
        (0.0001220703125f64, "0.000122070312"),
        (0.10000000149011612f64, "0.100000001"),
        (0.3333333432674408f64, "0.333333343"),
        (199.99998474121094f64, "199.999985"),
        (0.012345679104328156f64, "0.0123456791"),
        (9.9998886718268301e-321f64, "9.99988867e-321"),
        (4.9406564584124654e-324f64, "4.94065646e-324"),
        (1.7976931348623157e+308f64, "1.79769313e+308"),
        // INFINITY / NAN handled separately
    ];
    for &(v, want) in cases {
        assert_eq!(g_of(v, 9).as_str(), want, "%.9g of {:.17e}", v);
    }
}

#[test]
fn g_handles_the_non_finite_cases_the_way_c_does() {
    assert_eq!(g_of(f64::INFINITY, 9).as_str(), "inf");
    assert_eq!(g_of(f64::NEG_INFINITY, 9).as_str(), "-inf");
    assert_eq!(g_of(f64::NAN, 9).as_str(), "nan");
}

#[test]
fn g_keeps_its_precision_inside_one_to_forty() {
    /* %.0g is %.1g, by the standard; past MAX_SIG the digits stop. */
    assert_eq!(g_of(123.0, 0).as_str(), "1e+02");
    assert_eq!(g_of(0.1, MAX_SIG + 10).as_str(), g_of(0.1, MAX_SIG).as_str());
    assert_eq!(g_of(0.1, usize::MAX).as_str(), g_of(0.1, MAX_SIG).as_str());
}

#[test]
fn nine_digits_round_trips_every_f32_in_range() {
    /*
     * The property the `params` readout depends on: nine significant
     * digits is FLT_DECIMAL_DIG, so a float printed and read back is the
     * same float. Swept by BIT PATTERN rather than by a handful of
     * samples, over exactly the range a stage or a unit value can hold --
     * 0 through 200 is bits 0 through 0x43480000, denormals included.
     *
     * A prime stride walks every exponent and every mantissa region
     * without testing all 1.1 billion of them; the endpoints and the
     * awkward values from the round-trip test are checked outright,
     * because a stride is free to step over the one that matters.
     */
    let check = |v: f32| {
        let s = g_of(v as f64, 9);
        let back = atof(&s) as f32;
        assert_eq!(back.to_bits(), v.to_bits(), "{} -> {} -> {}", v, s, back);
    };

    for v in [
        0.0f32, f32::from_bits(1), f32::MIN_POSITIVE, 1.0, 200.0,
        0.123456789, 0.987654321, 123.456789, 0.0123456789,
        0.333333343, 199.999985, 1.0 / 3.0, 0.1,
    ] {
        check(v);
    }

    let hi = 200.0f32.to_bits();
    let mut bits = 0u32;
    let mut checked = 0u64;
    while bits <= hi {
        check(f32::from_bits(bits));
        checked += 1;
        bits += 4093;
    }
    assert!(checked > 100_000, "swept only {checked}");
}

/* ------------------------------------------------------ C as the oracle */

/*
 * THE C LIBRARY ITSELF, ASKED DIRECTLY. The table above was printed by C
 * once and pasted; these put the same question to it over far more inputs
 * than a table can hold. Unix only, because that is where the wire's C
 * lives: the Move's glibc, and the macOS libc the plugins are tested against.
 */
#[cfg(unix)]
mod c {
    use std::ffi::{c_char, c_int, c_longlong, CStr, CString};

    extern "C" {
        fn strtod(s: *const c_char, end: *mut *mut c_char) -> f64;
        fn strtoll(s: *const c_char, end: *mut *mut c_char, base: c_int) -> c_longlong;
        fn snprintf(buf: *mut c_char, n: usize, format: *const c_char, ...) -> c_int;
    }

    pub fn atof(s: &str) -> f64 {
        let s = CString::new(s).unwrap();
        unsafe { strtod(s.as_ptr(), std::ptr::null_mut()) }
    }

    pub fn atoi(s: &str) -> i64 {
        let s = CString::new(s).unwrap();
        unsafe { strtoll(s.as_ptr(), std::ptr::null_mut(), 10) }
    }

    /// printf's text for a format taking a precision and a double, as
    /// `%.*g` and `%.*f` do. 512 bytes hold `%.8f` of the largest double.
    pub fn printf(format: &CStr, precision: usize, v: f64) -> String {
        let mut buf = [0u8; 512];
        let n = unsafe {
            snprintf(buf.as_mut_ptr().cast(), buf.len(), format.as_ptr(), precision as c_int, v)
        };
        String::from_utf8(buf[..n as usize].to_vec()).unwrap()
    }
}

/// Every string made of one choice from each part, in order -- a grammar
/// walked exhaustively rather than sampled. Returns how many there were.
#[cfg(unix)]
fn every(parts: &[&[&str]], mut each: impl FnMut(&str)) -> usize {
    let total: usize = parts.iter().map(|p| p.len()).product();
    let mut s = String::new();
    for i in 0..total {
        s.clear();
        let mut rest = i;
        for p in parts {
            s.push_str(p[rest % p.len()]);
            rest /= p.len();
        }
        each(&s);
    }
    total
}

#[cfg(unix)]
#[test]
fn atof_reads_every_shape_of_number_the_way_strtod_does() {
    let n = every(
        &[
            &["", " ", "\t", "\r\n "],
            &["", "+", "-"],
            &["", "0", "7", "123", "00042", "18446744073709551616"],
            &["", "."],
            &["", "5", "25", "000123", "9999999999999999999"],
            &["", "e", "E", "e+", "e-", "e5", "E-3", "e+308", "e-400", "e999", "e0012"],
            &["", "x", "ms", " 1", ".5", "e1", "_"],
        ],
        |s| assert_eq!(atof(s).to_bits(), c::atof(s).to_bits(), "atof({s:?})"),
    );
    assert!(n > 50_000, "walked only {n}");
}

#[cfg(unix)]
#[test]
fn atoi_reads_every_shape_of_integer_the_way_strtoll_does() {
    /* Seventeen digits and one more from the tail at most: past i64's range
     * strtoll saturates where this answers 0, which the cases above pin. */
    every(
        &[
            &["", " ", "\t\n"],
            &["", "+", "-", "--", "+-"],
            &["", "0", "7", "123", "00042", "92233720368547758"],
            &["", ".5", "x", "ms", "e5", " 1", "-1", "9"],
        ],
        |s| assert_eq!(atoi(s), c::atoi(s), "atoi({s:?})"),
    );
}

#[cfg(unix)]
#[test]
fn g_and_f_write_what_printf_writes() {
    /*
     * Three populations: bit patterns stepped through every exponent and
     * both signs, subnormals included; the f32 range a stage or a unit value
     * holds, widened as the params readout widens it; and decimal-looking
     * values and binary fractions, which is where %f's ties and near-ties
     * live (2.675, 0.015625).
     */
    let precisions = [1, 2, 6, 9, 9, 9, 17, 25, MAX_SIG];
    let check = |i: usize, v: f64| {
        let p = precisions[i % precisions.len()];
        assert_eq!(g_of(v, p).as_str(), c::printf(c"%.*g", p, v), "%.{p}g of {v:e}");
        let d = i % 9;
        assert_eq!(f_of(v, d), c::printf(c"%.*f", d, v), "%.{d}f of {v:e}");
    };

    let mut bits = 0u64;
    for i in 0..20_000 {
        bits = bits.wrapping_add(0x9E37_79B9_7F4A_7C15);
        let v = f64::from_bits(bits);
        if v.is_finite() {
            check(i, v);
        }
    }
    let stride = 200.0f32.to_bits() / 20_000;
    for i in 0..=20_000u32 {
        check(i as usize, f32::from_bits(i * stride) as f64);
    }
    for k in 0..20_000 {
        check(k, k as f64 / 1000.0);
        check(k + 1, -(k as f64) / 64.0);
    }
}
