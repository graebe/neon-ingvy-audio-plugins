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
boxes. Which thread may read which text is therefore `state`'s note, not this
module's.

# Numbers

A number of up to 19 digits is read as that integer, divided or multiplied
once by an exactly representable power of ten: one correctly rounded
operation, and so the same double `str::parse` gives -- which `fmt::atof`, the
hand-written reader's, also gives. Every number a build has written has at
most seven digits, and JavaScript, which rewrites them when Schwung stores a
blob, writes the shortest text that reads back to the same double.
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
