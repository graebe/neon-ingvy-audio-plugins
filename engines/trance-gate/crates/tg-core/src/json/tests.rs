// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! The flat-object reader: what it hands a sink, what it borrows, and what a
//! file's grammar refuses.

use super::{plain, read, read_plain, Fault, Object, Scalar, Sink, Unread, MAX_FIELDS};
use crate::fmt;
use std::borrow::Cow;

/// Every field, as the reader handed it over.
#[derive(Default)]
struct All<'a>(Vec<(Cow<'a, str>, Scalar<'a>)>);

impl<'a> Sink<'a> for All<'a> {
    fn field(&mut self, key: Cow<'a, str>, value: Scalar<'a>) -> Result<(), ()> {
        self.0.push((key, value));
        Ok(())
    }
}

fn all(text: &str) -> Result<Vec<(Cow<'_, str>, Scalar<'_>)>, Unread> {
    let mut sink = All::default();
    read(text, &mut sink).map(|()| sink.0)
}

#[test]
fn a_blob_is_read_in_order_and_borrowed_from_its_text() {
    let text = "{\"sv\":7,\"rate\":\"1/16\",\"attack\":1.60,\"p0\":\"5555:0:16:\",\"x\":null}";
    let got = all(text).unwrap();
    let keys: Vec<&str> = got.iter().map(|(k, _)| k.as_ref()).collect();
    assert_eq!(keys, ["sv", "rate", "attack", "p0", "x"]);
    assert_eq!(got[0].1, Scalar::Whole(7));
    assert_eq!(got[1].1, Scalar::Str(Cow::Borrowed("1/16")));
    assert_eq!(got[2].1, Scalar::Number(1.6));
    assert_eq!(got[4].1, Scalar::Other);
    /* No copy of any of it: what makes reading allocation-free. */
    assert!(got.iter().all(|(k, v)| matches!(k, Cow::Borrowed(_)) && !matches!(v, Scalar::Str(Cow::Owned(_)))));
}

#[test]
fn whitespace_and_javascripts_numbers_are_the_same_object() {
    /* How a Schwung patch file stores a blob. */
    let pretty = "{\n  \"sv\": 7,\n  \"sustain\": 1,\n  \"fade\": 0.35\n}";
    let compact = "{\"sv\":7,\"sustain\":1.000,\"fade\":0.3500}";
    let numbers = |text| all(text).unwrap().into_iter().map(|(_, v)| v.number()).collect::<Vec<_>>();
    assert_eq!(numbers(pretty), numbers(compact));
}

#[test]
fn every_number_is_the_double_atof_reads() {
    /* Up to seven digits at up to four decimals, as every blob has carried
     * them, and the signs and forms JSON allows. The hand-written reader used
     * fmt::atof; serde_json must land on the same double, or a value moves by
     * an ulp on load. */
    let mut texts: Vec<String> = Vec::new();
    for decimals in 0..=4u32 {
        for i in (0..10_000_000u64).step_by(7919) {
            let scale = 10u64.pow(decimals);
            let text = if decimals == 0 {
                i.to_string()
            } else {
                format!("{}.{:0width$}", i / scale, i % scale, width = decimals as usize)
            };
            texts.push(text.clone());
            texts.push(format!("-{text}"));
        }
    }
    texts.extend(["0", "-0", "0.0", "1e3", "2.5E-2", "199.99", "0.1", "0.7123", "45.25"].map(String::from));
    for text in texts {
        let object = format!("{{\"x\":{text}}}");
        let got = all(&object).unwrap()[0].1.number().unwrap();
        assert_eq!(got.to_bits(), fmt::atof(&text).to_bits(), "{text}");
        assert_eq!(got.to_bits(), text.parse::<f64>().unwrap().to_bits(), "{text}");
    }
}

#[test]
fn a_whole_number_is_told_from_any_other() {
    let whole = |text: &str| match all(&format!("{{\"v\":{text}}}")).unwrap()[0].1 {
        Scalar::Whole(n) => Some(n),
        Scalar::Number(_) => None,
        ref other => panic!("{text} read as {other:?}"),
    };
    assert_eq!(whole("1"), Some(1));
    assert_eq!(whole("999999999"), Some(999_999_999));
    for text in ["1.0", "1e0", "-1", "-0", "1.5"] {
        assert_eq!(whole(text), None, "{text}");
    }
}

#[test]
fn an_escape_is_decoded_by_the_reader_and_refused_by_a_file_and_a_plain_read() {
    let text = "{\"rate\":\"1\\/16\"}";
    assert_eq!(all(text).unwrap()[0].1.text(), Some("1/16"));
    assert_eq!(Object::read(text).err(), Some(Fault::NotAFile));
    assert_eq!(Object::read("{\"r\\u0061te\":\"1/16\"}").err(), Some(Fault::NotAFile));
    /* serde_json decodes it into a copy, so the reader that must not allocate
     * never asks it to. */
    assert_eq!(read_plain(text, &mut All::default()), Err(Unread::NotAnObject));
}

/* What the reader that must not allocate passes to serde_json, and what it
 * refuses itself. */
#[test]
fn a_plain_text_is_one_object_that_serde_json_reads_in_place() {
    for text in [
        "{}",
        " {\n\t\"sv\" : 7 ,\r\n \"x\":null } ",
        "{\"sv\":7,\"rate\":\"1/16\",\"attack\":1.60,\"p0\":\"5555:0:16:\"}",
        "{\"a\":true,\"b\":false,\"c\":-0,\"d\":0.5,\"e\":1e-7,\"f\":2.5E+2,\"g\":1e-300}",
        "{\"name\":\"Größe ♪\"}",
        /* Nested values, as a newer build might write under a key this one
         * passes over: two deep and no deeper. */
        "{\"x\":[],\"y\":{},\"z\":[1,\"a\",{\"k\":2},[]],\"w\":{\"k\":[1,2],\"l\":{}}}",
        "{\"n\":12345678901234567890123456789012}",
    ] {
        assert!(plain(text), "{text}");
        assert!(read_plain(text, &mut All::default()).is_ok(), "{text}");
    }
    for text in [
        "",
        " ",
        "null",
        "[]",
        "\"x\"",
        "{",
        "}",
        "{\"sv\":7,",
        "{\"sv\":}",
        "{\"sv\" 7}",
        "{,}",
        "{\"a\":1,}",
        "{\"a\":1}}",
        "{\"a\":1} x",
        "{a:1}",
        "{\"a\":01}",
        "{\"a\":1.}",
        "{\"a\":.5}",
        "{\"a\":-}",
        "{\"a\":1e}",
        "{\"a\":1e+}",
        "{\"a\":+1}",
        "{\"a\":tru}",
        "{\"a\":nul}",
        "{\"a\":nullx}",
        "{\"a\":\"x}",
        "{\"a\":\"x\\ny\"}",
        "{\"a\\u0062\":1}",
        "{\"a\":\"tab\there\"}",
        "{\"a\":[1,]}",
        "{\"a\":[1}",
        "{\"a\":{\"b\"}}",
        /* Three deep: serde_json skips it on a stack it allocates. */
        "{\"a\":[[[]]]}",
        "{\"a\":{\"b\":{\"c\":[]}}}",
        /* Too large for a double, which serde_json refuses. */
        "{\"a\":1e999}",
        "{\"a\":1e+100}",
        "{\"a\":123456789012345678901234567890123}",
        "\u{feff}{}",
        "{}\u{c}",
    ] {
        assert!(!plain(text), "{text}");
        assert_eq!(read_plain(text, &mut All::default()), Err(Unread::NotAnObject), "{text}");
    }
}

proptest::proptest! {
    /* plain may refuse what serde_json reads, but never pass what it refuses
     * or has to copy: that would be the allocation it exists to prevent. The
     * texts are drawn from JSON's own alphabet, so most are near misses. */
    #[test]
    fn a_plain_text_never_makes_serde_json_refuse_or_copy(text in "[{}\\[\\],:\" \\\\a0-9.eE+\\-tnul\n]{0,40}") {
        if plain(&text) {
            let got = all(&text);
            proptest::prop_assert!(got.is_ok(), "{text:?}");
            proptest::prop_assert!(got.unwrap().iter().all(|(k, v)| matches!(k, Cow::Borrowed(_)) && !matches!(v, Scalar::Str(Cow::Owned(_)))));
        }
    }

    /* And every way of breaking a blob a build wrote, which is what a damaged
     * patch file is. */
    #[test]
    fn a_damaged_blob_is_plain_only_when_serde_json_reads_it(cut in 0usize..200, at in 0usize..200, byte in proptest::sample::select(b"{}[]:,\"\\0-.e ".to_vec())) {
        let blob = "{\"sv\":7,\"slot\":0,\"rate\":\"1/16\",\"attack\":1.60,\"sustain\":1.000,\"p0\":\"5555:0:16:\",\"s3\":\"1/4:1.6:20.0\"}";
        let mut damaged = blob.as_bytes()[..cut.min(blob.len())].to_vec();
        damaged.insert(at.min(damaged.len()), byte);
        let text = String::from_utf8(damaged).unwrap();
        proptest::prop_assert!(!plain(&text) || all(&text).is_ok(), "{text:?}");
    }
}

#[test]
fn what_is_not_one_flat_object_is_not_read() {
    for text in ["", "hello", "[1,2]", "\"x\"", "{", "{\"a\":1,}", "{\"a\":1} trailing", "{\"a\":01}", "{\"a\":1.}"] {
        assert_eq!(all(text).err(), Some(Unread::NotAnObject), "{text}");
        assert_eq!(Object::read(text).err(), Some(Fault::NotAFile), "{text}");
    }
    /* Read, but not by a file's grammar: a value that is neither a string nor
     * a number. */
    for text in ["{\"a\":true}", "{\"a\":null}", "{\"a\":[1]}", "{\"a\":{\"b\":1}}"] {
        assert!(all(text).is_ok(), "{text}");
        assert_eq!(Object::read(text).err(), Some(Fault::NotAFile), "{text}");
    }
}

#[test]
fn a_file_fault_is_the_first_one_in_the_text() {
    assert_eq!(Object::read("{\"a\":1,\"a\":2}").err(), Some(Fault::Duplicate));
    /* The duplicate comes first, so it is the one reported... */
    assert_eq!(Object::read("{\"a\":1,\"a\":2,}").err(), Some(Fault::Duplicate));
    /* ...and here the broken value does. */
    assert_eq!(Object::read("{\"a\":1,\"b\":01,\"a\":2}").err(), Some(Fault::NotAFile));
    let fields = |n: usize| {
        let body: Vec<String> = (0..n).map(|i| format!("\"k{i}\":{i}")).collect();
        format!("{{{}}}", body.join(","))
    };
    assert_eq!(Object::read(&fields(MAX_FIELDS)).map(|o| o.fields().len()), Ok(MAX_FIELDS));
    assert_eq!(Object::read(&fields(MAX_FIELDS + 1)).err(), Some(Fault::TooMany));
}

#[test]
fn a_file_keeps_its_fields_in_order() {
    let o = Object::read("{\"format\": \"x\", \"version\": 1, \"sound\": \"s\"}").unwrap();
    let keys: Vec<&str> = o.fields().iter().map(|(k, _)| *k).collect();
    assert_eq!(keys, ["format", "version", "sound"]);
    assert_eq!(o.get("version"), Some(&Scalar::Whole(1)));
    assert_eq!(o.get("pattern"), None);
}
