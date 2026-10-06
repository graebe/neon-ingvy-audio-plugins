// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
`set_param` and `get_param` -- the string-keyed surface both shells drive.

Every value crosses a `char*`, so this module is where C's conventions live:
lenient parsing ([`fmt::atof`]), `snprintf`-shaped output, and the wire
conventions each key speaks. Two of those are load-bearing and easy to get
backwards, so they are stated where they are used rather than here.
*/

use crate::edit::Edit;
use crate::envelope::{Curve, Stage};
use crate::fmt::{self, Buf};
use crate::mask::Mask;
use crate::{rates, Instance, TimeMode, DEPTH_FULL, MAX_STEPS, STAGE_MAX_PCT};
use core::fmt::Write;

#[inline]
/// The `randomize` values that roll nothing -- the Move's enum knob turned back
/// to rest. Public because a shell that seeds a roll on the posting side has to
/// tell a roll from a hold exactly as the engine does.
pub fn randomize_holds(val: &str) -> bool {
    matches!(val, "Hold" | "hold" | "0" | "Off" | "off")
}

fn clampf(x: f32, lo: f32, hi: f32) -> f32 {
    if x < lo { lo } else if x > hi { hi } else { x }
}

/*
 * HEX IS LSB-ALIGNED, AND THAT IS WHAT KEEPS OLD PATCHES READABLE.
 *
 * The mask used to be one u32 printed with %X, so "5555" meant steps
 * 0,2,4,... Reading right-to-left into word 0 first gives a 128-bit mask the
 * same meaning, so a v3 blob -- which never has more than 8 hex digits --
 * lands exactly where it did. And emitting the minimal form writes "5555"
 * again for any pattern inside 32 steps, so a short pattern's state is
 * byte-identical to what the previous version wrote.
 */
pub fn set_pattern_hex(dst: &mut Mask, val: &str) {
    dst.clear();
    /* Walk from the END of the string: the last character is the low nibble. */
    let b = val.as_bytes();
    let mut bit = 0usize;
    for &c in b.iter().rev() {
        if bit >= MAX_STEPS {
            break;
        }
        let v = match c {
            b'0'..=b'9' => (c - b'0') as u32,
            b'a'..=b'f' => (c - b'a') as u32 + 10,
            b'A'..=b'F' => (c - b'A') as u32 + 10,
            _ => continue, /* skip whitespace, 0x, junk */
        };
        for k in 0..4 {
            if bit >= MAX_STEPS {
                break;
            }
            if (v >> k) & 1 != 0 {
                dst.set(bit, true);
            }
            bit += 1;
        }
    }
}

/*
 * THE AUTOMATABLE PARAMETERS, BY NUMBER.
 *
 * [`Instance::set_param`] is the canonical door and takes strings, which is
 * right for a patch, a pattern or a pad edit -- all of them message-thread
 * work. It is wrong for HOST AUTOMATION, which arrives on the audio thread: a
 * float formatted and parsed back costs a locale-dependent conversion in each
 * direction (C's `atof` honours `LC_NUMERIC`, so a comma-decimal host turns
 * "0.750" into 0) and a string-match ladder, per value, per block.
 *
 * These are the same fifteen values on the same wire conventions -- slot,
 * length and rate are INDICES, legato and time_mode are 0|1, the rest are the
 * units the string keys use -- with the decimal detour removed. `set_param`
 * is implemented in terms of [`Instance::set_num`], so every clamp exists
 * once.
 *
 * THE DISCRIMINANTS ARE THE C ABI. `tg_param_t` is this enum's order, and a
 * host that saved an automation lane saved these numbers, so inserting one in
 * the middle silently rewires a user's project.
 */
#[repr(i32)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Param {
    Slot = 0,
    Length,
    Rate,
    Legato,
    TimeMode,
    Curve,
    Amount,
    Hold,
    Attack,
    Decay,
    Sustain,
    Release,
    /* APPENDED, AS EVERYTHING AFTER THE FIRST RELEASE MUST BE. The
     * discriminants ARE the ABI -- a host stores the index in a project -- so
     * the two below can only go on the end. */
    Fade,
    FadeSoft,
    FadeDir,
}

impl Param {
    /// The C side passes an `int`. Anything outside the enum is dropped
    /// rather than clamped onto a neighbour: a wrong parameter silently
    /// moving a different control is worse than one doing nothing.
    pub fn from_i32(v: i32) -> Option<Param> {
        use Param::*;
        Some(match v {
            0 => Slot,
            1 => Length,
            2 => Rate,
            3 => Legato,
            4 => TimeMode,
            5 => Curve,
            6 => Amount,
            7 => Hold,
            8 => Attack,
            9 => Decay,
            10 => Sustain,
            11 => Release,
            12 => Fade,
            13 => FadeSoft,
            14 => FadeDir,
            _ => return None,
        })
    }
}

impl Instance {
    /// The fifteen automatable values, by number. Every clamp and every side
    /// effect lives here; [`Instance::set_param`] parses a string and
    /// delegates, so the two doors cannot drift apart.
    ///
    /// Audio-thread safe: a match, a clamp and a store. No allocation, no
    /// formatting, no locale.
    pub fn set_num(&mut self, param: Param, value: f64) {
        let before = self.saved_scalars();
        self.set_num_inner(param, value);
        if self.saved_scalars() != before {
            self.rev = self.rev.wrapping_add(1);
        }
    }

    /*
     * EVERY SCALAR THE STATE BLOB CARRIES THAT A NUMBER CAN MOVE: the slot, and
     * the current slot's sound and length. Compared around `set_num` so the
     * revision moves only for a real change; a plugin pushes all fifteen every
     * block, and nearly all of those change nothing. Only the current slot can
     * be edited through here, and a switch changes `slot` itself.
     */
    fn saved_scalars(&self) -> (usize, usize, [u32; 13]) {
        (self.slot, self.pat[self.slot].length, self.snd().bits())
    }

    fn set_num_inner(&mut self, param: Param, value: f64) {
        /* A NaN from a host is not a value. Clamping would propagate it
         * (NaN.clamp is NaN) and `as i32` would turn it into 0 -- a different
         * slot or rate -- so it is dropped at the door. sc-core does the same. */
        if value.is_nan() {
            return;
        }
        /*
         * `as i32` SATURATES IN RUST WHERE C'S CAST IS UNDEFINED. For an
         * out-of-range double C commonly lands on INT_MIN, which every branch
         * below treats as out of range and rejects -- and saturation lands on
         * i32::MAX or i32::MIN, which they reject identically. Same outcome,
         * one of them defined.
         */
        match param {
            Param::Slot => {
                let s = value as i32; /* the wire is the OPTION INDEX */
                /* The slot it already is changes nothing -- and PushParams
                 * writes it every block, so the weights are not recomputed
                 * for it. */
                if s >= 0 && (s as usize) < crate::SLOTS && s as usize != self.slot {
                    /* A switch recalls the slot's whole sound: see `sound`. */
                    self.switch_slot(s as usize);
                }
            }
            Param::Length => {
                /* Option INDEX, as for `cursor`: index 15 is the option named
                 * "16", which is a length of 16. */
                let n = (value as i64 + 1).clamp(1, MAX_STEPS as i64) as usize;
                let slot = self.slot;
                /*
                 * THE LENGTH IT ALREADY HAS IS NOT A CHANGE, and PushParams
                 * writes Length every block. Renumbering is O(length^2) -- ~19 us
                 * of every block at 128 steps -- and every other door leaves
                 * the order normalised already (see `Pattern::renumber`), so
                 * doing it again produced the table it started from.
                 */
                if n == self.pat[slot].length {
                    return;
                }
                self.pat[slot].length = n;
                if self.cursor >= self.pat[slot].length {
                    self.cursor = self.pat[slot].length - 1;
                }
                /* The length decides how many steps the fade counts, so both
                 * the ranks and the weights move with it. */
                self.pat[slot].renumber();
                self.recalc_fade();
            }
            Param::Rate => {
                /* OUT OF RANGE IS THE DEFAULT, NOT THE NEAREST END --
                 * `rates::index_from` has answered that way since indices
                 * were first accepted, and an old state blob may carry one.
                 * Clamping here instead would have been a second convention
                 * for the same wire, differing only in the case nobody looks
                 * at. A host parameter is a 13-way choice and never sends
                 * anything else, so this is about the patch door, not the
                 * knob. */
                let i = value as i32;
                self.snd_mut().rate_idx = if i >= 0 && (i as usize) < rates::RATES.len() {
                    i as usize
                } else {
                    rates::RATE_DEFAULT
                };
                /* The `ui` readout carries the step DURATION, which the
                 * plugin draws its envelope against -- so it follows the rate
                 * now, not at the next block, or an idle host shows the old
                 * subdivision's length. */
                self.recalc_ms_per_step();
            }
            Param::Legato => self.snd_mut().legato = value != 0.0,
            Param::TimeMode => {
                self.snd_mut().time_mode = if value != 0.0 { TimeMode::Pct } else { TimeMode::Ms }
            }
            Param::Curve => {
                let c = value as i32;
                let c = Curve::from_i32(if (0..=2).contains(&c) { c } else { 0 });

                /* Re-anchored, so a mid-gate change keeps the level: see
                 * `Instance::reanchor`. */
                let from = self.snd().curve;
                self.snd_mut().curve = c;
                self.reanchor(from);
            }
            Param::Amount => self.snd_mut().amount = clampf(value as f32, 0.0, 1.0),
            Param::Hold => self.snd_mut().hold = clampf(value as f32, 0.0, 1.0),
            Param::Sustain => self.snd_mut().sustain = clampf(value as f32, 0.0, 1.0),
            Param::Attack => self.snd_mut().attack = clampf(value as f32, 0.0, STAGE_MAX_PCT),
            Param::Decay => self.snd_mut().decay = clampf(value as f32, 0.0, STAGE_MAX_PCT),
            Param::Release => self.snd_mut().release = clampf(value as f32, 0.0, STAGE_MAX_PCT),
            /*
             * COMPARED BEFORE RECOMPUTED, AND THAT IS NOT AN OPTIMISATION.
             *
             * The plugin's `PushParams` writes every parameter at the top of
             * every block, so an unconditional `recalc_fade()` here would walk
             * the pattern twice a block for the whole life of the instance --
             * on the audio thread, to produce the table it already had.
             */
            Param::Fade => {
                let v = clampf(value as f32, 0.0, 1.0);
                if v != self.snd().fade {
                    self.snd_mut().fade = v;
                    self.recalc_fade();
                }
            }
            Param::FadeSoft => {
                let v = value != 0.0;
                if v != self.snd().fade_soft {
                    self.snd_mut().fade_soft = v;
                    self.recalc_fade();
                }
            }
            /* Compared before recomputed, like the two above and for the same
             * reason: PushParams writes every parameter every block. */
            Param::FadeDir => {
                let v = crate::FadeDir::from_i32(value as i32);
                if v != self.snd().fade_dir {
                    self.snd_mut().fade_dir = v;
                    self.recalc_fade();
                }
            }
        }
    }

    /// The string door: [`Edit::parse`] and [`Instance::apply_edit`] in one
    /// call, on whatever thread calls it -- the Move's audio callback, which
    /// has no other. A key that is not an edit changes nothing.
    pub fn set_param(&mut self, key: &str, val: &str) {
        match key {
            /* A whole patch has a reader of its own, so this is the one key
             * that is not an `Edit`: parse and load in one -- see the state
             * module on where parsing runs. A text that is no blob loads
             * nothing. */
            "state" => {
                if let Ok(patch) = crate::state::Patch::parse(val) {
                    self.load(&patch);
                }
            }
            _ => {
                if let Some(edit) = Edit::parse(key, val) {
                    self.apply_edit(&edit);
                }
            }
        }
    }

    /// Returns the length written, or -1 for a key this engine does not serve
    /// -- which is how a shell knows to answer its own.
    pub fn get_param(&self, key: &str, out: &mut [u8]) -> i32 {
        let mut b = Buf::new(out);
        let p = self.pattern();
        let s = self.snd();
        let _ = match key {
            "name" => write!(b, "TRANCE GATE"),
            "slot" => write!(b, "{}", self.slot),
            "length" => write!(b, "{}", p.length - 1),
            "rate" => write!(b, "{}", rates::RATES[s.rate_idx].label),
            "attack" => fmt::f(&mut b, s.attack as f64, 1),
            "decay" => fmt::f(&mut b, s.decay as f64, 1),
            "sustain" => fmt::f(&mut b, s.sustain as f64, 2),
            "release" => fmt::f(&mut b, s.release as f64, 1),
            "hold" => fmt::f(&mut b, s.hold as f64, 2),
            "amount" => fmt::f(&mut b, s.amount as f64, 2),
            "legato" => write!(b, "{}", s.legato as i32),
            "fade" => fmt::f(&mut b, s.fade as f64, 2),
            "fade_soft" => write!(b, "{}", s.fade_soft as i32),
            "fade_dir" => write!(b, "{}", s.fade_dir as i32),
            /* The step's place in the arrival order, at the cursor -- among its
             * OWN KIND, because that is what the fade ranks. An off step has a
             * rank now: it is the order the holes arrive in under Fade Out. */
            "step_order" => write!(b, "{}", p.order[self.cursor]),
            "time_mode" => write!(b, "{}", s.time_mode as i32),
            "curve" => write!(b, "{}", s.curve as i32),
            /* The step's length in ms, so a shell can show what a % actually
             * costs without duplicating the rate table. */
            "ms_per_step" => fmt::f(&mut b, self.ms_per_step as f64, 2),
            /* How long the gate is open for. A shell showing a stage in
             * MILLISECONDS needs this and the percentage:
             * ms = value/100 * width_ms. Served rather than left to the shell
             * to recompute, so the rate table and the hold clamp stay in one
             * place. */
            "width_ms" => fmt::f(&mut b, self.width_ms() as f32 as f64, 2),
            "cursor" => write!(b, "{}", self.cursor),
            "step" => {
                let w = if !p.steps.get(self.cursor) {
                    "Off"
                } else if p.ties.get(self.cursor) {
                    "Tie"
                } else {
                    "On"
                };
                write!(b, "{}", w)
            }
            "step_amount" => fmt::f(&mut b, (p.depth[self.cursor] as f32 * (1.0 / 255.0)) as f64, 2),
            "pattern" => p.steps.to_hex(&mut b),
            "ties" => p.ties.to_hex(&mut b),
            /* The live playhead. Declared "live", so the host re-reads
             * `phase:effective` every tick instead of once per value rotation
             * -- the difference between an animated ring and a slideshow. */
            "phase" | "phase:effective" => {
                let length = p.length.max(1) as f64;
                let mut pos = self.phase.pos % length;
                if pos < 0.0 {
                    pos += length;
                }
                fmt::f(&mut b, pos, 3)
            }
            /*
             * ONE READ FOR THE AUTOMATABLE VALUES.
             *
             * `ui` carries the pattern and the playhead; it carries no part
             * of the SOUND, which is why a shell that wants to know whether
             * its picture is stale has to ask for nine keys one at a time --
             * nine locks and nine buffers, thirty times a second, to answer
             * "did anything move".
             *
             * This is that set in one line: exactly the values with a host
             * parameter behind them, in the order the plugin declares them,
             * plus `width_ms` because a shell showing a stage in milliseconds
             * needs it to convert and would otherwise take a tenth lock to
             * get it.
             *
             *   slot:legato:time_mode:curve:rate:length:amount:hold:attack:
             *   decay:sustain:release:width_ms:fade:fade_soft:fade_dir:detents
             *
             * FADE AND FADE_SOFT ARE APPENDED, past `width_ms`, because every
             * reader of this string indexes it. Adding them anywhere else --
             * beside `amount`, where they belong by meaning -- would move ten
             * fields under two shells and a test that counts them.
             *
             * FLOATS ARE %.9g, WHICH IS NOT COSMETIC. Nine significant digits
             * is FLT_DECIMAL_DIG -- the shortest precision for which
             * float -> decimal -> float is the identity. The single-key
             * getters round to %.1f and %.2f, so a shell that reads a value
             * and writes it back quantises the patch every time it does so.
             * Anything that round-trips through this readout must come back
             * bit-identical, or the caller needs a suppression flag and every
             * suppression flag eventually drops something real.
             *
             * `length` is the OPTION INDEX and `rate` is the LABEL, both
             * exactly as the single-key getters answer them -- one convention
             * per key, not two. attack/decay/release are PERCENTAGES OF
             * WIDTH, like everywhere else.
             *
             * `detents` is APPENDED, for the same reason: the pattern lengths,
             * comma-separated and ascending, that are half a bar, one, two or
             * four at this slot's Rate and the host's meter (`rates::detents`)
             * -- "16,32,64,128" at 1/32 in 4/4, empty when none is whole. The
             * editor's Length control holds on them; it is told them rather
             * than working them out, so the rate table stays the only thing
             * that knows what a step is worth.
             */
            "params" => {
                let r = write!(
                    b,
                    "{}:{}:{}:{}:{}:{}:",
                    self.slot,
                    s.legato as i32,
                    s.time_mode as i32,
                    s.curve as i32,
                    rates::RATES[s.rate_idx].label,
                    p.length - 1
                );
                r.and_then(|_| {
                    for v in [s.amount, s.hold, s.attack, s.decay, s.sustain, s.release] {
                        fmt::g(&mut b, v as f64, 9)?;
                        b.write_char(':')?;
                    }
                    fmt::g(&mut b, self.width_ms(), 9)?;
                    b.write_char(':')?;
                    fmt::g(&mut b, s.fade as f64, 9)?;
                    write!(b, ":{}:{}:", s.fade_soft as i32, s.fade_dir as i32)?;
                    let d = rates::detents(s.rate_idx, self.meter, MAX_STEPS);
                    for (i, steps) in d.as_slice().iter().enumerate() {
                        if i > 0 {
                            b.write_char(',')?;
                        }
                        write!(b, "{steps}")?;
                    }
                    Ok(())
                })
            }
            "ui" => return self.ui_readout(b),
            "state" => return crate::state::save(self, b),
            _ => return -1,
        };
        b.finish()
    }

    /*
     * ONE READ FOR THE WHOLE PICTURE.
     *
     * The animated page needs six facts and a param read is ~2.8 ms, so
     * asking for them separately would cost six rotation stops -- and an
     * `extra_keys` value is refreshed on the SLOW rotation only, so six keys
     * would be six times slower than one, not merely six reads.
     *
     *   steps : ties : length : phase : ms_step : advancing : cursor : depths
     *     : orders : slot
     *
     * `orders` is APPENDED for the same reason the params readout's are: every
     * field here is read by index, on both shells. Two hex digits per step like
     * `depths`, and EVERY step carries one -- a rank among its own kind, because
     * Fade Out ranks the holes. Which set a shell should draw follows from the
     * direction, which it has from the params readout.
     *
     * `slot` is appended last, for the same reason. A slot switch changes every
     * other value a shell shows, and on the Move the knob grid caches them: the
     * module's editor watches this field and re-reads the grid when it moves,
     * whoever moved it -- the Slot knob, the arrows, a preset.
     */
    fn ui_readout(&self, mut b: Buf) -> i32 {
        let p = self.pattern();
        let length = p.length.max(1);
        let mut pos = self.phase.pos % length as f64;
        if pos < 0.0 {
            pos += length as f64;
        }
        let _ = p.steps.to_hex(&mut b);
        let _ = write!(b, ":");
        let _ = p.ties.to_hex(&mut b);
        let _ = write!(b, ":{}:", length);
        let _ = fmt::f(&mut b, pos, 3);
        let _ = write!(b, ":");
        let _ = fmt::f(&mut b, self.ms_per_step as f64, 2);
        let _ = write!(b, ":{}:{}:", self.advancing as i32, self.cursor);
        /* Per-step depths as a run of two hex digits each -- one field rather
         * than many, because the page already pays for this string once per
         * rotation stop and a second read would halve the anchor rate. */
        for i in 0..length {
            let _ = write!(b, "{:02X}", p.depth[i]);
        }
        let _ = write!(b, ":");
        for i in 0..length {
            let _ = write!(b, "{:02X}", p.order[i]);
        }
        let _ = write!(b, ":{}", self.slot);
        b.finish()
    }

    /// The envelope's stage, for a shell that wants to know whether a gate is
    /// live without inferring it from the level.
    pub fn stage(&self) -> Stage {
        self.env.stage
    }

    /// Every step of `slot` back to full level. A slot out of range does
    /// nothing, as it does at every other door that takes one.
    pub fn reset_depths(&mut self, slot: usize) {
        if let Some(p) = self.pat.get_mut(slot) {
            p.depth = [DEPTH_FULL; MAX_STEPS];
        }
    }
}

#[cfg(test)]
mod tests;
