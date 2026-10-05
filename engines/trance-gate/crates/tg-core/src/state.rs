/*!
The `state` blob: one string that carries a whole patch.

# Versions, and why there is a number at all

A version field costs nothing and is the only thing that can tell one reading
of a number from another. The Ducker shipped without one, and a 0.1.x blob's
envelope times -- read as the units the next version introduced -- collapsed
the effect silently on a patch that had been working.

- **2**: patterns gained a per-step DEPTH array. A v1 blob has none, and the
  absent value must load as FULL; loading it as zero would silently mute every
  gate in every patch that already works.
- **3**: `depth` folded into `mix`. They were one quantity with two names --
  only their PRODUCT ever reached the audio -- so a v2 blob's migration
  multiplies them, and a saved patch sounds identical rather than jumping to
  full wet.
- **4**: attack/decay/release are PERCENTAGES OF WIDTH, not milliseconds. The
  keys did not change, so only the version can tell a v3's `"attack": 2.0`
  (2 ms) from a v4's (2% of the gate).
- **5**: the fade-in. Patterns gained a per-step ARRIVAL ORDER as a fifth
  `p<N>` field, and `fade` / `fsoft` joined the globals. Every one of them is
  ABSENT-MEANS-INERT: no order field is position order, no `fade` is 1.0 and no
  `fsoft` is hard, which together are exactly what a v4 patch did. So this
  version number buys nothing today -- and it is here anyway, because the one
  thing it can do is tell a later reader that an absent order field meant
  "never reordered" rather than "written by a build that had no orders".

  A v5 blob read by a v4 build stops cleanly rather than corrupting: that
  reader splits three ways and hands the depth parser `"<depths>:<order>"`,
  where the colon is not a hex digit and ends the run at exactly the right
  place.
- **6**: the fade gained a DIRECTION (`fdir`), and with it the arrival order
  changed meaning. It used to be a rank among the ON steps, with `00` written
  for every off step; it is now a rank among the step's OWN KIND, so the holes
  carry one too -- that is the order Fade Out introduces them in. Same field,
  same width, different reading, which is exactly what a version number is for.

  Both directions degrade rather than break. A v5 blob's `00`s fall through to
  the position order seeded below, so an old patch fades its holes in left to
  right. A v6 blob in a v5 build has its off-step values loaded and then ignored,
  because that build's `renumber` only ranks the on steps.
- **7**: every slot has its own SOUND -- all thirteen values but the slot (see
  `crate::sound`). The top-level keys are the CURRENT slot's, exactly as they
  always were, and a slot whose sound differs from it adds one field,
  `"s<N>":"rate:attack:decay:sustain:release:hold:amount:fade:fsoft:fdir:legato:tmode:curve"`.

  A slot with no field of its own has the top level's sound, and that one rule
  is the whole migration: no older blob has an `s<N>`, so its one instance-wide
  sound -- with every conversion above applied to it first -- lands in all
  eight slots, beside each slot's own pattern. It also keeps a patch whose slots
  all sound alike exactly the size it was, which the bus insert's 1024 bytes
  are measured against.

  A v7 blob read by a v6 build loads the current slot's sound as the
  instance-wide one and ignores the rest: the slot that was playing still
  sounds the same.
*/

use crate::envelope::Curve;
use crate::sound::Sound;
use crate::fmt::{self, Buf};
use crate::params::set_pattern_hex;
use crate::{rates, Instance, Pattern, TimeMode, DEPTH_FULL, MAX_STEPS, SLOTS, STAGE_MAX_PCT};
use core::fmt::Write;

pub const STATE_VERSION: i32 = 7;
/// The first version in which every slot carries its own sound. A blob older
/// than this holds one sound for all eight.
pub const STATE_VERSION_SLOT_SOUNDS: i32 = 7;

#[inline]
fn clampf(x: f32, lo: f32, hi: f32) -> f32 {
    if x < lo { lo } else if x > hi { hi } else { x }
}

/* Tiny JSON scanners. No allocation, no locale surprises -- the same two the
 * C had, for the same reason: the blob is ours, it is flat, and a parser
 * would be more code than the format. */
fn find_value<'a>(json: &'a str, key: &str) -> Option<&'a str> {
    let mut needle = [0u8; 64];
    let mut n = 0;
    for &b in b"\"" {
        needle[n] = b;
        n += 1;
    }
    for &b in key.as_bytes() {
        if n + 2 >= needle.len() {
            return None;
        }
        needle[n] = b;
        n += 1;
    }
    needle[n] = b'"';
    n += 1;
    needle[n] = b':';
    n += 1;
    let needle = core::str::from_utf8(&needle[..n]).ok()?;
    let at = json.find(needle)? + needle.len();
    Some(json[at..].trim_start_matches([' ', '\t']))
}

fn get_number(json: &str, key: &str) -> Option<f64> {
    find_value(json, key).map(fmt::atof)
}

fn get_string<'a>(json: &'a str, key: &str) -> Option<&'a str> {
    let v = find_value(json, key)?;
    let rest = v.strip_prefix('"')?;
    let end = rest.find('"')?;
    Some(&rest[..end])
}

/// The format a blob says it is in: `sv`, or 0 for the oldest blobs, which
/// carry none. A shell asks, because a blob from before v7 holds one sound
/// for all eight slots.
pub fn version(val: &str) -> i32 {
    get_number(val, "sv").unwrap_or(0.0) as i32
}

pub fn load(inst: &mut Instance, val: &str) {
    let from_curve = inst.snd().curve;
    /* Which format this blob is in; 0 when absent, which the oldest
     * pre-version blobs are. */
    let sv_num = version(val);

    if let Some(n) = get_number(val, "slot") {
        if n >= 0.0 && (n as usize) < SLOTS {
            inst.slot = n as usize;
        }
    }
    /*
     * THE TOP LEVEL IS ONE SOUND, read into the slot it was saved from and
     * then handed to every slot that has no field of its own. It starts from
     * what that slot held, so a key a blob lacks and nothing below defaults
     * keeps its value, as it always did.
     */
    let mut top = *inst.snd();
    if let Some(s) = get_string(val, "rate") {
        top.rate_idx = rates::index_from(s);
    } else if let Some(n) = get_number(val, "rate") {
        /* A numeric rate is an INDEX and must be resolved as one. Passing ""
         * here instead silently reset every such blob to the default -- a
         * patch that loads, reports a rate, and runs at another. */
        let n = n as i64;
        if n >= 0 && (n as usize) < rates::RATES.len() {
            top.rate_idx = n as usize;
        }
    }

    /* Read raw and clamp LATER: a v3 blob's numbers are milliseconds and do
     * not fit the 0..200 a percentage does, so clamping here would flatten
     * every legacy attack above 200 ms before it could be converted. */
    let raw_att = get_number(val, "attack");
    let raw_dec = get_number(val, "decay");
    let raw_rel = get_number(val, "release");

    if let Some(n) = get_number(val, "sustain") {
        top.sustain = clampf(n as f32, 0.0, 1.0);
    }
    /* Absent in v1 and v2 blobs, where the gate always ran the whole step;
     * 1.0 is that behaviour, so an old patch is unchanged. */
    top.hold = 1.0;
    /* Absent in a pre-legato blob, and off is what those patches did. */
    top.legato = get_number(val, "legato").map_or(false, |n| n >= 0.5);
    /* Absent in a pre-% blob, and MS is what those patches meant. */
    top.time_mode = match get_number(val, "tmode") {
        Some(n) if n >= 0.5 => TimeMode::Pct,
        _ => TimeMode::Ms,
    };
    /* Absent in a pre-curve blob, and straight lines are what those patches
     * sounded like. */
    top.curve = match get_number(val, "curve") {
        Some(n) => Curve::from_i32((n + 0.5) as i32),
        None => Curve::Linear,
    };
    if let Some(n) = get_number(val, "hold") {
        top.hold = clampf(n as f32, 0.0, 1.0);
    }
    /*
     * THE FADE, AND ABSENT MEANS THE WHOLE PATTERN.
     *
     * Every patch written before v5 has no `fade`, and 1.0 is what those
     * patches did: all of the gate, all of the time. Loading them at 0 would
     * open eight slots of silence -- the same class of mistake as a v1 blob's
     * depths loading as zero, and the reason that note exists above.
     */
    top.fade = match get_number(val, "fade") {
        Some(n) => clampf(n as f32, 0.0, 1.0),
        None => 1.0,
    };
    top.fade_soft = get_number(val, "fsoft").map_or(false, |n| n >= 0.5);
    /* Absent in a pre-v6 blob, and In is what those patches did. */
    top.fade_dir = match get_number(val, "fdir") {
        Some(n) if n >= 0.5 => crate::FadeDir::Out,
        _ => crate::FadeDir::In,
    };

    /*
     * THE STAGES, ONCE RATE AND WIDTH ARE BOTH KNOWN.
     *
     * A v3 blob holds absolute milliseconds and a v4 one percentages of the
     * gate's width, so a legacy patch is CONVERTED rather than reinterpreted
     * -- 2 ms read as 2% would be a patch that loads and sounds like a
     * different patch.
     *
     * The conversion needs the width, which needs the rate AND hold, and hold
     * is parsed just above. Hence the raw reads earlier and the arithmetic
     * here rather than in place.
     *
     * IT ASSUMES 120 BPM, because a patch does not carry the tempo it was
     * written at. `ms_per_step` is seeded at that tempo and only a running
     * transport replaces it, so a patch written at 120 converts exactly and
     * one written elsewhere converts proportionally -- the best available
     * without a tempo to read, and why the version exists rather than a
     * silent reinterpretation.
     */
    *inst.snd_mut() = top;
    inst.recalc_ms_per_step();
    let legacy_ms = sv_num > 0 && sv_num < 4;
    let w = inst.width_ms();
    let to_pct = if legacy_ms && w > 1.0e-6 { 100.0 / w } else { 1.0 };
    if let Some(v) = raw_att {
        top.attack = clampf((v * to_pct) as f32, 0.0, STAGE_MAX_PCT);
    }
    if let Some(v) = raw_dec {
        top.decay = clampf((v * to_pct) as f32, 0.0, STAGE_MAX_PCT);
    }
    if let Some(v) = raw_rel {
        top.release = clampf((v * to_pct) as f32, 0.0, STAGE_MAX_PCT);
    }

    /*
     * THREE SPELLINGS OF ONE VALUE, AND THE OLD PAIR MULTIPLIES.
     *
     * v3 writes `amount`. v2 wrote `mix` and `depth`, which spanned one
     * degree of freedom between them, so the faithful migration is that
     * product. Reading just one would make every patch saved with depth < 1
     * jump to full -- louder gating on a patch that had been working.
     */
    if let Some(n) = get_number(val, "amount") {
        top.amount = clampf(n as f32, 0.0, 1.0);
    } else {
        let mix = get_number(val, "mix");
        let depth = get_number(val, "depth");
        if mix.is_some() || depth.is_some() {
            let m = clampf(mix.unwrap_or(1.0) as f32, 0.0, 1.0);
            let d = clampf(depth.unwrap_or(1.0) as f32, 0.0, 1.0);
            top.amount = clampf(m * d, 0.0, 1.0);
        }
    }

    /* Every slot: its own field where a v7 blob wrote one, the top level's
     * sound everywhere else -- which is every slot of an older blob. */
    for s in 0..SLOTS {
        inst.snd[s] = match get_string(val, slot_key(b's', s).as_str()) {
            Some(field) if sv_num >= 7 => Sound::parse(field).unwrap_or(top),
            _ => top,
        };
    }

    /* Patterns travel as one field per slot so a slot cannot be restored
     * half-applied. */
    for s in 0..SLOTS {
        if let Some(field) = get_string(val, slot_key(b'p', s).as_str()) {
            read_pattern(&mut inst.pat[s], field);
        }
    }
    /* THE CURSOR IS NOT SAVED, BUT THE LENGTH IS. A patch whose current slot
     * is shorter than where the cursor stood would leave it past the end,
     * where every edit lands on a step the ring never draws -- the same
     * re-clamp `slot` and `length` do. */
    let len = inst.pat[inst.slot].length.clamp(1, MAX_STEPS);
    if inst.cursor >= len {
        inst.cursor = len - 1;
    }
    /* The slot's sound arrived whole, so whatever the envelope is doing is
     * re-anchored and re-measured as a switch would. */
    inst.reanchor(from_curve);
    inst.recalc_ms_per_step();
    inst.recalc_fade();
}

/// `p<N>` or `s<N>`: a slot's field name, without allocating.
struct SlotKey([u8; 2]);

impl SlotKey {
    fn as_str(&self) -> &str {
        core::str::from_utf8(&self.0).unwrap_or("")
    }
}

fn slot_key(prefix: u8, slot: usize) -> SlotKey {
    SlotKey([prefix, b'0' + (slot % 10) as u8])
}

/*
 * ONE SLOT'S PATTERN FROM ITS FIELD: "<steps>:<ties>:<length>[:<depths>[:<orders>]]".
 * The same text a slot file carries (see `slotfile`), so both read it here.
 */
pub(crate) fn read_pattern(p: &mut Pattern, field: &str) {
    /* "<steps>:<ties>:<length>[:<depths>]". The two masks are up to 32
     * hex digits now, so they are read as TEXT and handed to the
     * LSB-aligned parser -- a %x would cap them at whatever an unsigned
     * holds and silently drop steps 32 and up. A v3 blob's 8-digit field
     * parses identically. */
    let mut parts = field.splitn(5, ':');
    let (Some(stx), Some(tix), Some(lens)) = (parts.next(), parts.next(), parts.next())
    else {
        return;
    };
    set_pattern_hex(&mut p.steps, stx);
    set_pattern_hex(&mut p.ties, tix);
    p.length = (fmt::atoi(lens)).clamp(1, MAX_STEPS as i64) as usize;

    /*
     * A V1 TRIPLE HAS NO DEPTHS, AND ABSENT MEANS FULL.
     *
     * Every patch saved before that version ends after the length.
     * Leaving the array at whatever it held -- or zeroing it -- would
     * load those patches SILENT, with the pattern and the ring both
     * looking completely correct. This is the whole reason the state
     * version exists.
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
            /* 00 is "this blob had no rank for this step" -- a v5 hole, or
             * a step past what was written. Leaving the seeded position
             * value there is what makes both old formats load sensibly. */
            if v != 0 {
                p.order[i] = v;
            }
        }
    }
    p.renumber();
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
