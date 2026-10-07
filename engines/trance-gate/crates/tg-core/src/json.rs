// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The flat JSON object every Trance Gate text is -- the state blob, a slot or
bank file, a pasted patch -- as serde_json reads it.

All three are one level deep: string and number values under string keys. What
differs is what each does with a key it does not know or meets twice, so the
reading is in two halves. serde_json hands every key and value, in order, to a
[`Sink`], and the sink is the format's: the state blob's (`state::Fields`)
keeps the first of each key and passes over the rest, as the reader that
searched the text for a key always found the first; a file's and a paste's
([`Object`]) holds the text to the grammar this build writes.

# Allocation

Reading a text this repository wrote allocates nothing: keys and strings are
borrowed from it -- none of the formats has a character to escape -- numbers
are parsed in place, and both sinks are fixed-size. serde_json allocates for
what no build writes, an escaped string (decoded into a new one) or a value
nested two deep (skipped on a stack), and for every error it reports, which it
boxes.

[`read`] is therefore for a thread that may allocate. [`read_plain`] is for
one that may not -- the Move's audio callback, which is where a state blob is
read there -- and never allocates, whatever the text: it lets serde_json see
only a text [`plain`] has found it reads without an error and without a copy,
and refuses every other text itself. Which thread reads which text is
`state`'s note.

# Numbers

serde_json reads a number's digits as an integer and divides or multiplies it
once by a power of ten. Up to fifteen digits and 10^22 both are exact, so
that is one correctly rounded operation -- the same double `str::parse` gives,
and `fmt::atof`, the hand-written reader's, gave. Every number a build has
written has at most seven digits, and JavaScript, which rewrites them when
Schwung stores a blob, writes the shortest text that reads back to the same
double. (Past fifteen digits the integer itself can round first, which is why
the `float_roundtrip` feature exists; nothing here needs it.)
*/

use serde::de::{self, DeserializeSeed, Deserializer, IgnoredAny, MapAccess, SeqAccess, Visitor};
use std::borrow::Cow;
use std::fmt;

/// One value of a flat object, as JSON typed it.
#[derive(Clone, Debug, PartialEq)]
pub(crate) enum Scalar<'a> {
    Str(Cow<'a, str>),
    /// A number with no sign, fraction or exponent.
    Whole(u64),
    /// Any other number.
    Number(f64),
    /// `true`, `false`, `null`, an array or an object: nothing any of the
    /// formats has ever written as a value.
    Other,
}

impl Scalar<'_> {
    /// The number, whatever JSON wrote it as.
    pub(crate) fn number(&self) -> Option<f64> {
        match *self {
            Scalar::Whole(n) => Some(n as f64),
            Scalar::Number(n) => Some(n),
            _ => None,
        }
    }

    pub(crate) fn text(&self) -> Option<&str> {
        match self {
            Scalar::Str(s) => Some(s),
            _ => None,
        }
    }
}

/// Where a reader's keys and values go, in the order the text has them.
pub(crate) trait Sink<'a> {
    /// One field. An error stops the reading: the sink has seen enough to
    /// refuse the text, and says why in its own terms.
    fn field(&mut self, key: Cow<'a, str>, value: Scalar<'a>) -> Result<(), ()>;
}

/// Why a text could not be read whole: it is not a JSON object -- broken,
/// unfinished, another type, or followed by more than whitespace -- or its
/// sink stopped the reading.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub(crate) enum Unread {
    NotAnObject,
    Stopped,
}

/// Every key and value of `text`, which must be one JSON object and nothing
/// else, into `sink`.
pub(crate) fn read<'a>(text: &'a str, sink: &mut impl Sink<'a>) -> Result<(), Unread> {
    let mut stopped = false;
    let mut de = serde_json::Deserializer::from_str(text);
    let read = ObjectSeed { sink, stopped: &mut stopped }.deserialize(&mut de).and_then(|()| de.end());
    match read {
        Ok(()) => Ok(()),
        Err(_) if stopped => Err(Unread::Stopped),
        Err(_) => Err(Unread::NotAnObject),
    }
}

/// [`read`], for a thread that must not allocate: a text that is not
/// [`plain`] is refused before serde_json sees it, as `NotAnObject`, and every
/// other text is read, as by [`read`], without a copy or an error to box --
/// provided the sink never stops, as only a file's does.
pub(crate) fn read_plain<'a>(text: &'a str, sink: &mut impl Sink<'a>) -> Result<(), Unread> {
    if !plain(text) {
        return Err(Unread::NotAnObject);
    }
    read(text, sink)
}

/*
 * THE TEXTS SERDE_JSON READS WITHOUT ALLOCATING: one JSON object, nothing but
 * whitespace around it, no escape in any string, and no value nested more than
 * two deep. Every format here is that -- flat, with no character to escape --
 * so this is no second grammar to keep in step with the formats: it is
 * serde_json's own JSON, narrowed to what reads in place, and it decides only
 * whether serde_json is asked at all.
 *
 * The depth is serde_json's. A value that is an array or an object is walked
 * by the visitor (one deep) and its members skipped by serde_json (two deep);
 * a third level is the first that its skipping keeps on a stack, which it
 * allocates. Two deep keeps a blob from a newer build readable if it ever
 * nests a value under a key this build passes over.
 *
 * It must never pass a text serde_json then refuses: that refusal is the
 * allocation this exists to keep off the audio thread. Hence JSON's number
 * grammar to the letter (no leading zero, a digit on both sides of the point
 * and after the `e`), no control character inside a string, and one bound
 * JSON does not have. serde_json refuses a number too large for a double, so a
 * number is at most NUMBER_MAX bytes long and a positive exponent at most two
 * digits: below 10^32 * 10^99, far inside a double's range. No build writes a
 * number over ten bytes, and JavaScript's shortest form -- what Schwung
 * rewrites a stored blob's numbers to -- is at most 24.
 */
pub(crate) fn plain(text: &str) -> bool {
    let mut s = Scan { b: text.as_bytes(), i: 0 };
    s.ws();
    let object = s.peek() == Some(b'{') && s.members(b'}', |s| s.string() && s.ws_eat(b':') && s.value(DEPTH_MAX));
    s.ws();
    object && s.i == s.b.len()
}

/// The longest number [`plain`] passes; see there.
const NUMBER_MAX: usize = 32;
/// How deep [`plain`] lets a value nest; see there.
const DEPTH_MAX: u32 = 2;

struct Scan<'a> {
    b: &'a [u8],
    i: usize,
}

impl Scan<'_> {
    fn peek(&self) -> Option<u8> {
        self.b.get(self.i).copied()
    }

    fn eat(&mut self, c: u8) -> bool {
        let hit = self.peek() == Some(c);
        self.i += hit as usize;
        hit
    }

    /// JSON's whitespace, which is not Rust's: no form feed, no Unicode.
    fn ws(&mut self) {
        while matches!(self.peek(), Some(b' ' | b'\t' | b'\n' | b'\r')) {
            self.i += 1;
        }
    }

    fn ws_eat(&mut self, c: u8) -> bool {
        self.ws();
        self.eat(c)
    }

    fn digits(&mut self) -> usize {
        let from = self.i;
        while matches!(self.peek(), Some(b'0'..=b'9')) {
            self.i += 1;
        }
        self.i - from
    }

    /// A string with no escape and no control character, which serde_json
    /// borrows; the text is a `str`, so its bytes are UTF-8 already.
    fn string(&mut self) -> bool {
        if !self.eat(b'"') {
            return false;
        }
        loop {
            match self.peek() {
                Some(b'"') => {
                    self.i += 1;
                    return true;
                }
                Some(b'\\' | 0x00..=0x1f) | None => return false,
                Some(_) => self.i += 1,
            }
        }
    }

    /// A value, which may be an array or an object `depth` levels deep.
    fn value(&mut self, depth: u32) -> bool {
        self.ws();
        match self.peek() {
            Some(b'"') => self.string(),
            Some(b'-' | b'0'..=b'9') => self.number(),
            Some(b't') => self.word(b"true"),
            Some(b'f') => self.word(b"false"),
            Some(b'n') => self.word(b"null"),
            Some(b'[') if depth > 0 => self.members(b']', |s| s.value(depth - 1)),
            Some(b'{') if depth > 0 => self.members(b'}', |s| s.string() && s.ws_eat(b':') && s.value(depth - 1)),
            _ => false,
        }
    }

    /// An array's or an object's members, from its opening bracket to `close`.
    fn members(&mut self, close: u8, mut member: impl FnMut(&mut Self) -> bool) -> bool {
        self.i += 1;
        if self.ws_eat(close) {
            return true;
        }
        loop {
            self.ws();
            if !member(self) {
                return false;
            }
            if self.ws_eat(close) {
                return true;
            }
            if !self.eat(b',') {
                return false;
            }
        }
    }

    fn word(&mut self, w: &[u8]) -> bool {
        let hit = self.b[self.i..].starts_with(w);
        self.i += if hit { w.len() } else { 0 };
        hit
    }

    /// `-? (0 | [1-9][0-9]*) (. [0-9]+)? ([eE] [+-]? [0-9]+)?`, within the
    /// bounds [`plain`] gives.
    fn number(&mut self) -> bool {
        let from = self.i;
        self.eat(b'-');
        match self.peek() {
            Some(b'0') => self.i += 1,
            Some(b'1'..=b'9') => {
                self.digits();
            }
            _ => return false,
        }
        if self.eat(b'.') && self.digits() == 0 {
            return false;
        }
        if self.eat(b'e') || self.eat(b'E') {
            let negative = self.eat(b'-');
            if !negative {
                self.eat(b'+');
            }
            match self.digits() {
                0 => return false,
                n if !negative && n > 2 => return false,
                _ => {}
            }
        }
        self.i - from <= NUMBER_MAX
    }
}

struct ObjectSeed<'s, S> {
    sink: &'s mut S,
    stopped: &'s mut bool,
}

impl<'de, S: Sink<'de>> DeserializeSeed<'de> for ObjectSeed<'_, S> {
    type Value = ();

    fn deserialize<D: Deserializer<'de>>(self, d: D) -> Result<(), D::Error> {
        d.deserialize_map(self)
    }
}

impl<'de, S: Sink<'de>> Visitor<'de> for ObjectSeed<'_, S> {
    type Value = ();

    fn expecting(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.write_str("a flat JSON object")
    }

    fn visit_map<A: MapAccess<'de>>(self, mut map: A) -> Result<(), A::Error> {
        while let Some(key) = map.next_key_seed(TextSeed)? {
            let value: Scalar<'de> = map.next_value()?;
            if self.sink.field(key, value).is_err() {
                *self.stopped = true;
                return Err(de::Error::custom("refused by its format"));
            }
        }
        Ok(())
    }
}

/// A string, borrowed from the text unless an escape had to be decoded.
struct TextSeed;

impl<'de> DeserializeSeed<'de> for TextSeed {
    type Value = Cow<'de, str>;

    fn deserialize<D: Deserializer<'de>>(self, d: D) -> Result<Cow<'de, str>, D::Error> {
        d.deserialize_str(TextVisitor)
    }
}

struct TextVisitor;

impl<'de> Visitor<'de> for TextVisitor {
    type Value = Cow<'de, str>;

    fn expecting(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.write_str("a string")
    }

    fn visit_borrowed_str<E: de::Error>(self, s: &'de str) -> Result<Cow<'de, str>, E> {
        Ok(Cow::Borrowed(s))
    }

    fn visit_str<E: de::Error>(self, s: &str) -> Result<Cow<'de, str>, E> {
        Ok(Cow::Owned(s.to_owned()))
    }
}

impl<'de> de::Deserialize<'de> for Scalar<'de> {
    fn deserialize<D: Deserializer<'de>>(d: D) -> Result<Scalar<'de>, D::Error> {
        d.deserialize_any(ScalarVisitor)
    }
}

struct ScalarVisitor;

impl<'de> Visitor<'de> for ScalarVisitor {
    type Value = Scalar<'de>;

    fn expecting(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.write_str("a JSON value")
    }

    fn visit_borrowed_str<E: de::Error>(self, s: &'de str) -> Result<Scalar<'de>, E> {
        Ok(Scalar::Str(Cow::Borrowed(s)))
    }

    fn visit_str<E: de::Error>(self, s: &str) -> Result<Scalar<'de>, E> {
        Ok(Scalar::Str(Cow::Owned(s.to_owned())))
    }

    fn visit_u64<E: de::Error>(self, n: u64) -> Result<Scalar<'de>, E> {
        Ok(Scalar::Whole(n))
    }

    fn visit_i64<E: de::Error>(self, n: i64) -> Result<Scalar<'de>, E> {
        Ok(Scalar::Number(n as f64))
    }

    fn visit_f64<E: de::Error>(self, n: f64) -> Result<Scalar<'de>, E> {
        Ok(Scalar::Number(n))
    }

    fn visit_bool<E: de::Error>(self, _: bool) -> Result<Scalar<'de>, E> {
        Ok(Scalar::Other)
    }

    fn visit_unit<E: de::Error>(self) -> Result<Scalar<'de>, E> {
        Ok(Scalar::Other)
    }

    fn visit_seq<A: SeqAccess<'de>>(self, mut seq: A) -> Result<Scalar<'de>, A::Error> {
        while seq.next_element::<IgnoredAny>()?.is_some() {}
        Ok(Scalar::Other)
    }

    fn visit_map<A: MapAccess<'de>>(self, mut map: A) -> Result<Scalar<'de>, A::Error> {
        while map.next_entry::<IgnoredAny, IgnoredAny>()?.is_some() {}
        Ok(Scalar::Other)
    }
}

/*
 * A FILE'S OR A PASTE'S FIELDS, IN ORDER, HELD TO THE GRAMMAR THIS BUILD
 * WRITES: a key at most once, a value that is a string or a number, and no
 * escape anywhere -- nothing in these formats needs one, so a text that has
 * one was not written by a build. The first fault in the text is the one
 * reported, as the hand-written tokenizer this replaces reported it.
 */
pub(crate) struct Object<'a> {
    fields: arrayvec::ArrayVec<(&'a str, Scalar<'a>), MAX_FIELDS>,
    fault: Option<Fault>,
}

/// Room for more fields than any file has -- format, version and two per
/// slot -- so a text with a few extra is read far enough to say what it is.
pub(crate) const MAX_FIELDS: usize = 40;

/// What made an [`Object`] stop.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub(crate) enum Fault {
    /// Not the flat `{ "key": value, ... }` these formats are.
    NotAFile,
    /// A key that appears twice.
    Duplicate,
    /// More fields than any of these formats has.
    TooMany,
}

impl<'a> Object<'a> {
    pub(crate) fn read(text: &'a str) -> Result<Object<'a>, Fault> {
        let mut o = Object { fields: arrayvec::ArrayVec::new(), fault: None };
        match read(text, &mut o) {
            Ok(()) => Ok(o),
            Err(Unread::Stopped) => Err(o.fault.unwrap_or(Fault::NotAFile)),
            Err(Unread::NotAnObject) => Err(Fault::NotAFile),
        }
    }

    pub(crate) fn get(&self, key: &str) -> Option<&Scalar<'a>> {
        self.fields.iter().find(|(k, _)| *k == key).map(|(_, v)| v)
    }

    pub(crate) fn fields(&self) -> &[(&'a str, Scalar<'a>)] {
        &self.fields
    }
}

impl<'a> Sink<'a> for Object<'a> {
    fn field(&mut self, key: Cow<'a, str>, value: Scalar<'a>) -> Result<(), ()> {
        let fault = match (key, &value) {
            (Cow::Borrowed(key), Scalar::Str(Cow::Borrowed(_)) | Scalar::Whole(_) | Scalar::Number(_)) => {
                if self.get(key).is_some() {
                    Fault::Duplicate
                } else if self.fields.try_push((key, value)).is_err() {
                    Fault::TooMany
                } else {
                    return Ok(());
                }
            }
            _ => Fault::NotAFile,
        };
        self.fault = Some(fault);
        Err(())
    }
}

#[cfg(test)]
mod tests;
