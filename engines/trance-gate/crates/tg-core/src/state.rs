// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
The `state` blob: one string that carries a whole patch.

```text
text --Patch::parse--> Patch --Instance::load--> the engine
```

[`Patch::parse`] reads the text with serde_json (see `json`) and does all of a
load that is not the engine's own: the version decided, every number typed and
clamped, every slot's pattern and sound decoded. What it returns is ready and
fixed-size, and [`Instance::load`] applies it with a handful of stores and the
recalculations a parameter change already pays for -- no text, no allocation.
Writing goes the other way in [`save`], into the caller's buffer.

# Where the parsing happens

Parsing belongs on a thread that may allocate, and the audio thread is handed
the [`Patch`] -- or a [`SlotFile`](crate::slotfile::SlotFile) or a
[`Clip`](crate::paste::Clip) -- to apply. tests/no_alloc.rs holds the three
applies to it.

- **The plugin** parses on its main thread: tg-capi's shell reads every edit,
  load, paste and import as it is posted, and its command queue carries what
  was read -- an [`Edit`](crate::edit::Edit), a [`Patch`], a
  [`Clip`](crate::paste::Clip) -- for the audio thread to apply. Its view and
  every save are rebuilt from text there too.
- **The Move** has no other thread. Schwung calls `set_param` -- the text
  door -- on its audio callback, so a blob is parsed there, as it always was,
  and whatever Schwung restores: a blob it stored, or one a hand-edit or a
  truncated patch file damaged.

So a blob is read with `json::read_plain`, which never allocates, whatever the
text: a text serde_json would refuse -- and box the error -- or would have to
copy is refused before serde_json sees it. That is the one narrowing of
JSON, and it is a narrowing to what every build writes: a blob whose string
has an escape, or with a value that is an array or an object, is no patch.
A slot file or a paste is read on the plugin's main thread only, with the
plain `json::read`, which may allocate for a text it refuses.

The text doors -- `set_param("state", _)`, [`Instance::import_into`] and
[`Instance::paste_into`] -- are parse-then-apply in one call, for the Move and
for a plugin shell's main thread.

# The format

```text
{"sv":7,"slot":0,"rate":"1/16","attack":1.60,...,"p0":"5555:0:16:",...,"s3":"1/4:..."}
```

A flat object: the version, the current slot, the current slot's sound as
top-level keys, each slot's pattern as `p<N>`, and a slot's own sound as
`s<N>` where it differs. A key the blob lacks keeps what the engine held, or
reads as the value that changes nothing (see [`Version`]); a key this build
does not know is passed over -- the `stopped` a v3 blob carries, or whatever a
newer build adds. A text that is not a JSON object loads nothing at all, rather
than whatever fields could be picked out of it.

The written form is v7's, byte for byte as every build since v7 wrote it:
older builds and the Move read it. Its numbers have fixed decimals (`1.60`,
`1.000`), which serde_json cannot write -- it writes the shortest form -- so
the writer is `core::fmt`, as it was.
*/

use crate::envelope::Curve;
use crate::fmt::{self, Buf};
use crate::json::{self, Scalar, Sink};
use crate::params::set_pattern_hex;
use crate::sound::Sound;
use crate::{rates, FadeDir, Instance, Pattern, TimeMode, DEPTH_FULL, MAX_STEPS, SLOTS, STAGE_MAX_PCT};
use core::fmt::Write;
use std::borrow::Cow;

pub const STATE_VERSION: i32 = 7;
/// The first version in which every slot carries its own sound. A blob older
/// than this holds one sound for all eight.
pub const STATE_VERSION_SLOT_SOUNDS: i32 = 7;

#[inline]
fn clampf(x: f32, lo: f32, hi: f32) -> f32 {
    if x < lo { lo } else if x > hi { hi } else { x }
}

/*
 * THE VERSIONS, AND WHY THERE IS A NUMBER AT ALL.
 *
 * A version field costs nothing and is the only thing that can tell one
 * reading of a number from another. The Ducker shipped without one, and a
 * 0.1.x blob's envelope times -- read as the units the next version
 * introduced -- collapsed the effect silently on a patch that had been
 * working.
 *
 * Most of the migrations below need no number to apply: what a version added
 * is ABSENT-MEANS-INERT, so an older blob, lacking it, reads as the behaviour
 * it was written with. Two readings do turn on the number, and they are the
 * two predicates: whether the stages are milliseconds, and whether a slot's
 * own sound is there to be read.
 */
/// The version a blob was written in, by its `sv`.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Version {
    /// No `sv` at all: the oldest blobs, read as they were written.
    Unversioned,
    /// Patterns are `<steps>:<ties>:<length>`, with no per-step levels. An
    /// ABSENT LEVEL IS FULL, in every version: loading it as zero would
    /// silently mute every gate in every patch that already works.
    V1,
    /// Patterns gained the per-step levels, and the amount was two globals,
    /// `mix` and `depth`. They were one quantity with two names -- only their
    /// PRODUCT ever reached the audio -- so a blob without `amount` reads the
    /// product as its amount, and sounds identical rather than jumping to
    /// full.
    V2,
    /// `depth` folded into `mix`, written as `amount`. Attack, decay and
    /// release are still milliseconds, as in v1 and v2 (see
    /// [`Version::stages_in_ms`]).
    V3,
    /// Attack, decay and release are PERCENTAGES OF WIDTH, not milliseconds.
    /// The keys did not change, so only the version can tell a v3's
    /// `"attack": 2.0` (2 ms) from a v4's (2% of the gate).
    V4,
    /// The fade-in. Patterns gained a per-step ARRIVAL ORDER as a fifth `p<N>`
    /// field, and `fade` / `fsoft` joined the globals. Every one of them is
    /// ABSENT-MEANS-INERT: no order field is position order, no `fade` is 1.0
    /// and no `fsoft` is hard, which together are exactly what a v4 patch did.
    /// So this version number buys nothing today -- and it is here anyway,
    /// because the one thing it can do is tell a later reader that an absent
    /// order field meant "never reordered" rather than "written by a build that
    /// had no orders".
    ///
    /// A v5 blob read by a v4 build stops cleanly rather than corrupting: that
    /// reader splits three ways and hands the depth parser
    /// `"<depths>:<order>"`, where the colon is not a hex digit and ends the
    /// run at exactly the right place.
    V5,
    /// The fade gained a DIRECTION (`fdir`), and with it the arrival order
    /// changed meaning. It used to be a rank among the ON steps, with `00`
    /// written for every off step; it is now a rank among the step's OWN KIND,
    /// so the holes carry one too -- that is the order Fade Out introduces
    /// them in. Same field, same width, different reading, which is exactly
    /// what a version number is for.
    ///
    /// Both directions degrade rather than break. A v5 blob's `00`s fall
    /// through to the position order (see `read_pattern`), so an old patch
    /// fades its holes in left to right. A v6 blob in a v5 build has its
    /// off-step values loaded and then ignored, because that build's
    /// `renumber` only ranks the on steps.
    V6,
    /// Every slot has its own SOUND -- all thirteen values but the slot (see
    /// `crate::sound`). The top-level keys are the CURRENT slot's, exactly as
    /// they always were, and a slot whose sound differs from it adds one
    /// field, `"s<N>":"rate:attack:decay:sustain:release:hold:amount:fade:fsoft:fdir:legato:tmode:curve"`.
    ///
    /// A slot with no field of its own has the top level's sound, and that
    /// one rule is the whole migration: no older blob has an `s<N>`, so its
    /// one instance-wide sound -- with every conversion above applied to it
    /// first -- lands in all eight slots, beside each slot's own pattern. It
    /// also keeps a patch whose slots all sound alike exactly the size it
    /// was, which the bus insert's 1024 bytes are measured against.
    ///
    /// A v7 blob read by a v6 build loads the current slot's sound as the
    /// instance-wide one and ignores the rest: the slot that was playing
    /// still sounds the same.
    V7,
    /// Written by a newer build: read as far as this one understands it.
    Newer(i32),
}

impl Version {
    /// The version an `sv` names. Zero or less is what a blob without one
    /// reads as.
    pub fn of(sv: i32) -> Version {
        match sv {
            i32::MIN..=0 => Version::Unversioned,
            1 => Version::V1,
            2 => Version::V2,
            3 => Version::V3,
            4 => Version::V4,
            5 => Version::V5,
            6 => Version::V6,
            7 => Version::V7,
            n => Version::Newer(n),
        }
    }

    /// v1 to v3 wrote the stages as MILLISECONDS. They are converted to
    /// percentages of the gate's width as they load -- 2 ms read as 2% would
    /// be a patch that loads and sounds like a different patch.
    pub fn stages_in_ms(self) -> bool {
        matches!(self, Version::V1 | Version::V2 | Version::V3)
    }

    /// From v7 on, a slot whose sound differs carries it as `s<N>`.
    pub fn slot_sounds(self) -> bool {
        matches!(self, Version::V7 | Version::Newer(_))
    }
}

/*
 * A BLOB'S FIELDS, BY THE KEY EACH WAS SAVED UNDER: every key a version has
 * written and this one reads. The first of a key wins, as the reader that
 * searched the text for a key always found the first. Anything else -- v3's
 * `stopped`, a newer build's additions -- is passed over.
 */
#[derive(Default)]
pub(crate) struct Fields<'a> {
    sv: Option<Scalar<'a>>,
    slot: Option<Scalar<'a>>,
    rate: Option<Scalar<'a>>,
    attack: Option<Scalar<'a>>,
    decay: Option<Scalar<'a>>,
    sustain: Option<Scalar<'a>>,
    release: Option<Scalar<'a>>,
    hold: Option<Scalar<'a>>,
    amount: Option<Scalar<'a>>,
    mix: Option<Scalar<'a>>,
    depth: Option<Scalar<'a>>,
    fade: Option<Scalar<'a>>,
    fsoft: Option<Scalar<'a>>,
    fdir: Option<Scalar<'a>>,
    legato: Option<Scalar<'a>>,
    tmode: Option<Scalar<'a>>,
    curve: Option<Scalar<'a>>,
    p: [Option<Scalar<'a>>; SLOTS],
    s: [Option<Scalar<'a>>; SLOTS],
}

impl<'a> Fields<'a> {
    fn place(&mut self, key: &str) -> Option<&mut Option<Scalar<'a>>> {
        Some(match key {
            "sv" => &mut self.sv,
            "slot" => &mut self.slot,
            "rate" => &mut self.rate,
            "attack" => &mut self.attack,
            "decay" => &mut self.decay,
            "sustain" => &mut self.sustain,
            "release" => &mut self.release,
            "hold" => &mut self.hold,
            "amount" => &mut self.amount,
            "mix" => &mut self.mix,
            "depth" => &mut self.depth,
            "fade" => &mut self.fade,
            "fsoft" => &mut self.fsoft,
            "fdir" => &mut self.fdir,
            "legato" => &mut self.legato,
            "tmode" => &mut self.tmode,
            "curve" => &mut self.curve,
            k => match (slot_field(k, b'p'), slot_field(k, b's')) {
                (Some(i), _) => &mut self.p[i],
                (_, Some(i)) => &mut self.s[i],
                _ => return None,
            },
        })
    }
}

impl<'a> Sink<'a> for Fields<'a> {
    fn field(&mut self, key: Cow<'a, str>, value: Scalar<'a>) -> Result<(), ()> {
        if let Some(place) = self.place(&key) {
            place.get_or_insert(value);
        }
        Ok(())
    }
}

/// `p<N>` / `s<N>` for a slot 0..7: the slot, or None.
pub(crate) fn slot_field(key: &str, prefix: u8) -> Option<usize> {
    match key.as_bytes() {
        [p, d] if *p == prefix && d.is_ascii_digit() && ((d - b'0') as usize) < SLOTS => Some((d - b'0') as usize),
        _ => None,
    }
}

/// A text that is not one flat JSON object of the kind every build writes
/// (`json::plain`), and so no patch at all.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct NotAPatch;

/*
 * A STATE BLOB, READ: everything it carries, typed, clamped and decoded, and
 * nothing it does not. What a key's ABSENCE means is decided here too, where
 * it does not depend on the engine -- a blob without `hold` is a gate the
 * whole step long, whatever the engine held -- and left to the load where it
 * does: a blob without `sustain` keeps the engine's.
 *
 * Fixed-size and free of text, so it can be built where parsing is allowed and
 * handed to the audio thread whole.
 */
/// A state blob, read: ready for [`Instance::load`].
#[derive(Clone, Debug, PartialEq)]
pub struct Patch {
    sv: i32,
    slot: Option<usize>,
    rate: Option<usize>,
    /// As written: a v1..v3 blob's are milliseconds, converted by the load
    /// against the width the engine then has.
    attack: Option<f64>,
    decay: Option<f64>,
    release: Option<f64>,
    sustain: Option<f32>,
    hold: f32,
    amount: Option<f32>,
    legato: bool,
    time_mode: TimeMode,
    curve: Curve,
    fade: f32,
    fade_soft: bool,
    fade_dir: FadeDir,
    sounds: [Option<Sound>; SLOTS],
    patterns: [Option<Pattern>; SLOTS],
}

impl Patch {
    /// `text`, read. Allocates nothing, whatever the text, so the Move's
    /// audio callback may call it; see the note on threads above.
    pub fn parse(text: &str) -> Result<Patch, NotAPatch> {
        let mut f = Fields::default();
        json::read_plain(text, &mut f).map_err(|_| NotAPatch)?;
        Ok(Patch::from_fields(&f))
    }

    /// The version the blob says it is in.
    pub fn version(&self) -> Version {
        Version::of(self.sv)
    }

    pub(crate) fn from_fields(f: &Fields<'_>) -> Patch {
        let num = |v: &Option<Scalar<'_>>| v.as_ref().and_then(Scalar::number);
        let on = |v: &Option<Scalar<'_>>| num(v).is_some_and(|n| n >= 0.5);
        let unit = |n: f64| clampf(n as f32, 0.0, 1.0);
        /* Which format this blob is in; 0 when absent, which the oldest
         * pre-version blobs are. */
        let sv = num(&f.sv).unwrap_or(0.0) as i32;
        let version = Version::of(sv);

        let rate = match &f.rate {
            Some(Scalar::Str(label)) => Some(rates::index_from(label)),
            /* A numeric rate is an INDEX and must be resolved as one. Passing
             * "" here instead silently reset every such blob to the default --
             * a patch that loads, reports a rate, and runs at another. */
            v => num(v).map(|n| n as i64).filter(|n| *n >= 0 && (*n as usize) < rates::RATES.len()).map(|n| n as usize),
        };

        /*
         * THREE SPELLINGS OF ONE VALUE, AND THE OLD PAIR MULTIPLIES.
         *
         * v3 writes `amount`. v2 wrote `mix` and `depth`, which spanned one
         * degree of freedom between them, so the faithful migration is that
         * product. Reading just one would make every patch saved with depth < 1
         * jump to full -- louder gating on a patch that had been working.
         */
        let amount = match (num(&f.amount), num(&f.mix), num(&f.depth)) {
            (Some(a), _, _) => Some(unit(a)),
            (None, None, None) => None,
            (None, mix, depth) => Some(clampf(unit(mix.unwrap_or(1.0)) * unit(depth.unwrap_or(1.0)), 0.0, 1.0)),
        };

        Patch {
            sv,
            slot: num(&f.slot).filter(|n| *n >= 0.0 && (*n as usize) < SLOTS).map(|n| n as usize),
            rate,
            /* Kept raw and clamped LATER: a v3 blob's numbers are milliseconds
             * and do not fit the 0..200 a percentage does, so clamping here
             * would flatten every legacy attack above 200 ms before it could
             * be converted. */
            attack: num(&f.attack),
            decay: num(&f.decay),
            release: num(&f.release),
            sustain: num(&f.sustain).map(unit),
            /* Absent in v1 and v2 blobs, where the gate always ran the whole
             * step; 1.0 is that behaviour, so an old patch is unchanged. */
            hold: num(&f.hold).map_or(1.0, unit),
            amount,
            /* Absent in a pre-legato blob, and off is what those patches did. */
            legato: on(&f.legato),
            /* Absent in a pre-% blob, and MS is what those patches meant. */
            time_mode: if on(&f.tmode) { TimeMode::Pct } else { TimeMode::Ms },
            /* Absent in a pre-curve blob, and straight lines are what those
             * patches sounded like. */
            curve: num(&f.curve).map_or(Curve::Linear, |n| Curve::from_i32((n + 0.5) as i32)),
            /*
             * THE FADE, AND ABSENT MEANS THE WHOLE PATTERN.
             *
             * Every patch written before v5 has no `fade`, and 1.0 is what those
             * patches did: all of the gate, all of the time. Loading them at 0
             * would open eight slots of silence -- the same class of mistake as
             * a v1 blob's depths loading as zero.
             */
            fade: num(&f.fade).map_or(1.0, unit),
            fade_soft: on(&f.fsoft),
            /* Absent in a pre-v6 blob, and In is what those patches did. */
            fade_dir: if on(&f.fdir) { FadeDir::Out } else { FadeDir::In },
            /* A slot's own sound where a v7 blob wrote one; a field that is not
             * one this build could read leaves the slot the top level's. */
            sounds: core::array::from_fn(|s| {
                let field = f.s[s].as_ref().and_then(Scalar::text).filter(|_| version.slot_sounds());
                field.and_then(Sound::parse)
            }),
            /* Patterns travel as one field per slot so a slot cannot be
             * restored half-applied. */
            patterns: core::array::from_fn(|s| f.p[s].as_ref().and_then(Scalar::text).and_then(read_pattern)),
        }
    }
}

/// The format a blob says it is in: `sv`, or 0 for the oldest blobs, which
/// carry none -- and for a text that is no blob. A shell asks, because a blob
/// from before v7 holds one sound for all eight slots.
pub fn version(text: &str) -> i32 {
    /* Only `sv` is kept: the rest of the text is read past, not decoded. */
    struct Sv<'a>(Option<Scalar<'a>>);
    impl<'a> Sink<'a> for Sv<'a> {
        fn field(&mut self, key: Cow<'a, str>, value: Scalar<'a>) -> Result<(), ()> {
            if key == "sv" && self.0.is_none() {
                self.0 = Some(value);
            }
            Ok(())
        }
    }
    let mut sv = Sv(None);
    /* The same reader as Patch::parse, so a text it refuses is version 0 here
     * too, and no text says a version it would not load as. */
    match json::read_plain(text, &mut sv) {
        Ok(()) => sv.0.and_then(|v| v.number()).unwrap_or(0.0) as i32,
        Err(_) => 0,
    }
}

impl Instance {
    /*
     * A READ BLOB, APPLIED. Allocation-free, and no text in sight: everything
     * that could be decided without the engine was decided by `Patch::parse`.
     *
     * THE TOP LEVEL IS ONE SOUND, read into the slot it was saved from and
     * then handed to every slot that has no field of its own. It starts from
     * what that slot held, so a key a blob lacks and nothing defaults keeps
     * its value, as it always did.
     */
    /// Load `patch` -- the whole saved state -- into the engine. The playhead
    /// and the envelope carry on as through a slot switch.
    pub fn load(&mut self, patch: &Patch) {
        self.rev = self.rev.wrapping_add(1);
        let from_curve = self.snd().curve;
        if let Some(slot) = patch.slot {
            self.slot = slot;
        }
        let mut top = *self.snd();
        if let Some(rate) = patch.rate {
            top.rate_idx = rate;
        }
        if let Some(sustain) = patch.sustain {
            top.sustain = sustain;
        }
        top.hold = patch.hold;
        top.legato = patch.legato;
        top.time_mode = patch.time_mode;
        top.curve = patch.curve;
        top.fade = patch.fade;
        top.fade_soft = patch.fade_soft;
        top.fade_dir = patch.fade_dir;

        /*
         * THE STAGES, ONCE RATE AND WIDTH ARE BOTH KNOWN.
         *
         * A v3 blob holds absolute milliseconds and a v4 one percentages of the
         * gate's width, so a legacy patch is CONVERTED rather than
         * reinterpreted. The conversion needs the width, which needs the rate
         * AND hold -- hence the sound goes in first and the arithmetic follows.
         *
         * IT ASSUMES 120 BPM, because a patch does not carry the tempo it was
         * written at. `ms_per_step` is seeded at that tempo and only a running
         * transport replaces it, so a patch written at 120 converts exactly and
         * one written elsewhere converts proportionally -- the best available
         * without a tempo to read, and why the version exists rather than a
         * silent reinterpretation.
         */
        *self.snd_mut() = top;
        self.recalc_ms_per_step();
        let w = self.width_ms();
        let to_pct = if patch.version().stages_in_ms() && w > 1.0e-6 { 100.0 / w } else { 1.0 };
        let stage = |v: f64| clampf((v * to_pct) as f32, 0.0, STAGE_MAX_PCT);
        if let Some(v) = patch.attack {
            top.attack = stage(v);
        }
        if let Some(v) = patch.decay {
            top.decay = stage(v);
        }
        if let Some(v) = patch.release {
            top.release = stage(v);
        }
        if let Some(amount) = patch.amount {
            top.amount = amount;
        }

        /* Every slot: its own sound where a v7 blob wrote one, the top level's
         * everywhere else -- which is every slot of an older blob. */
        for (s, own) in patch.sounds.iter().enumerate() {
            self.snd[s] = own.unwrap_or(top);
        }
        for (s, pattern) in patch.patterns.iter().enumerate() {
            if let Some(p) = pattern {
                self.pat[s] = p.clone();
            }
        }
        /* THE CURSOR IS NOT SAVED, BUT THE LENGTH IS. A patch whose current slot
         * is shorter than where the cursor stood would leave it past the end,
         * where every edit lands on a step the ring never draws -- the same
         * re-clamp `slot` and `length` do. */
        let len = self.pat[self.slot].length.clamp(1, MAX_STEPS);
        if self.cursor >= len {
            self.cursor = len - 1;
        }
        /* The slot's sound arrived whole, so whatever the envelope is doing is
         * re-anchored and re-measured as a switch would. */
        self.reanchor(from_curve);
        self.recalc_ms_per_step();
        self.recalc_fade();
    }
}

/*
 * ONE SLOT'S PATTERN FROM ITS FIELD: "<steps>:<ties>:<length>[:<depths>[:<orders>]]",
 * or None for a field with fewer than three parts, which leaves the slot's
 * pattern as it was. The same text a slot file carries (see `slotfile`), so
 * both read it here.
 */
pub(crate) fn read_pattern(field: &str) -> Option<Pattern> {
    /* The two masks are up to 32 hex digits, so they are read as TEXT and
     * handed to the LSB-aligned parser -- a %x would cap them at whatever an
     * unsigned holds and silently drop steps 32 and up. A v3 blob's 8-digit
     * field parses identically. */
    let mut parts = field.splitn(5, ':');
    let (Some(stx), Some(tix), Some(lens)) = (parts.next(), parts.next(), parts.next()) else {
        return None;
    };
    let mut p = Pattern::new(0);
    set_pattern_hex(&mut p.steps, stx);
    set_pattern_hex(&mut p.ties, tix);
    p.length = (fmt::atoi(lens)).clamp(1, MAX_STEPS as i64) as usize;

    /*
     * A V1 TRIPLE HAS NO DEPTHS, AND ABSENT MEANS FULL.
     *
     * Every patch saved before that version ends after the length. Leaving
     * the array at whatever it held -- or zeroing it -- would load those
     * patches SILENT, with the pattern and the ring both looking completely
     * correct. This is the whole reason the state version exists.
     */
    p.depth = [DEPTH_FULL; MAX_STEPS];
    if let Some(d) = parts.next() {
        let b = d.as_bytes();
        for i in 0..MAX_STEPS {
            let (Some(&h), Some(&l)) = (b.get(i * 2), b.get(i * 2 + 1)) else { break };
            let (Some(h), Some(l)) = (hexval(h), hexval(l)) else { break };
            p.depth[i] = h * 16 + l;
        }
    }

    /*
     * AN ABSENT OR ZERO ORDER MEANS POSITION ORDER, AND THAT COVERS TWO
     * DIFFERENT OLD BLOBS AT ONCE.
     *
     * A pre-v5 quadruple has no order field at all. A v5 one has the field
     * but wrote `00` for every OFF step, because ranks only covered the on
     * steps then -- so the holes arrive here with nothing to say. Both fall
     * through to the position seed below, are normalised per kind, and come
     * out sweeping left to right: the one behaviour nobody has to be told
     * about. Zeroing instead would make every step rank 1 and the whole
     * pattern arrive at once, which looks like the fade being broken rather
     * than absent.
     */
    for i in 0..MAX_STEPS {
        p.order[i] = (i + 1) as u8;
    }
    if let Some(o) = parts.next() {
        let b = o.as_bytes();
        for i in 0..MAX_STEPS {
            let (Some(&h), Some(&l)) = (b.get(i * 2), b.get(i * 2 + 1)) else { break };
            let (Some(h), Some(l)) = (hexval(h), hexval(l)) else { break };
            let v = h * 16 + l;
            /* 00 is "this blob had no rank for this step" -- a v5 hole, or a
             * step past what was written. Leaving the seeded position value
             * there is what makes both old formats load sensibly. */
            if v != 0 {
                p.order[i] = v;
            }
        }
    }
    p.renumber();
    Some(p)
}

fn hexval(c: u8) -> Option<u8> {
    match c {
        b'0'..=b'9' => Some(c - b'0'),
        b'a'..=b'f' => Some(c - b'a' + 10),
        b'A'..=b'F' => Some(c - b'A' + 10),
        _ => None,
    }
}

/// One slot's pattern as its field -- the reader is [`read_pattern`].
pub(crate) fn write_pattern(p: &Pattern, b: &mut dyn Write) -> core::fmt::Result {
    p.steps.to_hex(b)?;
    write!(b, ":")?;
    p.ties.to_hex(b)?;
    write!(b, ":{}:", p.length)?;

    /*
     * TRAILING DEFAULTS ARE NOT WRITTEN.
     *
     * This used to emit MAX_STEPS depths unconditionally -- 256
     * characters per slot at 128 steps, whether or not a single one
     * differed from full. The reader already treats an absent depth as
     * FULL (that is how v1 blobs load), so the last non-default entry is
     * the only place worth stopping at: a pattern with no accents writes
     * no depths at all.
     *
     * It is what keeps a realistic 8-slot patch inside a bus insert's
     * 1024 bytes at the new length, and it changes nothing about what a
     * reader gets back.
     */
    let last = (0..MAX_STEPS)
        .rev()
        .find(|&i| p.depth[i] != DEPTH_FULL);
    if let Some(last) = last {
        for i in 0..=last {
            write!(b, "{:02X}", p.depth[i])?;
        }
    }

    /*
     * THE ORDER IS ONLY WRITTEN WHEN IT IS NOT POSITION ORDER.
     *
     * Same rule as the depths above, and here it does more work: an order
     * nobody has touched is the overwhelmingly common case, and writing it
     * would add up to 256 characters per slot -- 2 KB across eight -- to
     * every patch, for a value the reader already defaults to. That is the
     * difference between a realistic patch fitting a bus insert's
     * 1024 bytes and not.
     *
     * The comparison is against the RANK a position order would give, not
     * against the raw keys, because that is what a reader reconstructs.
     */
    let (mut on_pos, mut off_pos) = (0u8, 0u8);
    let same = (0..p.length.min(MAX_STEPS)).all(|i| {
        let pos = if p.on(i) {
            on_pos += 1;
            on_pos
        } else {
            off_pos += 1;
            off_pos
        };
        p.order[i] == pos
    });
    if !same {
        write!(b, ":")?;
        for i in 0..p.length.min(MAX_STEPS) {
            write!(b, "{:02X}", p.order[i])?;
        }
    }
    Ok(())
}

/// The engine's saved state as a v7 blob, into `b`. Allocation-free: a shell
/// formats it on the audio thread whenever it may have changed.
pub fn save(inst: &Instance, mut b: Buf) -> i32 {
    let top = inst.snd();
    let _ = write!(
        b,
        "{{\"sv\":{},\"slot\":{},\"rate\":\"{}\",",
        STATE_VERSION,
        inst.slot,
        rates::RATES[top.rate_idx].label
    );
    let _ = write!(b, "\"attack\":");
    let _ = fmt::f(&mut b, top.attack as f64, 2);
    let _ = write!(b, ",\"decay\":");
    let _ = fmt::f(&mut b, top.decay as f64, 2);
    let _ = write!(b, ",\"sustain\":");
    let _ = fmt::f(&mut b, top.sustain as f64, 3);
    let _ = write!(b, ",\"release\":");
    let _ = fmt::f(&mut b, top.release as f64, 2);
    let _ = write!(b, ",\"hold\":");
    let _ = fmt::f(&mut b, top.hold as f64, 3);
    let _ = write!(b, ",\"amount\":");
    let _ = fmt::f(&mut b, top.amount as f64, 3);
    let _ = write!(b, ",\"fade\":");
    let _ = fmt::f(&mut b, top.fade as f64, 4);
    let _ = write!(
        b,
        ",\"fsoft\":{},\"fdir\":{},\"legato\":{},\"tmode\":{},\"curve\":{}",
        top.fade_soft as i32,
        top.fade_dir as i32,
        top.legato as i32,
        top.time_mode as i32,
        top.curve as i32
    );

    for s in 0..SLOTS {
        let _ = write!(b, ",\"p{}\":\"", s);
        let _ = write_pattern(&inst.pat[s], &mut b);
        let _ = write!(b, "\"");
    }

    /*
     * AFTER THE PATTERNS, so everything a v6 blob held is where it was.
     *
     * A SLOT IS WRITTEN ONLY WHERE IT SOUNDS DIFFERENT -- compared as the text
     * it would write, so "different" means different to a reader, and a slot
     * equal to the top level is the top level's on the way back in.
     */
    let mut mine = [0u8; 160];
    let mine = top.text(&mut mine);
    for s in 0..SLOTS {
        let mut theirs = [0u8; 160];
        if inst.snd[s].text(&mut theirs) != mine {
            let _ = write!(b, ",\"s{}\":\"", s);
            let _ = inst.snd[s].write(&mut b);
            let _ = write!(b, "\"");
        }
    }
    let _ = write!(b, "}}");
    b.finish()
}

#[cfg(test)]
mod tests;
