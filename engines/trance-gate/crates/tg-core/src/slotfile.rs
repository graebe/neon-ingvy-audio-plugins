/*!
Slot files: one slot, or all eight, as a small text file a person can read.

```text
{
  "format": "ni-trance-gate-slot",
  "version": 1,
  "sound": "1/16:1.60:16.00:1.000:16.00:1.000:1.000:1.0000:0:0:0:0:0",
  "pattern": "5555:0:16"
}
```

A bank is the same with `"format": "ni-trance-gate-bank"` and the two fields
numbered per slot, `"sound1"` / `"pattern1"` up to `"sound8"` / `"pattern8"`.
The fields are the state blob's own (`s<N>` and `p<N>`, see `state`), written
and read by the same code, so a slot exported and imported is the slot that
was saved -- and the engine owns the format end to end; a shell only moves
bytes.

# Reading is strict

A file comes from outside, so it is checked whole before anything is applied:
the format id and version, every field present and nothing else, every number a
number, every switch 0 or 1, the rate a known label, the masks hex of the right
size, the length 1..128. Out-of-range VALUES are clamped exactly as a state load
clamps them -- an Attack of 900 is a long attack, not a broken file -- but a
file that is not one this build could have written is refused with a reason,
and nothing changes.

Parsing allocates nothing, so the audio thread can apply what the main thread
has already checked: the same text, read again, at the top of a block.
*/

use crate::fmt::{self, Buf};
use crate::sound::Sound;
use crate::state::{read_pattern, write_pattern};
use crate::{rates, Instance, Pattern, MAX_STEPS, SLOTS};
use core::fmt::Write;

pub const SLOT_FORMAT: &str = "ni-trance-gate-slot";
pub const BANK_FORMAT: &str = "ni-trance-gate-bank";
/// The version this build writes, and the newest it reads.
pub const VERSION: u32 = 1;
/// The longest file this reads: a bank at its heaviest is about 6 KB.
pub const MAX_BYTES: usize = 16 * 1024;

/// What a file holds.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Kind {
    /// One slot: replaces the current one.
    Slot,
    /// All eight: replaces every slot.
    Bank,
}

/// Why a file was refused. [`Error::describe`] says it in words.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Error {
    Empty,
    TooLarge,
    /// Not the `{ "key": value, ... }` this format is.
    NotAFile,
    /// A format id that is not a Trance Gate slot or bank.
    WrongFormat,
    /// A version newer than this build reads.
    Newer(u32),
    /// A version that is not a positive whole number.
    BadVersion,
    /// A key this format does not have.
    UnknownKey,
    /// A key that appears twice.
    Duplicate,
    /// The sound or pattern of slot `n` (1-based; 0 in a single-slot file)
    /// is missing.
    Missing { pattern: bool, slot: u8 },
    /// ...or is there and is not one.
    Malformed { pattern: bool, slot: u8 },
}

impl Error {
    pub fn describe(&self, out: &mut dyn Write) -> core::fmt::Result {
        match *self {
            Error::Empty => write!(out, "The file is empty."),
            Error::TooLarge => write!(out, "The file is too large to be a Trance Gate slot file."),
            Error::NotAFile | Error::WrongFormat => {
                write!(out, "This is not a Trance Gate slot or bank file.")
            }
            Error::Newer(v) => write!(
                out,
                "The file is version {v}, made by a newer NI Trance Gate. This one reads version {VERSION}."
            ),
            Error::BadVersion => write!(out, "The file's version is not a number."),
            Error::UnknownKey => write!(out, "The file has a field this format does not have."),
            Error::Duplicate => write!(out, "The file has a field twice."),
            Error::Missing { pattern, slot } | Error::Malformed { pattern, slot } => {
                let what = if pattern { "pattern" } else { "sound" };
                let how = if matches!(self, Error::Missing { .. }) { "is missing" } else { "is damaged" };
                if slot == 0 {
                    write!(out, "The slot's {what} {how}.")
                } else {
                    write!(out, "Slot {slot}'s {what} {how}.")
                }
            }
        }
    }
}

/* ------------------------------------------------------------- the writer */

impl Instance {
    /// The current slot as a slot file, or every slot as a bank, into `out`.
    /// Returns the length written, as `get_param` does.
    pub fn export(&self, kind: Kind, out: &mut [u8]) -> i32 {
        let mut b = Buf::new(out);
        let _ = write_file(self, kind, &mut b);
        b.finish()
    }
}

fn write_file(inst: &Instance, kind: Kind, b: &mut dyn Write) -> core::fmt::Result {
    let format = if kind == Kind::Slot { SLOT_FORMAT } else { BANK_FORMAT };
    write!(b, "{{\n  \"format\": \"{format}\",\n  \"version\": {VERSION}")?;
    match kind {
        Kind::Slot => write_slot(b, &inst.snd[inst.slot], &inst.pat[inst.slot], "")?,
        Kind::Bank => {
            for s in 0..SLOTS {
                let mut n = [0u8; 2];
                let n = digit(&mut n, s + 1);
                write_slot(b, &inst.snd[s], &inst.pat[s], n)?;
            }
        }
    }
    write!(b, "\n}}\n")
}

fn write_slot(b: &mut dyn Write, s: &Sound, p: &Pattern, n: &str) -> core::fmt::Result {
    write!(b, ",\n  \"sound{n}\": \"")?;
    s.write(b)?;
    write!(b, "\",\n  \"pattern{n}\": \"")?;
    write_pattern(p, b)?;
    write!(b, "\"")
}

fn digit(buf: &mut [u8; 2], n: usize) -> &str {
    buf[0] = b'0' + (n % 10) as u8;
    core::str::from_utf8(&buf[..1]).unwrap_or("")
}

/* ------------------------------------------------------------- the reader */

/// Room for more fields than any file has -- format, version and two per
/// slot -- so a text with a few extra is read far enough to say what it is.
pub(crate) const MAX_FIELDS: usize = 40;

#[derive(Clone, Copy)]
pub(crate) enum Value<'a> {
    Str(&'a str),
    Num(&'a str),
}

/*
 * A FLAT JSON OBJECT, STRICTLY: `{`, then `"key": value` pairs separated by
 * commas, then `}` and nothing but whitespace. A value is a string without
 * escapes (nothing in this format needs one) or a run of digits. No nesting,
 * no trailing comma, no duplicate keys.
 */
pub(crate) fn fields<'a>(text: &'a str, out: &mut [(&'a str, Value<'a>); MAX_FIELDS]) -> Result<usize, Error> {
    let b = text.as_bytes();
    let mut i = 0;
    let ws = |i: &mut usize| {
        while *i < b.len() && matches!(b[*i], b' ' | b'\t' | b'\n' | b'\r') {
            *i += 1;
        }
    };
    let string = |i: &mut usize| -> Result<&'a str, Error> {
        if b.get(*i) != Some(&b'"') {
            return Err(Error::NotAFile);
        }
        let start = *i + 1;
        let mut j = start;
        while j < b.len() && b[j] != b'"' {
            if b[j] == b'\\' || b[j] < 0x20 {
                return Err(Error::NotAFile);
            }
            j += 1;
        }
        if j >= b.len() {
            return Err(Error::NotAFile);
        }
        *i = j + 1;
        Ok(&text[start..j])
    };

    ws(&mut i);
    if b.get(i) != Some(&b'{') {
        return Err(Error::NotAFile);
    }
    i += 1;
    ws(&mut i);
    let mut n = 0;
    if b.get(i) == Some(&b'}') {
        i += 1;
    } else {
        loop {
            ws(&mut i);
            let key = string(&mut i)?;
            ws(&mut i);
            if b.get(i) != Some(&b':') {
                return Err(Error::NotAFile);
            }
            i += 1;
            ws(&mut i);
            let value = match b.get(i) {
                Some(b'"') => Value::Str(string(&mut i)?),
                Some(c) if c.is_ascii_digit() || *c == b'-' => {
                    let start = i;
                    i += 1;
                    while i < b.len() && (b[i].is_ascii_digit() || b[i] == b'.') {
                        i += 1;
                    }
                    Value::Num(&text[start..i])
                }
                _ => return Err(Error::NotAFile),
            };
            if out[..n].iter().any(|(k, _)| *k == key) {
                return Err(Error::Duplicate);
            }
            if n == MAX_FIELDS {
                return Err(Error::UnknownKey);
            }
            out[n] = (key, value);
            n += 1;
            ws(&mut i);
            match b.get(i) {
                Some(b',') => i += 1,
                Some(b'}') => {
                    i += 1;
                    break;
                }
                _ => return Err(Error::NotAFile),
            }
        }
    }
    ws(&mut i);
    if i != b.len() {
        return Err(Error::NotAFile);
    }
    Ok(n)
}

fn is_hex(s: &str) -> bool {
    s.bytes().all(|c| c.is_ascii_hexdigit())
}

pub(crate) fn is_number(s: &str) -> bool {
    let t = s.strip_prefix('-').unwrap_or(s);
    let mut dots = 0;
    !t.is_empty()
        && t.bytes().all(|c| {
            if c == b'.' {
                dots += 1;
            }
            c.is_ascii_digit() || c == b'.'
        })
        && dots <= 1
        && t != "."
}

/// A sound field this build could have written: thirteen parts, the rate a
/// known label, seven numbers, then five whole-number switches.
pub(crate) fn sound_ok(field: &str) -> bool {
    let mut n = 0;
    for (i, part) in field.split(':').enumerate() {
        let ok = match i {
            0 => rates::RATES.iter().any(|r| r.label == part),
            1..=7 => is_number(part),
            12 => matches!(part, "0" | "1" | "2"),
            8..=11 => matches!(part, "0" | "1"),
            _ => false,
        };
        if !ok {
            return false;
        }
        n += 1;
    }
    n == 13
}

/// A pattern field this build could have written: two masks of up to 128
/// steps, a length of 1..128, then optionally the levels and the order, two
/// hex digits a step.
pub(crate) fn pattern_ok(field: &str) -> bool {
    let mut n = 0;
    for (i, part) in field.split(':').enumerate() {
        let ok = match i {
            0 | 1 => !part.is_empty() && part.len() <= MAX_STEPS / 4 && is_hex(part),
            2 => {
                !part.is_empty()
                    && part.bytes().all(|c| c.is_ascii_digit())
                    && part.len() <= 3
                    && (1..=MAX_STEPS as i64).contains(&fmt::atoi(part))
            }
            3 | 4 => part.len() % 2 == 0 && part.len() <= 2 * MAX_STEPS && is_hex(part),
            _ => false,
        };
        if !ok {
            return false;
        }
        n += 1;
    }
    n >= 3
}

/// One slot as a file holds it, checked.
#[derive(Clone, Copy)]
struct SlotText<'a> {
    sound: &'a str,
    pattern: &'a str,
}

/*
 * THE WHOLE FILE, CHECKED, without touching anything. A bank's eight slots are
 * all found good before the caller applies one.
 */
fn read(text: &str) -> Result<(Kind, [SlotText<'_>; SLOTS]), Error> {
    if text.trim().is_empty() {
        return Err(Error::Empty);
    }
    if text.len() > MAX_BYTES {
        return Err(Error::TooLarge);
    }
    let mut f = [("", Value::Num("")); MAX_FIELDS];
    let n = fields(text, &mut f)?;
    let f = &f[..n];
    let get = |key: &str| f.iter().find(|(k, _)| *k == key).map(|(_, v)| *v);

    let kind = match get("format") {
        Some(Value::Str(SLOT_FORMAT)) => Kind::Slot,
        Some(Value::Str(BANK_FORMAT)) => Kind::Bank,
        _ => return Err(Error::WrongFormat),
    };
    match get("version") {
        Some(Value::Num(v)) if v.bytes().all(|c| c.is_ascii_digit()) && v.len() <= 9 => {
            let v = fmt::atoi(v) as u32;
            if v == 0 {
                return Err(Error::BadVersion);
            }
            if v > VERSION {
                return Err(Error::Newer(v));
            }
        }
        _ => return Err(Error::BadVersion),
    }

    let slots = if kind == Kind::Slot { 1 } else { SLOTS };
    let mut out = [SlotText { sound: "", pattern: "" }; SLOTS];
    for (s, slot) in out.iter_mut().enumerate().take(slots) {
        let label = if kind == Kind::Slot { 0 } else { s as u8 + 1 };
        let mut sk = [0u8; 8];
        let mut pk = [0u8; 10];
        let (sk, pk) = (key(&mut sk, "sound", label), key(&mut pk, "pattern", label));
        let field = |k: &str, pattern: bool| match get(k) {
            Some(Value::Str(v)) => Ok(v),
            Some(Value::Num(_)) => Err(Error::Malformed { pattern, slot: label }),
            None => Err(Error::Missing { pattern, slot: label }),
        };
        slot.sound = field(sk, false)?;
        slot.pattern = field(pk, true)?;
        if !sound_ok(slot.sound) {
            return Err(Error::Malformed { pattern: false, slot: label });
        }
        if !pattern_ok(slot.pattern) {
            return Err(Error::Malformed { pattern: true, slot: label });
        }
    }
    /* Exactly the fields the format has: two, and two per slot. */
    if n != 2 + 2 * slots {
        return Err(Error::UnknownKey);
    }
    Ok((kind, out))
}

/// `sound`, `sound3`, `pattern8`: a field's name, without allocating.
fn key<'a>(buf: &'a mut [u8], base: &str, slot: u8) -> &'a str {
    let n = base.len();
    buf[..n].copy_from_slice(base.as_bytes());
    let len = if slot == 0 {
        n
    } else {
        buf[n] = b'0' + slot;
        n + 1
    };
    core::str::from_utf8(&buf[..len]).unwrap_or("")
}

/// What `text` is, or why it cannot be imported. Changes nothing.
pub fn check(text: &str) -> Result<Kind, Error> {
    read(text).map(|(k, _)| k)
}

impl Instance {
    /*
     * A FILE, APPLIED: a slot file replaces the current slot's sound and
     * pattern, a bank replaces all eight. Checked whole first -- a refused file
     * changes nothing. The playhead and the envelope carry on as through a
     * switch: the new sound is in place, and the glides and the latched stage
     * take the gain across.
     */
    pub fn import(&mut self, text: &str) -> Result<Kind, Error> {
        self.import_into(self.slot, text)
    }

    /// As [`Instance::import`], with a slot file going into `slot` (0-based)
    /// rather than the current one -- for a shell whose host may have moved
    /// the Slot in the same block, after the import was queued. A slot out of
    /// range is the current one.
    pub fn import_into(&mut self, slot: usize, text: &str) -> Result<Kind, Error> {
        let (kind, slots) = read(text)?;
        let from_curve = self.snd().curve;
        let slot = if slot < SLOTS { slot } else { self.slot };
        let targets = if kind == Kind::Slot { slot..slot + 1 } else { 0..SLOTS };
        for (i, s) in targets.enumerate() {
            let t = slots[i];
            /* `sound_ok` passed, so this parses. */
            if let Some(sound) = Sound::parse(t.sound) {
                self.snd[s] = sound;
            }
            read_pattern(&mut self.pat[s], t.pattern);
        }
        let len = self.pat[self.slot].length;
        if self.cursor >= len {
            self.cursor = len - 1;
        }
        self.reanchor(from_curve);
        self.recalc_ms_per_step();
        self.recalc_fade();
        self.rev = self.rev.wrapping_add(1);
        Ok(kind)
    }
}

#[cfg(test)]
mod tests;
