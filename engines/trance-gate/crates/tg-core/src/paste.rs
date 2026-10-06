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

Classifying allocates nothing, so the audio thread applies exactly what the
main thread checked, by reading the same text again.
*/

use crate::slotfile::{self, fields, is_number, pattern_ok, sound_ok, Kind, Value, MAX_FIELDS};
use crate::state::STATE_VERSION;
use crate::{rates, Instance, SLOTS};
use core::fmt::Write;

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

fn whole(s: &str) -> Option<u32> {
    (!s.is_empty() && s.len() <= 9 && s.bytes().all(|c| c.is_ascii_digit())).then(|| crate::fmt::atoi(s) as u32)
}

/// `p<N>` / `s<N>` for a slot 0..7: the slot, or None.
fn slot_field(key: &str, prefix: u8) -> Option<usize> {
    match key.as_bytes() {
        [p, d] if *p == prefix && d.is_ascii_digit() && ((d - b'0') as usize) < SLOTS => Some((d - b'0') as usize),
        _ => None,
    }
}

/// The fields of a state blob, checked: Ok when every one is the blob's.
fn patch(f: &[(&str, Value<'_>)]) -> Result<(), Refused> {
    let mut is_patch = false;
    for &(key, value) in f {
        let ok = match (key, value) {
            ("sv", Value::Num(v)) => {
                let v = whole(v).ok_or(Refused::Damaged)?;
                if v > STATE_VERSION as u32 {
                    return Err(Refused::Newer(v));
                }
                is_patch = true;
                true
            }
            ("slot", Value::Num(v)) => whole(v).is_some_and(|s| (s as usize) < SLOTS),
            ("rate", Value::Str(v)) => rates::RATES.iter().any(|r| r.label == v),
            ("rate", Value::Num(v)) => whole(v).is_some_and(|i| (i as usize) < rates::RATES.len()),
            (k, Value::Num(v)) if NUMBER_KEYS.contains(&k) => is_number(v),
            (k, v) if slot_field(k, b'p').is_some() => {
                is_patch = true;
                matches!(v, Value::Str(p) if pattern_ok(p))
            }
            (k, v) if slot_field(k, b's').is_some() => matches!(v, Value::Str(s) if sound_ok(s)),
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

/// What `text` holds, or why it cannot be pasted. Changes nothing, allocates
/// nothing.
pub fn classify(text: &str) -> Result<Holds, Refused> {
    /* Size first: a shell may hand over only the first MAX_BYTES + 1 bytes of
     * a larger text, and those say nothing about the rest. */
    if text.len() > slotfile::MAX_BYTES {
        return Err(Refused::NotTranceGate);
    }
    if text.trim().is_empty() {
        return Err(Refused::Empty);
    }
    let mut f = [("", Value::Num("")); MAX_FIELDS];
    let Ok(n) = fields(text, &mut f) else { return Err(Refused::NotTranceGate) };
    let f = &f[..n];
    /* A format id says it is a file's text, and a file's text is read as a
     * file is -- with the file's reasons when it is not a good one. */
    if f.iter().any(|(k, _)| *k == "format") {
        return match slotfile::check(text) {
            Ok(Kind::Slot) => Ok(Holds::Slot),
            Ok(Kind::Bank) => Ok(Holds::Bank),
            Err(slotfile::Error::NotAFile | slotfile::Error::WrongFormat) => Err(Refused::NotTranceGate),
            Err(e) => Err(Refused::File(e)),
        };
    }
    patch(f).map(|()| Holds::Patch)
}

impl Instance {
    /*
     * A PASTE, APPLIED: a slot into the current slot and a bank into all eight,
     * exactly as their files import; a patch replaces everything, as a state
     * load does. Classified whole first -- a refused text changes nothing.
     */
    pub fn paste(&mut self, text: &str) -> Result<Holds, Refused> {
        self.paste_into(self.slot, text)
    }

    /// As [`Instance::paste`], with a slot going into `slot` (0-based): the
    /// slot the person was looking at when they pasted, which a shell's host
    /// may move in the very block the paste lands in.
    pub fn paste_into(&mut self, slot: usize, text: &str) -> Result<Holds, Refused> {
        let holds = classify(text)?;
        match holds {
            Holds::Slot | Holds::Bank => {
                self.import_into(slot, text).map_err(Refused::File)?;
            }
            Holds::Patch => self.set_param("state", text),
        }
        Ok(holds)
    }
}

#[cfg(test)]
mod tests;
