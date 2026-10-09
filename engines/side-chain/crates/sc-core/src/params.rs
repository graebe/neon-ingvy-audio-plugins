// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The parameters: two doors onto one set of clamps, and the readouts.

TWO DOORS, AND WHY BOTH.

`set_num(Param, f64)` is for host automation. It runs on the audio callback and
it takes a number, so there is no parsing and no locale.

`set_param(&str, &str)` is for everything that speaks text -- a Schwung
`chain_params` shell, a typed value, a saved state. It exists as a SEPARATE
door rather than the only one because `atof` honours `LC_NUMERIC`: in a host
running under a comma-decimal locale, `"0.750"` parses as `0`. That is not a
hypothetical; it is why `trance_gate_core.h:116-132` argues the same case.

`set_param` IS IMPLEMENTED VIA `set_num`, so every clamp exists exactly once.
The string door's only extra job is deciding which number a word means.

EACH KEY IS SPELLED ONCE, in `Param::key`. `set_param` and `get_param` find a
parameter by its key through `Param::from_key`, which reads the spellings off
`key` rather than keeping a second copy of them.

WHY THE STRING DOOR IS NOT SERDE. The words are C's, and saved patches and the
Move's knob grid already speak them: `atof`'s leniency (`"4abc"` is 4, `"abc"`
is 0), an enum as its label OR its index, a note as `F#3` or as `66`.
`serde_json` refuses `"4abc"`, `".5"` and `"+40"` outright, so a `Deserialize`
would have to be taught each rule by hand -- the parsing would move into
visitors, not go away. And it would allocate on the audio callback: a derived
enum reports a word that is not a variant name by formatting an error, three
allocations for the `"2"` a knob sends, because in label-or-index every index
is such a word first. The wire tests in `params/tests.rs` hold the bytes
either way.

WHAT `get_param` MUST NOT DO. It runs on the audio callback too, so it does not
allocate and it does not compute. Everything it reports that costs arithmetic --
`ms_per_cycle`, the detector level -- is published by `block_setup`, once per
block, and read here.
*/

use crate::fmt::{self, Buf};
use crate::shape::{Curve, Stage};
use crate::{rates, Instance, Source, TimeMode, DELAY_RANGE_PCT, STAGE_MAX_PCT};
use core::fmt::Write;

/// The automatable parameters, in the order the shell declares them so a host
/// index IS an engine index.
///
/// APPEND ONLY. The numeric form is what a host stores in a session and what a
/// saved state may carry, so inserting a parameter re-points every project
/// that used the build before it.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
#[repr(i32)]
pub enum Param {
    Source = 0,
    Rate,
    TimeMode,
    Delay,
    Attack,
    Hold,
    Release,
    Depth,
    Curve,
    Channel,
    Note,
    MidiMode,
    VelSens,
    Threshold,
    Lockout,
}

pub const PARAM_COUNT: i32 = 15;

/// The first field of a `state` blob: the format and its version. A change
/// that an older build would misread takes a new tag, never a new meaning for
/// this one.
pub const STATE_TAG: &str = "sc1";

impl Param {
    /// Every parameter, in index order: `ALL[i] as i32 == i`, which a test
    /// holds it to.
    pub const ALL: [Param; PARAM_COUNT as usize] = {
        use Param::*;
        [
            Source, Rate, TimeMode, Delay, Attack, Hold, Release, Depth, Curve, Channel, Note,
            MidiMode, VelSens, Threshold, Lockout,
        ]
    };

    pub fn from_i32(v: i32) -> Option<Param> {
        usize::try_from(v).ok().and_then(|i| Param::ALL.get(i)).copied()
    }

    /// The parameter `key` names on the wire, if any. A scan of fifteen
    /// `&'static str` comparisons: no table to keep in step with `key`, and
    /// nothing allocated, because `set_param` and `get_param` call it on the
    /// audio callback.
    pub fn from_key(key: &str) -> Option<Param> {
        Param::ALL.into_iter().find(|p| p.key() == key)
    }

    /// The wire key, which is also the `chain_params` key. One spelling.
    pub fn key(self) -> &'static str {
        use Param::*;
        match self {
            Source => "source",
            Rate => "rate",
            TimeMode => "time_mode",
            Delay => "delay",
            Attack => "attack",
            Hold => "hold",
            Release => "release",
            Depth => "depth",
            Curve => "curve",
            Channel => "channel",
            Note => "trigger_note",
            MidiMode => "midi_mode",
            VelSens => "vel_sens",
            Threshold => "threshold",
            Lockout => "lockout",
        }
    }
}

/// dB to linear amplitude. `-inf` dB is silence, and the floor is the
/// threshold parameter's own minimum so the two agree.
pub fn db_to_amp(db: f64) -> f64 {
    if db <= THRESHOLD_MIN_DB {
        /* The bottom of the range means "trigger on anything", so it must be
         * BELOW any real signal rather than exactly at -60 dB. */
        return 0.0;
    }
    (10.0f64).powf(db / 20.0)
}

/// Linear amplitude back to dB, floored. The inverse of `db_to_amp` on the
/// open interval; the UI prints this, so it must not return `-inf` and make a
/// readout say "-inf dB".
#[allow(clippy::neg_cmp_op_on_partial_ord, reason = "a NaN amplitude must take the floor, not reach log10")]
pub fn amp_to_db(a: f64) -> f64 {
    if !(a > 0.0) {
        return THRESHOLD_MIN_DB;
    }
    let db = 20.0 * a.log10();
    if db < THRESHOLD_MIN_DB {
        THRESHOLD_MIN_DB
    } else {
        db
    }
}

pub const THRESHOLD_MIN_DB: f64 = -60.0;
pub const THRESHOLD_MAX_DB: f64 = 0.0;
pub const LOCKOUT_MAX_MS: f64 = 200.0;

impl Instance {
    /// The audio-thread door. Out-of-range values are clamped, never rejected:
    /// a host is allowed to send a normalised value that rounds outside the
    /// range, and refusing it would freeze the parameter instead of moving it.
    pub fn set_num(&mut self, p: Param, v: f64) {
        /* A NaN from a host is not a value. Clamping it would propagate it
         * (NaN.clamp is NaN), so it is dropped at the door. */
        if v.is_nan() {
            return;
        }
        use Param::*;
        match p {
            Source => {
                let s = crate::Source::from_i32(v as i32);
                if s != self.source {
                    self.source = s;
                    /* Switching source abandons whatever the old one was
                     * doing: a held MIDI note does not keep a cycle duck open,
                     * and the detector must not fire on a stale edge. */
                    self.env.reset();
                    self.follower.reset();
                    self.midi.held = 0;
                }
            }
            Rate => {
                let i = v as i64;
                if i >= 0 && (i as usize) < rates::RATES.len() {
                    self.rate_idx = i as usize;
                }
            }
            TimeMode => self.time_mode = crate::TimeMode::from_i32(v as i32),
            Delay => self.delay_pct = v.clamp(-DELAY_RANGE_PCT, DELAY_RANGE_PCT),
            Attack => self.attack_pct = v.clamp(0.0, STAGE_MAX_PCT),
            Hold => self.hold_pct = v.clamp(0.0, STAGE_MAX_PCT),
            Release => self.release_pct = v.clamp(0.0, STAGE_MAX_PCT),
            Depth => self.depth = v.clamp(0.0, 1.0),
            Curve => self.set_curve(crate::shape::Curve::from_i32(v as i32)),
            Channel => self.midi.channel = (v as i32).clamp(0, 16),
            Note => self.midi.note = (v as i32).clamp(0, 127),
            MidiMode => self.midi.gate = v as i32 != 0,
            VelSens => self.midi.vel_sens = v.clamp(0.0, 1.0),
            Threshold => {
                self.threshold = db_to_amp(v.clamp(THRESHOLD_MIN_DB, THRESHOLD_MAX_DB))
            }
            Lockout => self.lockout_ms = v.clamp(0.0, LOCKOUT_MAX_MS),
        }
    }

    /// Change the curve WITHOUT a click.
    ///
    /// The stage's elapsed position is a time, and the level it corresponds to
    /// depends on the curve. Swapping the curve and leaving `pos` alone jumps
    /// the gain to wherever the new curve happens to be at that time -- audible
    /// as a click, mid-duck, every time the control is touched.
    ///
    /// So the level is held fixed and the TIME is moved: invert the old shape
    /// to find where we are, then invert the new one to find where that level
    /// lives on it. `shape_inv` is analytic for all three curves, which is why
    /// they were all chosen to be invertible.
    #[allow(clippy::neg_cmp_op_on_partial_ord, reason = "a NaN span, level or position must take its guard, not reach a division")]
    pub fn set_curve(&mut self, curve: Curve) {
        if curve == self.curve {
            return;
        }
        let old = self.curve;
        self.curve = curve;

        /* Only the two moving stages have a position worth preserving. Delay
         * and Hold are flat, and Idle has nothing. */
        let (len, w) = match self.env.stage {
            Stage::Attack => {
                let span = self.env.scale - self.env.from;
                if !(span.abs() > 0.0) {
                    return;
                }
                (
                    self.env.pos,
                    ((self.env.duck - self.env.from) / span).clamp(0.0, 1.0),
                )
            }
            Stage::Release => {
                if !(self.env.from > 0.0) {
                    return;
                }
                (
                    self.env.pos,
                    (1.0 - self.env.duck / self.env.from).clamp(0.0, 1.0),
                )
            }
            _ => return,
        };

        /* `pos` and the stage length are proportional, so the stage length
         * cancels: t_old = pos / len, and the new pos is t_new * len. Recover
         * len from the old t rather than recomputing it, because the caller is
         * not necessarily inside a block and may not know the cycle length. */
        let t_old = crate::shape::shape_inv(old, w);
        if !(t_old > 0.0) {
            return;
        }
        let stage_len = len / t_old;
        let t_new = crate::shape::shape_inv(curve, w);
        self.env.pos = t_new * stage_len;
    }

    /// Load a `state` blob, as [`get_param`](Self::get_param) writes it:
    ///
    /// ```text
    /// sc1;source=0;rate=4;time_mode=0;delay=0;attack=2;...;lockout=0
    /// ```
    ///
    /// Every value goes through [`set_num`](Self::set_num) -- the inverse of
    /// [`num`](Self::num), so the blob round-trips exactly and every clamp
    /// exists once. Keyed rather than positional, so a key this build does not
    /// know is passed over and one the blob lacks keeps what the engine held:
    /// a newer build's blob loads here, and this one loads in a newer build.
    /// A text without the tag is no blob and loads nothing.
    ///
    /// Schwung calls this on its audio callback, which is the Move's only
    /// thread, so it allocates nothing: `split` and `parse::<f64>` are both
    /// allocation-free (and locale-free, unlike `atof`).
    pub fn load_state(&mut self, text: &str) {
        let Some(body) = text.strip_prefix(STATE_TAG) else {
            return;
        };
        for field in body.split(';') {
            let Some((k, v)) = field.split_once('=') else {
                continue;
            };
            if let (Some(p), Ok(n)) = (Param::from_key(k), v.parse::<f64>()) {
                self.set_num(p, n);
            }
        }
    }

    /// The string door. Returns false for a key it does not own, so a shell
    /// can chain its own keys after these.
    pub fn set_param(&mut self, key: &str, val: &str) -> bool {
        /* Not a parameter: a host panic arriving through the only door a
         * Schwung module has for one. See midi.rs. */
        if key == "panic" {
            self.reset();
            return true;
        }
        if key == "state" {
            self.load_state(val);
            return true;
        }
        let Some(p) = Param::from_key(key) else {
            return false;
        };
        /* The enums take a WORD or an index, because both arrive: the host
         * sends an index, a saved state or a hand-written patch sends the
         * label. `index_from` and `label_or_index` resolve either. Every
         * parameter is named, so a new one cannot arrive without a rule for
         * reading its text. */
        let v = match p {
            Param::Source => label_or_index(&Source::LABELS, val),
            Param::Rate => rates::index_from(val) as f64,
            Param::TimeMode => label_or_index(&TimeMode::LABELS, val),
            Param::Curve => label_or_index(&Curve::LABELS, val),
            Param::MidiMode => label_or_index(&["Trigger", "Gate"], val),
            Param::Channel => {
                if val == "Omni" {
                    0.0
                } else {
                    fmt::atoi(val) as f64
                }
            }
            Param::Note => {
                crate::midi::note_from_name(val).map_or_else(|| fmt::atof(val), f64::from)
            }
            Param::Delay
            | Param::Attack
            | Param::Hold
            | Param::Release
            | Param::Depth
            | Param::VelSens
            | Param::Threshold
            | Param::Lockout => fmt::atof(val),
        };
        self.set_num(p, v);
        true
    }

    /// The numeric value of a parameter, in the unit `set_num` takes. The
    /// inverse of `set_num` for every parameter, which is what lets a state
    /// round-trip through text without the engine owning a second table.
    pub fn num(&self, p: Param) -> f64 {
        use Param::*;
        match p {
            Source => self.source as i32 as f64,
            Rate => self.rate_idx as f64,
            TimeMode => self.time_mode as i32 as f64,
            Delay => self.delay_pct,
            Attack => self.attack_pct,
            Hold => self.hold_pct,
            Release => self.release_pct,
            Depth => self.depth,
            Curve => self.curve as i32 as f64,
            Channel => self.midi.channel as f64,
            Note => self.midi.note as f64,
            MidiMode => self.midi.gate as i32 as f64,
            VelSens => self.midi.vel_sens,
            Threshold => amp_to_db(self.threshold),
            Lockout => self.lockout_ms,
        }
    }

    /// A stage's length in milliseconds, which is what `Time Mode` = ms shows.
    /// Zero before the first block, because the cycle length is not known
    /// until a tempo is.
    pub fn stage_ms(&self, p: Param) -> f64 {
        self.num(p) * self.ms_per_cycle as f64 / 100.0
    }

    pub fn get_param(&self, key: &str, out: &mut [u8]) -> i32 {
        let mut b = Buf::new(out);
        let ok = match key {
            /*
             * ONE READ FOR THE WHOLE ANIMATED PICTURE, pushed once per frame.
             *
             * Everything here changes on its own; everything that only changes
             * when a control moves is in "params" instead. Splitting them is
             * what keeps the per-frame payload small enough not to matter.
             *
             * source:rate:ms_cycle:sweep:advancing:fires:duck:key:connected:stage:phase
             *
             * `sweep` before `phase` because `sweep` is the one the drawing
             * uses -- it is the shared axis, defined for all three sources.
             * `phase` is the transport's own and is only meaningful on Cycle;
             * it is carried so the editor can show a bar position.
             */
            "ui" => {
                let r = write!(
                    b,
                    "{}:{}:",
                    self.source as i32,
                    self.rate_idx
                );
                r.and_then(|_| fmt::f(&mut b, self.ms_per_cycle as f64, 3))
                    .and_then(|_| write!(b, ":"))
                    .and_then(|_| fmt::f(&mut b, self.sweep01(), 6))
                    .and_then(|_| {
                        write!(
                            b,
                            ":{}:{}:",
                            self.advancing as i32,
                            self.fires
                        )
                    })
                    .and_then(|_| fmt::f(&mut b, self.duck_now as f64, 4))
                    .and_then(|_| write!(b, ":"))
                    .and_then(|_| fmt::f(&mut b, self.key_level(), 5))
                    .and_then(|_| {
                        write!(
                            b,
                            ":{}:{}:",
                            self.key_connected as i32,
                            stage_index(self.env.stage)
                        )
                    })
                    .and_then(|_| fmt::f(&mut b, self.phase01(), 6))
            }
            /*
             * EVERY AUTOMATABLE VALUE, in `Param` order, colon separated.
             *
             * The floats go out at full round-trip precision -- Rust's `{}`
             * for f64 emits the shortest decimal that reads back as the same
             * number, which is what `%.17g` is reaching for and what `%.3f`
             * silently is not. A shell that round-trips a value must not
             * quantise the state doing it.
             */
            "params" => {
                let mut r = Ok(());
                for i in 0..PARAM_COUNT {
                    let Some(p) = Param::from_i32(i) else { continue };
                    if i > 0 {
                        r = r.and_then(|_| write!(b, ":"));
                    }
                    r = r.and_then(|_| write!(b, "{}", self.num(p)));
                }
                r
            }
            /* THE WHOLE PATCH, for a host that saves one opaque blob per
             * module -- Schwung's autosave and set files. See `load_state`. */
            "state" => {
                let mut r = write!(b, "{STATE_TAG}");
                for p in Param::ALL {
                    r = r.and_then(|_| write!(b, ";{}={}", p.key(), self.num(p)));
                }
                r
            }
            /* The four stage lengths in ms, for the editor's axis. Derived, so
             * it is one read rather than four multiplications in JS against a
             * cycle length the UI would have to be told separately. */
            "stage_ms" => fmt::f(&mut b, self.stage_ms(Param::Delay), 3)
                .and_then(|_| write!(b, ":"))
                .and_then(|_| fmt::f(&mut b, self.stage_ms(Param::Attack), 3))
                .and_then(|_| write!(b, ":"))
                .and_then(|_| fmt::f(&mut b, self.stage_ms(Param::Hold), 3))
                .and_then(|_| write!(b, ":"))
                .and_then(|_| fmt::f(&mut b, self.stage_ms(Param::Release), 3)),

            "phase" => fmt::f(&mut b, self.phase01(), 6),
            "sweep" => fmt::f(&mut b, self.sweep01(), 6),
            "ms_per_cycle" => fmt::f(&mut b, self.ms_per_cycle as f64, 3),
            "fires" => write!(b, "{}", self.fires),
            "duck" => fmt::f(&mut b, self.duck_now as f64, 4),
            "key_level" => fmt::f(&mut b, self.key_level(), 5),
            "advancing" => write!(b, "{}", self.advancing as i32),
            /* A diagnostic, for the same reason `spectro_dropped()` is one. */
            "dropped" => write!(b, "{}", self.dropped()),

            /* The labels, so a shell never re-spells a table the engine owns. */
            "rate_label" => write!(b, "{}", rates::RATES[self.rate_idx].label),
            "curve_label" => write!(b, "{}", Curve::LABELS[self.curve as usize]),
            "source_label" => write!(b, "{}", Source::LABELS[self.source as usize]),

            _ => match Param::from_key(key) {
                Some(p) => write!(b, "{}", self.num(p)),
                None => return -1,
            },
        };
        if ok.is_err() {
            /* Truncation is not an error the caller can act on -- `Buf` has
             * snprintf semantics and reports what WOULD have fit -- but a
             * genuine format failure is, and it must not look like content. */
            return -1;
        }
        b.finish()
    }
}

/// `Stage` as a small integer for the wire. A separate function rather than a
/// `#[repr(i32)]` on `Stage` because the wire's numbering is a UI contract and
/// the enum's order is an implementation detail; pinning them together would
/// make reordering the machine a silent change to the editor.
fn stage_index(s: Stage) -> i32 {
    match s {
        Stage::Idle => 0,
        Stage::Delay => 1,
        Stage::Attack => 2,
        Stage::Hold => 3,
        Stage::Release => 4,
    }
}

/// The position of `val` among `labels`, or else `val` read as an index with
/// `atoi`'s leniency -- so a word that is neither is 0, as the C read it.
fn label_or_index(labels: &[&str], val: &str) -> f64 {
    labels
        .iter()
        .position(|l| *l == val)
        .map_or_else(|| fmt::atoi(val) as f64, |i| i as f64)
}

#[cfg(test)]
mod tests;
