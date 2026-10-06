// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
Paste: whatever text is on the clipboard, and what the engine makes of it.

Copy puts the current slot on the clipboard as a slot file's text (see
`slotfile`). Paste has to read more than that, because two other texts were on
clipboards before slots could be copied, and both still have to land:

| the text                                   | is            | replaces          |
|--------------------------------------------|---------------|-------------------|
| `"format": "ni-trance-gate-slot"`          | [`Holds::Slot`]  | the current slot  |
| `"format": "ni-trance-gate-bank"`          | [`Holds::Bank`]  | all eight slots   |
| the state blob (`{"sv":7,"slot":0,...}`)   | [`Holds::Patch`] | the whole patch   |

The state blob is what Copy put there before this version, and it is the Move
module's patch, verbatim -- so a patch carried between the hardware and the DAW
keeps working.

# Strict, as a file is

A clipboard holds anything, so the text is classified whole before anything
is applied, and refused with a reason when it is none of the three. A slot or
bank is read exactly as a file is (`slotfile`). A patch has to be the flat
object the state blob is: every key one the blob has ever had, each value of its
kind -- a number a number, the rate a known label, each slot's pattern and sound
a field this build could have written -- and at least a version or a pattern, so
`{}` or a stray `{"slot":0}` is not mistaken for a patch.

[`Clip::parse`] does the classifying and the reading in one pass, and
[`Instance::apply_clip`] applies the result without a byte of text, so it may
run on the audio thread. Where the parsing runs is the state module's note.
*/

use crate::json::{Object, Scalar, Sink};
use crate::slotfile::{self, pattern_ok, sound_ok, Kind, SlotFile};
use crate::state::{slot_field, Fields, Patch, STATE_VERSION};
use crate::{rates, Instance, SLOTS};
use core::fmt::Write;
use std::borrow::Cow;

/// What a pasted text holds.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Holds {
    /// One slot: replaces the current one.
    Slot,
    /// All eight: replaces every slot.
    Bank,
    /// A whole patch, the state blob: replaces everything.
    Patch,
}

/// Why a pasted text was refused. [`Refused::describe`] says it in words.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Refused {
    Empty,
    /// None of the three.
    NotTranceGate,
    /// A slot or bank that a file with the same text would be refused for.
    File(slotfile::Error),
    /// A patch whose fields are the blob's but whose values are not.
    Damaged,
    /// A patch from a newer build.
    Newer(u32),
}

impl Refused {
    pub fn describe(&self, out: &mut dyn Write) -> core::fmt::Result {
        match *self {
            Refused::Empty => write!(out, "The clipboard is empty."),
            Refused::NotTranceGate => write!(out, "The clipboard doesn't hold a Trance Gate slot."),
            Refused::File(e) => e.describe(out),
            Refused::Damaged => write!(out, "The patch on the clipboard is damaged."),
            Refused::Newer(v) => write!(
                out,
                "The patch is version {v}, made by a newer NI Trance Gate. This one reads version {STATE_VERSION}."
            ),
        }
    }
}

/// Every top-level key a state blob has carried that is a plain number --
/// `mix` and `depth` are v2's, folded into `amount` since.
const NUMBER_KEYS: [&str; 14] = [
    "attack", "decay", "sustain", "release", "hold", "amount", "mix", "depth", "fade", "fsoft", "fdir",
    "legato", "tmode", "curve",
];

/// A whole number of nine digits at most, as a version, a slot and a rate
/// index are written.
fn whole(v: &Scalar<'_>) -> Option<u32> {
    match *v {
        Scalar::Whole(n) if n <= 999_999_999 => Some(n as u32),
        _ => None,
    }
}

/// The fields of a state blob, checked in the order the text has them: Ok
/// when every one is the blob's.
fn patch(o: &Object<'_>) -> Result<(), Refused> {
    let mut is_patch = false;
    for (key, value) in o.fields() {
        let number = value.number().is_some();
        let ok = match (*key, value) {
            ("sv", v) if number => {
                let v = whole(v).ok_or(Refused::Damaged)?;
                if v > STATE_VERSION as u32 {
                    return Err(Refused::Newer(v));
                }
                is_patch = true;
                true
            }
            ("slot", v) if number => whole(v).is_some_and(|s| (s as usize) < SLOTS),
            ("rate", Scalar::Str(v)) => rates::RATES.iter().any(|r| r.label == &**v),
            ("rate", v) if number => whole(v).is_some_and(|i| (i as usize) < rates::RATES.len()),
            (k, _) if number && NUMBER_KEYS.contains(&k) => true,
            (k, v) if slot_field(k, b'p').is_some() => {
                is_patch = true;
                matches!(v, Scalar::Str(p) if pattern_ok(p))
            }
            (k, v) if slot_field(k, b's').is_some() => matches!(v, Scalar::Str(s) if sound_ok(s)),
            ("sv" | "slot", _) => false,
            (k, _) if NUMBER_KEYS.contains(&k) => false,
            /* A key no blob has ever had: not a damaged patch, not a patch. */
            _ => return Err(Refused::NotTranceGate),
        };
        if !ok {
            return Err(Refused::Damaged);
        }
    }
    if is_patch { Ok(()) } else { Err(Refused::NotTranceGate) }
}

/// A pasted text, read and checked: ready for [`Instance::apply_clip`].
#[derive(Clone, Debug, PartialEq)]
pub enum Clip {
    /// A slot or a bank, as its file would import.
    File(SlotFile),
    /// A whole patch.
    Patch(Patch),
}

impl Clip {
    /// What `text` holds, read, or why it cannot be pasted. Allocates nothing
    /// for a text a build wrote; see the state module on threads.
    pub fn parse(text: &str) -> Result<Clip, Refused> {
        /* Size first: a shell may hand over only the first MAX_BYTES + 1 bytes
         * of a larger text, and those say nothing about the rest. */
        if text.len() > slotfile::MAX_BYTES {
            return Err(Refused::NotTranceGate);
        }
        if text.trim().is_empty() {
            return Err(Refused::Empty);
        }
        let Ok(o) = Object::read(text) else { return Err(Refused::NotTranceGate) };
        /* A format id says it is a file's text, and a file's text is read as a
         * file is -- with the file's reasons when it is not a good one. */
        if o.get("format").is_some() {
            return match SlotFile::from_object(&o) {
                Ok(file) => Ok(Clip::File(file)),
                Err(slotfile::Error::NotAFile | slotfile::Error::WrongFormat) => Err(Refused::NotTranceGate),
                Err(e) => Err(Refused::File(e)),
            };
        }
        patch(&o)?;
        let mut fields = Fields::default();
        for (key, value) in o.fields() {
            let _ = fields.field(Cow::Borrowed(*key), value.clone());
        }
        Ok(Clip::Patch(Patch::from_fields(&fields)))
    }

    /// What the clip holds.
    pub fn holds(&self) -> Holds {
        match self {
            Clip::File(f) if f.kind() == Kind::Slot => Holds::Slot,
            Clip::File(_) => Holds::Bank,
            Clip::Patch(_) => Holds::Patch,
        }
    }
}

/// What `text` holds, or why it cannot be pasted. Changes nothing.
pub fn classify(text: &str) -> Result<Holds, Refused> {
    Clip::parse(text).map(|c| c.holds())
}

impl Instance {
    /*
     * A PASTE, APPLIED: a slot into one slot and a bank into all eight,
     * exactly as their files import; a patch replaces everything, as a state
     * load does. Allocation-free, and no text: the clip was classified and
     * read whole by `Clip::parse`.
     */
    /// Apply `clip`, a slot going into `slot` (0-based): the slot the person
    /// was looking at when they pasted, which a shell's host may move in the
    /// very block the paste lands in. Out of range is the current slot.
    pub fn apply_clip(&mut self, slot: usize, clip: &Clip) -> Holds {
        match clip {
            Clip::File(file) => {
                self.apply_file(slot, file);
            }
            Clip::Patch(patch) => self.load(patch),
        }
        clip.holds()
    }

    /// Read `text` and paste it into the current slot. A refused text changes
    /// nothing.
    pub fn paste(&mut self, text: &str) -> Result<Holds, Refused> {
        self.paste_into(self.slot, text)
    }

    /// [`Clip::parse`] and [`Instance::apply_clip`] in one call. A refused text
    /// changes nothing.
    pub fn paste_into(&mut self, slot: usize, text: &str) -> Result<Holds, Refused> {
        let clip = Clip::parse(text)?;
        Ok(self.apply_clip(slot, &clip))
    }
}

#[cfg(test)]
mod tests;
