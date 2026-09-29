/*!
Trance Gate -- a tempo-locked step gate with a per-step ADSR.

Modelled on the Kilohearts Trance Gate: 8 pattern slots, a pattern of up to
128 steps with ties, a Rate (the length of one step), an ADSR applied at each
step, and an Amount.

# Threading

Every entry point runs on an audio callback. On Move that is SCHED_FIFO 70,
pinned to core 3, with ~2370us of slack per 128-frame block; in a plugin it is
whatever the host provides. There is no control thread.

So: **no allocation outside [`Instance::new`], no I/O, no locks, no logging**.
The workspace sets `panic = "abort"` because unwinding out of `extern "C"`
into a C host is undefined behaviour, and a crash the OS reports is better
than one that corrupts the host's stack on the way out.
*/

pub mod envelope;
pub mod fmt;
pub mod mask;
pub mod params;
pub mod rates;
pub mod state;

use envelope::{Curve, Env, Stage, StageLens};
use mask::Mask;

pub const MAX_STEPS: usize = 128;
pub const SLOTS: usize = 8;
pub const DEPTH_FULL: u8 = 255;

/// A stage runs to twice the gate's width and no further -- past that it
/// cannot finish under any Width, so the extra range would be knob travel
/// with nothing on the end of it.
pub const STAGE_MAX_PCT: f32 = 200.0;

/// What the envelope's three time values MEAN to a shell showing them. The
/// engine does not consult it: ms and % are two readings of one number.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum TimeMode {
    Ms = 0,
    Pct = 1,
}

/*
 * WHICH WAY THE FADE BUILDS THE PATTERN UP.
 *
 * The knob means the same thing in both: HOW MUCH OF THE DRAWN PATTERN IS
 * PRESENT, 0..1. Only the missing part differs -- In leaves silence where a step
 * has not arrived, Out leaves the gate open. So 100% is the pattern either way,
 * which is what keeps it the neutral default and lets the direction be switched
 * at rest without changing a sample.
 *
 * OUT FILLS HOLES; IT DOES NOT BYPASS THE GATE. At Out 0% every step sounds, and
 * below Width 100% the gate still pulses -- a denser gate, not an open one.
 * Amount is what bypasses.
 */
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum FadeDir {
    /// The steps you drew ON arrive, one at a time. 0% is silence.
    In = 0,
    /// The steps you drew OFF -- the holes -- arrive. 0% is every hole filled.
    Out = 1,
}

impl FadeDir {
    pub fn from_i32(v: i32) -> FadeDir {
        if v >= 1 { FadeDir::Out } else { FadeDir::In }
    }
}

/// Beyond this much error, jump rather than glide.
const RESYNC_STEPS: f64 = 0.25;
/// Fraction of the phase error absorbed per block.
const TRACK_GAIN: f64 = 0.05;

/// What the host says about the transport.
#[derive(Clone, Copy, Default)]
pub struct Transport {
    pub running: bool,
    pub beats: f64,
    pub bpm: f32,
}

#[derive(Clone)]
pub struct Pattern {
    pub steps: Mask,
    pub ties: Mask,
    /// 1..=MAX_STEPS
    pub length: usize,
    /// Per-step level, 0..255. An ACCENT: the global Amount scales the whole
    /// sequence on top of it, so this says "how much of the gate" and Amount
    /// says "how much gating". 255 is the neutral value, which is why a v1
    /// blob without the array must fill it rather than zero it.
    pub depth: [u8; MAX_STEPS],
    /*
     * ARRIVAL ORDER: WHICH STEP THE FADE INTRODUCES FIRST.
     *
     * A 1-based rank among the ON steps inside `length`, and it is kept a
     * PERMUTATION of 1..=N by `renumber` after every edit to the mask -- so
     * there is no illegal state for a shell to draw and no numbering for a
     * user to repair. An off step keeps whatever it held, which is what lets
     * a step switched off and on again come back where it was.
     *
     * Position order (the leftmost on step is 1) is the neutral value, which
     * is why a pre-fade blob without the array must fill it that way rather
     * than zero it: zero would make every step arrive at once.
     */
    pub order: [u8; MAX_STEPS],
}

impl Pattern {
    fn new(slot: usize) -> Self {
        let mut p = Self {
            steps: Mask::new(),
            ties: Mask::new(),
            length: 16,
            depth: [DEPTH_FULL; MAX_STEPS],
            /* Overwritten by the `renumber` below; a distinct value per step
             * rather than all-zero so the sort is not asked to break ties. */
            order: [0; MAX_STEPS],
        };
        /* Slot 1 is every other step -- the plainest thing that is audibly a
         * gate the moment the module is loaded. The rest start fully open,
         * which is silence-free rather than "the effect is broken". Written
         * through the bit helpers so the initial pattern cannot silently
         * depend on the mask being exactly one word wide. */
        for i in 0..16 {
            p.steps.set(i, if slot == 0 { i % 2 == 0 } else { true });
        }
        for i in 0..MAX_STEPS {
            p.order[i] = (i + 1) as u8;
        }
        p.renumber();
        p
    }

    /*
     * THE ORDER IS NORMALISED RATHER THAN VALIDATED.
     *
     * Every caller that can move the mask, the length or a rank ends here, and
     * what leaves is always a permutation of 1..=N over the ON steps inside
     * `length`, in the same relative order they went in. So "what happens when
     * you switch a step off" needs no rule of its own: the gap closes.
     *
     * Off steps and steps past `length` keep their stored rank, deliberately.
     * They are what a step switched back on -- or a Length turned back up --
     * returns to, and they cost nothing because nothing reads them.
     *
     * A COUNTING RANK, NOT A SORT. The rank of step i is "how many on steps
     * carry a lower key", which is O(length^2) at worst -- 16k integer
     * comparisons at 128 steps -- and needs no scratch array. `set_param` runs
     * on the audio callback, so an allocation here would be the real cost, not
     * the comparisons. Ties in the key break by index, which is what keeps
     * this a total order and the result a permutation.
     */
    pub fn renumber(&mut self) {
        self.renumber_kind(true);
        self.renumber_kind(false);
    }

    /*
     * ONE ARRAY, BOTH ORDERS, BECAUSE A STEP IS EITHER ON OR OFF.
     *
     * The fade runs in two directions: IN introduces the steps you drew on, OUT
     * introduces the holes. Each needs its own arrival order -- and the two sets
     * PARTITION the pattern, so one array holds both without a conflict. An on
     * step's rank is its place among the on steps, an off step's among the off
     * steps, and neither can be asked about the other.
     *
     * The alternative was a second 128-byte array per slot, which is a kilobyte
     * across eight slots and another field in a blob that is already measured
     * against a bus insert's 1024 bytes.
     */
    fn renumber_kind(&mut self, want_on: bool) {
        let n = self.length.min(MAX_STEPS);
        let mut rank = [0u8; MAX_STEPS];
        for i in 0..n {
            if self.on(i) != want_on {
                continue;
            }
            let mut r = 1u32;
            for j in 0..n {
                if j == i || self.on(j) != want_on {
                    continue;
                }
                let (a, b) = (self.order[j], self.order[i]);
                if a < b || (a == b && j < i) {
                    r += 1;
                }
            }
            rank[i] = r.min(255) as u8;
        }
        for i in 0..n {
            if self.on(i) == want_on {
                self.order[i] = rank[i];
            }
        }
    }

    /// How many steps sound in one cycle -- the divisor Fade In spaces its
    /// arrivals over.
    pub fn hits(&self) -> usize {
        (0..self.length.min(MAX_STEPS)).filter(|&i| self.on(i)).count()
    }

    /// How many holes one cycle has -- the divisor Fade Out spaces its arrivals
    /// over. `hits() + holes() == length`, which is what makes one order array
    /// enough for both.
    pub fn holes(&self) -> usize {
        self.length.min(MAX_STEPS) - self.hits()
    }

    /// How many steps of `i`'s own kind there are, which is the range its rank
    /// lives in.
    pub fn kin(&self, i: usize) -> usize {
        if i < MAX_STEPS && self.on(i) { self.hits() } else { self.holes() }
    }

    /*
     * BACK TO POSITION ORDER, FOR A MASK THAT ARRIVED WHOLE.
     *
     * A rank only means anything relative to the other steps of its kind, so
     * REPLACING the mask invalidates every one of them at once: the partition
     * moved, and the keys left behind rank steps against a pattern that no
     * longer exists. Renumbering them anyway produces a deterministic but
     * arbitrary order -- the same argument `randomize` makes for clearing ties.
     *
     * It showed up as SIZE. A wholesale write left an order that differed from
     * position order in every slot, so the emitter wrote all 2 KB of it, and the
     * no-accent blob went from ~310 bytes to 2358 -- past a bus insert's 1024.
     * The arbitrary fade order was the real bug; the byte count is how it was
     * noticed.
     *
     * A LENGTH change is not this: the mask is the same mask, so the ranks still
     * mean what they meant, and steps coming into range carry the position seed
     * `renumber` has never touched.
     */
    pub fn reseed_order(&mut self) {
        for i in 0..MAX_STEPS {
            self.order[i] = (i + 1) as u8;
        }
        self.renumber();
    }

    /*
     * A STEP JOINING ITS KIND ARRIVES LAST.
     *
     * `renumber` cannot infer this -- a step that has never been on has no
     * meaningful key, and one switched off and on again has a stale one. Both
     * want "after everything currently of this kind", which is what a pattern
     * being drawn in reads as: the order you click is the order they arrive.
     *
     * Toggling a step moves it BETWEEN the two rankings: it leaves one, which
     * compacts behind it, and joins the other at the end. The mask bit must
     * already be set to its new value when this is called, because that is what
     * says which kind it is joining.
     */
    pub fn order_append(&mut self, i: usize) {
        if i >= MAX_STEPS {
            return;
        }
        /* Above every rank a permutation of 1..=128 can hold, so the counting
         * pass places it last among its own kind. */
        self.order[i] = 255;
        self.renumber();
    }

    /*
     * Put step `i` at rank `rank` among its own kind, and give whoever held that
     * rank `i`'s old one.
     *
     * A SWAP, NOT AN INSERT. Typing a number into a step that already has one is
     * an exchange -- you are naming which step arrives Nth, and the step that was
     * Nth has to go somewhere. Shifting the whole run instead would renumber
     * every step between the two, which is not what you asked for and is
     * invisible until you look at the others.
     *
     * Tapping the pads in ORDER mode still produces the sequence you tap: each
     * click swaps the next step into the next rank, and the steps already placed
     * hold ranks below it, so none of them can be the one swapped out.
     */
    pub fn order_set(&mut self, i: usize, rank: usize) {
        if i >= MAX_STEPS || i >= self.length {
            return;
        }
        self.renumber();
        let n = self.kin(i);
        if n == 0 {
            return;
        }
        let r = rank.clamp(1, n) as u8;
        let k = self.order[i];
        if r == k {
            return;
        }
        let want_on = self.on(i);
        for j in 0..self.length.min(MAX_STEPS) {
            if j != i && self.on(j) == want_on && self.order[j] == r {
                self.order[j] = k;
                break;
            }
        }
        self.order[i] = r;
    }

    #[inline]
    pub fn on(&self, i: usize) -> bool {
        self.steps.get(i)
    }
    #[inline]
    pub fn tied(&self, i: usize) -> bool {
        self.ties.get(i)
    }
}

pub struct Instance {
    pub pat: Vec<Pattern>,
    pub slot: usize,
    pub rate_idx: usize,

    /*
     * ATTACK, DECAY AND RELEASE ARE PERCENTAGES OF THE GATE'S WIDTH, 0..200.
     *
     * Not milliseconds, despite the wire keys, which are kept because they
     * appear in every saved patch. 100% is "exactly fills the gate"; 200% is
     * "twice the gate", a stage that never finishes before the gate shuts.
     *
     * Measured against WIDTH and not against the step because the step is not
     * the musical unit here -- the gate's open time is. It also makes ms and
     * % two readings of ONE number: ms is `value/100 * width_ms`, so its
     * maximum moves with the rate and with Width while the percentage stays
     * put. See [`Instance::stage_samples`].
     */
    pub attack: f32,
    pub decay: f32,
    /// 0..1 -- a LEVEL, not a duration.
    pub sustain: f32,
    pub release: f32,

    /*
     * How much of a step the gate stays open, 0..1.
     *
     * SUSTAIN IS A LEVEL AND HAS NO LENGTH -- in an ADSR it holds until the
     * note ends, and here "the note" is the step. That is correct and it is
     * also not what someone reaching for a shorter gate wants. This is the
     * control they are reaching for: release begins this far into the step
     * rather than at its end, which is a sequencer's gate length.
     */
    pub hold: f32,
    /// How much the gate acts, 0..1. 1 == a closed gate is silent, 0 == the
    /// effect is bypassed.
    pub amount: f32,
    /// Edit position on the ring, 0..length-1.
    pub cursor: usize,

    // ---- runtime, not saved ----
    pub step_pos: f64,
    /// Step index at the previous sample; `None` = none.
    pub last_step: Option<usize>,
    pub was_running: bool,
    pub env: Env,
    /*
     * THE STRUCK STEP'S LEVEL, HELD FOR THE WHOLE GATE.
     *
     * Read fresh every sample as `depth[current_step]`, this is wrong in the
     * two places where a gate and a step are not the same span: a RELEASE
     * outliving its step was scaled by the NEXT step's amount, and a TIE
     * stepped the level mid-gate -- a discontinuity in the gain, which is a
     * click. Latched when the envelope enters ATTACK and held until IDLE.
     */
    pub step_level: f32,

    /// Published for the UI, computed once per block, because `get_param`
    /// runs on the audio callback too and must stay trivial.
    pub ms_per_step: f32,
    pub last_bpm: f32,
    /// "The playhead is moving" -- the UI's extrapolator is the only reader
    /// and is written against the concept, not against what drives it.
    pub advancing: bool,
    /// Adjacent ON steps hold as ONE gate instead of re-articulating.
    pub legato: bool,
    pub time_mode: TimeMode,
    pub curve: Curve,
    pub sample_rate: f64,

    /*
     * THE FADE-IN: HOW MUCH OF THE PATTERN HAS ARRIVED, 0..1.
     *
     * 0 is "no step sounds" and 1 is "all of them do", and the N on steps
     * arrive at equal intervals between the two -- step of rank r crosses at
     * exactly r/N. A build-up is this parameter automated, which is the whole
     * reason it is a host parameter and the pattern is not.
     *
     * 1.0 IS THE NEUTRAL VALUE and the default, so a fresh instance and every
     * patch saved before this existed sound exactly as they did. The golden
     * renders are what say so.
     */
    pub fade: f32,
    /*
     * Whether a step ARRIVES or APPEARS.
     *
     * Soft ramps the step in on its own level -- the same quantity a drag up
     * and down in a pad sets -- over its slice of the knob's travel. Hard
     * jumps it on at the end of that slice. They are one formula and a
     * threshold, so the two agree at every arrival boundary and the switch
     * reads as smoothing rather than as a second feature.
     */
    pub fade_soft: bool,
    /*
     * Which end the pattern is built up from. See [`FadeDir`]. In is the
     * default and the neutral one: it is what the gate did before the direction
     * existed, so every patch and both golden renders are unaffected.
     */
    pub fade_dir: FadeDir,
    /*
     * THE FADE'S WEIGHT PER STEP, CACHED.
     *
     * `next_gain` consults this twice a sample and a step's RANK costs a pass
     * over the pattern to find, so deriving it in the sample loop would put an
     * O(length) search inside an O(frames) one. Recomputed by `recalc_fade`
     * whenever anything it depends on moves -- the same arrangement
     * `ms_per_step` has, and for the same reason.
     */
    fade_w: [f32; MAX_STEPS],
    /*
     * The generator's state. A small xorshift rather than anything from a
     * library: `set_param` runs on the audio callback, where `rand()` is not
     * RT-safe in the strict sense, and the core has no dependencies to reach
     * for anyway.
     */
    rng: u32,
}

impl Instance {
    pub fn new(sample_rate: f64) -> Self {
        let mut me = Self {
            pat: (0..SLOTS).map(Pattern::new).collect(),
            slot: 0,
            rate_idx: rates::RATE_DEFAULT,
            /* The percentages that reproduce the old 2 / 20 / 20 ms defaults
             * against a full-width 1/16 step at 120 BPM, so a fresh instance
             * sounds as it always did. */
            attack: 1.6,
            decay: 16.0,
            sustain: 1.0,
            release: 16.0,
            hold: 1.0,
            amount: 1.0,
            cursor: 0,
            step_pos: 0.0,
            last_step: None,
            was_running: false,
            env: Env::default(),
            step_level: 0.0,
            ms_per_step: 0.0,
            last_bpm: 120.0,
            advancing: false,
            legato: false,
            time_mode: TimeMode::Ms,
            curve: Curve::Linear,
            sample_rate: if sample_rate > 0.0 { sample_rate } else { 44100.0 },
            /* The whole pattern, arriving as one -- which is the behaviour of
             * every version before the fade existed. */
            fade: 1.0,
            fade_soft: false,
            fade_dir: FadeDir::In,
            fade_w: [1.0; MAX_STEPS],
            /* A FIXED SEED, ADVANCED PER CALL. There is no entropy source in
             * here -- no clock, no I/O, by design -- so successive presses
             * differing is what the walk provides and reproducibility is what
             * the fixed start provides. A shell that wants a specific roll
             * passes its own seed to `randomize`. */
            rng: 0x9E37_79B9,
        };
        me.recalc_ms_per_step();
        me.recalc_fade();
        me
    }

    /*
     * THE FADE, AS ONE FORMULA WITH FOUR READINGS.
     *
     *     w(r) = clamp(f*n - (r-1), 0, 1)        soft
     *     w(r) = w_soft(r) >= 1 ? 1 : 0          hard
     *
     * n is how many steps of the ARRIVING KIND there are inside `length` and r
     * is a step's rank among them. Everything the control promises falls out of
     * it: f=0 leaves every weight at 0, f=1 leaves every one at 1, and rank r
     * crosses at f = r/n, so the arrivals are equidistant. In soft mode EXACTLY
     * ONE step is part-way in at any moment -- a one-slot window travelling up
     * the order -- which is the smoothest thing that is still evenly spaced.
     *
     * Hard is that value thresholded rather than a second rule, so the two agree
     * at every arrival boundary. A switch that moved the arrivals as well as
     * their shape would be two features wearing one name.
     *
     * WHAT IS WRITTEN HERE IS A LEVEL FACTOR, NOT THE WEIGHT, and that is what
     * makes the direction cost one line instead of a second code path.
     * `sounds()` and the latch in `on_step_boundary` read only this array, so
     * the whole of Fade Out is which factor it holds:
     *
     *     In    lf[i] = on(i) ? w(rank(i)) : 0
     *     Out   lf[i] = on(i) ? 1 : 1 - w(rank(i))
     *
     * An arriving HOLE therefore starts at level 1 -- an ordinary on step at
     * full level -- and soft ramps it DOWN to 0, which is a gap. The steps that
     * are not of the arriving kind sit at their finished value (0 for a hole
     * under In, 1 for a hit under Out) and the knob never touches them.
     *
     * THE EPSILON IS NOT COSMETIC. f arrives as a float from a host and the
     * value nearest below 1.0 is a real thing to be handed; without the slack,
     * the last arrival would stay put at the top of the knob, which is the one
     * setting a user is certain to check.
     */
    pub fn recalc_fade(&mut self) {
        let (fade, soft, dir) = (self.fade, self.fade_soft, self.fade_dir);
        let p = &self.pat[self.slot];
        let arriving_on = dir == FadeDir::In;
        let n = if arriving_on { p.hits() } else { p.holes() };
        let mut w = [0.0f32; MAX_STEPS];

        for i in 0..p.length.min(MAX_STEPS) {
            /* The finished value for a step the knob does not move. */
            if p.on(i) != arriving_on {
                w[i] = if p.on(i) { 1.0 } else { 0.0 };
                continue;
            }
            if n == 0 {
                continue;
            }
            let t = fade as f64 * n as f64;
            let r = p.order[i].max(1) as f64;
            let v = (t - (r - 1.0)).clamp(0.0, 1.0);
            let v = if soft {
                v as f32
            } else if v >= 1.0 - 1.0e-6 {
                1.0
            } else {
                0.0
            };
            /* In: the hit fades UP from nothing. Out: the hole fades the step
             * DOWN from a full one, and arriving means gone. */
            w[i] = if arriving_on { v } else { 1.0 - v };
        }
        self.fade_w = w;
    }

    /*
     * A STEP THE FADE HAS NOT REACHED IS A GAP, NOT A SILENT ON STEP.
     *
     * The difference is TIES and JOIN NEIGHBORS, both of which ask the mask
     * whether a step sounds in order to decide whether to hold a gate open
     * through it. Reading the raw mask there would let a step that has not
     * arrived keep its neighbour's gate open -- audible, and impossible to
     * explain from the picture. So every read of "does this step sound" on the
     * audio path goes through here, and hard mode is then literally the
     * pattern with the later steps removed.
     */
    #[inline]
    fn sounds(&self, i: usize) -> bool {
        i < MAX_STEPS && self.fade_w[i] > 0.0
    }

    /// One turn of xorshift32. Never returns 0 once seeded non-zero, which is
    /// the only state this generator cannot leave.
    #[inline]
    fn next_rand(&mut self) -> u32 {
        let mut x = self.rng;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        self.rng = x;
        x
    }

    /*
     * FILL A SLOT WITH A GATE WORTH HEARING.
     *
     * Sixteen coin flips reads as noise rather than as a trance gate, so the
     * hits are spread EVENLY over the pattern -- the Euclidean rhythm, by the
     * two-line test below rather than by Bjorklund's algorithm, which produces
     * the same spacing and needs a scratch array. It puts a hit on step 0 by
     * construction, so a random pattern always lands on the downbeat.
     *
     * The ARRIVAL ORDER is shuffled with it, and that is the half that makes
     * this worth having: position order can only ever fade in as a
     * left-to-right wipe, where a shuffled order is a build-up.
     *
     * TIES ARE CLEARED AND THE LEVELS GO BACK TO FULL. They are not randomised
     * -- an accent pattern nobody chose is harder to work with than none --
     * but they describe a pattern that no longer exists, so carrying them over
     * would leave a gate holding through a step that is now a gap.
     *
     * IT DOES NOT TOUCH THE PLAYHEAD. Not `step_pos`, not `last_step`, not
     * `env`, not `was_running`: pressing this mid-bar must change what the
     * gate plays, not when it plays it.
     */
    pub fn randomize(&mut self, slot: usize, seed: Option<u32>) {
        if slot >= SLOTS {
            return;
        }
        if let Some(s) = seed {
            /* Zero is xorshift's one dead state, so it is spelled as something
             * else rather than silently producing the same pattern forever. */
            self.rng = if s == 0 { 0x6C07_8965 } else { s };
        }
        let length = self.pat[slot].length.clamp(1, MAX_STEPS);

        /* A quarter to three quarters full. Below that a gate reads as an
         * accident and above it as no gate at all. */
        let lo = (length / 4).max(1);
        let hi = (length * 3 / 4).max(lo + 1);
        let hits = lo + (self.next_rand() as usize) % (hi - lo + 1);

        let p = &mut self.pat[slot];
        p.ties.clear();
        p.depth = [DEPTH_FULL; MAX_STEPS];
        for i in 0..MAX_STEPS {
            p.steps.set(i, false);
        }
        /*
         * THE EUCLIDEAN TEST, AND WHICH END OF THE GROUP THE HIT SITS AT.
         *
         *     hit(i)  <=>  (i * hits) mod length  <  hits
         *
         * The other spelling of this -- comparing floor((i+1)*h/n) with
         * floor(i*h/n) -- spreads the hits identically and puts each one at the
         * END of its group, which leaves step 0 EMPTY at every density below
         * full. A random gate that never lands on the downbeat is the one thing
         * this generator is supposed to guarantee, and it reads as the
         * randomiser being broken rather than as an off-by-one.
         */
        for i in 0..length {
            p.steps.set(i, (i * hits) % length < hits);
        }

        /* Fisher-Yates over the ranks, in place over the step indices that
         * carry them. The borrow ends before the generator is asked for the
         * next number, which is why the ranks are collected first. */
        let mut idx = [0usize; MAX_STEPS];
        let mut n = 0;
        for i in 0..length {
            if self.pat[slot].on(i) {
                idx[n] = i;
                n += 1;
            }
        }
        for i in 0..n {
            self.pat[slot].order[idx[i]] = (i + 1) as u8;
        }
        for i in (1..n).rev() {
            let j = (self.next_rand() as usize) % (i + 1);
            let (a, b) = (idx[i], idx[j]);
            let t = self.pat[slot].order[a];
            self.pat[slot].order[a] = self.pat[slot].order[b];
            self.pat[slot].order[b] = t;
        }
        self.pat[slot].renumber();
        self.recalc_fade();
    }

    #[inline]
    pub fn pattern(&self) -> &Pattern {
        &self.pat[self.slot]
    }

    /// One step's duration in ms, at the rate and tempo currently known.
    /// Called whenever either changes, so the `ui` readout never reports a
    /// stale one -- a value that only appeared after the first block meant a
    /// blank panel on open.
    pub fn recalc_ms_per_step(&mut self) {
        let bpm = if self.last_bpm > 1.0 { self.last_bpm as f64 } else { 120.0 };
        let sr = if self.sample_rate > 0.0 { self.sample_rate } else { 44100.0 };
        let mut samples = (60.0 / bpm) * sr * rates::RATES[self.rate_idx].beats;
        if samples < 1.0 {
            samples = 1.0;
        }
        self.ms_per_step = (samples * 1000.0 / sr) as f32;
    }

    /// How long the gate is open for, in ms: Width of a step. The unit every
    /// envelope stage is measured in.
    #[inline]
    pub fn width_ms(&self) -> f64 {
        self.hold as f64 * self.ms_per_step as f64
    }

    /// How long a stage lasts, in samples. `value` is a percentage of the
    /// gate's WIDTH, 0..200.
    #[inline]
    fn stage_samples(&self, value: f32) -> f64 {
        value as f64 * 0.01 * self.width_ms() * (self.sample_rate / 1000.0)
    }

    #[inline]
    fn lens(&self) -> StageLens {
        StageLens {
            attack: self.stage_samples(self.attack),
            decay: self.stage_samples(self.decay),
            release: self.stage_samples(self.release),
            sustain: self.sustain,
        }
    }

    /// The playhead's position in the pattern, 0..1. Cheap and
    /// allocation-free -- for a caller on the audio thread, where the
    /// equivalent formatted readout's `snprintf` does not belong.
    pub fn phase01(&self) -> f64 {
        let length = self.pattern().length.max(1) as f64;
        let mut pos = self.step_pos % length;
        if pos < 0.0 {
            pos += length;
        }
        pos / length
    }

    /*
     * A step boundary. This is the whole of what a tie means: an ON step
     * arriving on top of a held ON step does NOT restart the envelope.
     */
    fn on_step_boundary(&mut self, prev_step: Option<usize>, new_step: usize) {
        let l = self.lens();
        /* THE FADE'S WEIGHTS ARE READ BEFORE THE PATTERN IS BORROWED, not
         * because the borrow checker insists but because `sounds` consults
         * `self` and `p` holds a shared borrow of it. Three reads, named. */
        let on_now = self.sounds(new_step);
        let on_prev = prev_step.map_or(false, |s| self.sounds(s));
        let w_now = if new_step < MAX_STEPS { self.fade_w[new_step] } else { 0.0 };
        let p = &self.pat[self.slot];
        let tied = prev_step.map_or(false, |s| p.tied(s));

        if on_now {
            /*
             * LEGATO IS "TREAT EVERY ADJACENT PAIR AS TIED".
             *
             * At Sustain 100% this changes nothing audible, because attack
             * already ramps from wherever the envelope is and there is
             * nowhere to ramp from a fully open gate. The difference appears
             * BELOW 100%, which is where a retrigger actually re-articulates.
             */
            if !(on_prev && (tied || self.legato)) {
                /* THE FADE SCALES THE STEP'S LEVEL, AND IT SCALES IT HERE.
                 *
                 * This is the one place the struck step's level is read, and
                 * it is latched for the whole gate -- so a step whose arrival
                 * completes mid-gate does not step its own level and click.
                 * The knob is smooth ACROSS steps rather than within one,
                 * which is what "the fields are introduced one by one" means.
                 *
                 * A weight of 0 never reaches this line: `on_now` is already
                 * false there, so the step is a gap. */
                let lvl = p.depth[new_step] as f32 * (1.0 / 255.0) * w_now;

                /*
                 * THE ATTACK STARTS WHERE THE GAIN IS, NOT WHERE `env` IS.
                 *
                 * `env` is only half the gain: what you hear is `env * level`,
                 * and the level changes at this very boundary. `att_from`
                 * carries `env` across, so after a half-filled pad the
                 * envelope resumed at the right ENV and instantly the wrong
                 * GAIN -- the click the att_from ramp was added to prevent,
                 * arriving through the other factor.
                 *
                 * `env` may land ABOVE 1 -- a loud pad followed by a quiet
                 * one -- and that is not a special case: the attack is a lerp
                 * from att_from to 1, so it ramps DOWN to the new level over
                 * the attack time instead of up.
                 */
                let gain_now = self.env.level as f64 * self.step_level as f64;
                self.step_level = lvl;
                self.env.level = if lvl > 1.0e-6 {
                    (gain_now / lvl as f64) as f32
                } else {
                    0.0
                };
                self.env.enter(Stage::Attack, &l);
            }
        } else if on_prev || self.env.stage != Stage::Idle {
            self.env.enter(Stage::Release, &l);
        }
    }

    /// The gain for one sample, advancing every bit of state that depends on
    /// it.
    #[inline]
    fn next_gain(&mut self, r: &mut Run) -> f32 {
        if Some(r.step) != self.last_step {
            let prev = self.last_step;
            self.on_step_boundary(prev, r.step);
            self.last_step = Some(r.step);
        }

        /*
         * Gate length: release inside the step, not only at its edge. A tie
         * means "hold through", so shortening it would contradict the tie --
         * and JOIN NEIGHBORS is "every adjacent ON pair is tied", so it has
         * to hold through for exactly the same reason.
         *
         * Leaving legato out of this rule made it wrong in both directions at
         * once. Below Width 100% the envelope released inside every step,
         * reached Idle, and then found no attack waiting at the boundary
         * because `on_step_boundary` had suppressed it. Idle is ABSORBING --
         * this block's own guard excludes it -- so the gate shut after step 0
         * and the pattern was SILENT from there on. At Width 100% this block
         * never runs at all, and at Sustain 100% a retrigger ramps from 1.0
         * to 1.0, so there the switch did nothing audible. Between them that
         * covered nearly every setting anyone would reach for, which is why
         * it read as broken.
         *
         * The SUCCESSOR is consulted, not the current step's tie bit, because
         * that is what "adjacent ON pair" means. A next step that is OFF
         * still closes the gate, which is what keeps this distinct from a
         * tie.
         */
        if self.hold < 1.0
            && self.env.stage != Stage::Release
            && self.env.stage != Stage::Idle
        {
            let next = if r.length > 0 { (r.step + 1) % r.length } else { r.step };
            /* The FADED mask on both reads. A step the fade has not reached is
             * a gap, and Join Neighbors must not bridge to a gap -- see
             * `sounds`. */
            let (here, there) = (self.sounds(r.step), self.sounds(next));
            let p = &self.pat[self.slot];
            let held = here && (p.tied(r.step) || (self.legato && there));
            if r.frac >= self.hold as f64 && !held {
                let l = self.lens();
                self.env.enter(Stage::Release, &l);
            }
        }

        let l = self.lens();
        self.env.advance(self.curve, &l);

        /* The step's amount is how far the gate OPENS, not how far it closes:
         *     m = 1 - amount * (1 - env * level)
         * level 0 is silent, an OFF step is a gap whatever its level (env is
         * 0 there, so the term vanishes), and the global amount is the
         * dry/wet.
         *
         * `step_level` is the STRUCK step's, latched at gate-open -- not
         * `depth[r.step]`, which is a different number the moment a release
         * or a tie outlives the step that started it. */
        let m = 1.0 - self.amount * (1.0 - self.env.level * self.step_level);

        self.step_pos += r.inc;
        r.frac += r.inc;
        while r.frac >= 1.0 {
            r.frac -= 1.0;
            r.step += 1;
            if r.step >= r.length {
                r.step = 0;
            }
        }
        while r.frac < 0.0 {
            r.frac += 1.0;
            if r.step == 0 {
                r.step = r.length - 1;
            } else {
                r.step -= 1;
            }
        }
        m
    }

    /*
     * PHASE IS ANCHORED PER BLOCK AND ADVANCED PER SAMPLE.
     *
     * The host's beat position is interpolated per block and its intra-tick
     * fraction is CLAMPED, so a late tick freezes phase instead of
     * overshooting. Differencing it per sample therefore renders that plateau
     * as an audible stutter on every late clock tick. Instead the host's
     * answer is an anchor that a local accumulator is pulled gently towards
     * -- a phase-locked loop, not a clock divider.
     *
     * Returns `None` when the block needs no gain applied at all, having
     * already advanced whatever state the next block depends on. The caller
     * leaves the buffer untouched, which is exactly right: the dry signal is
     * already in it.
     */
    fn block_setup(&mut self, frames: usize, t: Option<&Transport>) -> Option<Run> {
        let length = self.pattern().length.clamp(1, MAX_STEPS);

        let mut bpm = 120.0f32;
        if let Some(t) = t {
            if t.bpm > 1.0 && t.bpm < 1000.0 {
                bpm = t.bpm;
            }
        }

        let beats_per_step = rates::RATES[self.rate_idx].beats;
        let mut samples_per_step = (60.0 / bpm as f64) * self.sample_rate * beats_per_step;
        if samples_per_step < 1.0 {
            samples_per_step = 1.0;
        }
        let mut inc = 1.0 / samples_per_step;

        /* A stopped transport is not beat 0, it is no beat at all. */
        let beats = match t {
            Some(t) if t.running => t.beats,
            _ => -1.0,
        };
        let running = beats >= 0.0;

        self.last_bpm = bpm;
        self.ms_per_step = (samples_per_step * 1000.0 / self.sample_rate) as f32;
        self.advancing = running;

        if running {
            let target = beats / beats_per_step;
            if !self.was_running {
                /* Transport just started: land exactly, do not glide in. */
                self.step_pos = target;
                self.last_step = None;
            } else {
                let err = target - self.step_pos;
                if err > RESYNC_STEPS || err < -RESYNC_STEPS {
                    self.step_pos = target; /* loop, seek or tempo jump */
                    /* A jump re-evaluates the step even when it lands on the
                     * same index: the boundary test is `step != last_step`,
                     * so a seek back onto the step we were already on would
                     * fire nothing and leave the envelope where it was. */
                    self.last_step = None;
                } else {
                    inc += (err * TRACK_GAIN) / frames as f64;
                }
            }
        } else {
            /* Stopped means open: hold the gate open and park at step 0, so
             * the next start is a downbeat rather than wherever the pattern
             * stopped. */
            self.step_pos = 0.0;
            self.last_step = None;
            self.env.stage = Stage::Idle;
            self.env.level = 0.0;
            self.was_running = false;
            return None;
        }
        self.was_running = running;

        /* Amount zero is a true bypass -- m collapses to exactly 1.0 -- so do
         * not spend a block proving it. The phase still advances, so turning
         * it back up lands on the step the pattern would have reached. */
        if self.amount <= 0.0 {
            self.step_pos += inc * frames as f64;
            return None;
        }

        let frac = self.step_pos - self.step_pos.floor();
        let mut step = (self.step_pos.floor() % length as f64) as i64;
        if step < 0 {
            step += length as i64;
        }
        Some(Run {
            length,
            inc,
            frac,
            step: step as usize,
        })
    }

    /*
     * ONE SET OF MATHS, THREE BUFFER FORMATS.
     *
     * Move hands us int16 interleaved; VST3 and AU hand us float, usually as
     * separate channel pointers. Writing the loop three times would mean
     * three places for the gain law to drift, and the drift would be
     * inaudible until somebody A/B'd the plugin against the hardware.
     */
    pub fn process_i16(&mut self, lr: &mut [i16], frames: usize, t: Option<&Transport>) {
        let Some(mut r) = self.block_setup(frames, t) else { return };
        for i in 0..frames {
            let m = self.next_gain(&mut r);
            let l = lr[i * 2] as f32 * m;
            let rr = lr[i * 2 + 1] as f32 * m;
            lr[i * 2] = l.clamp(-32768.0, 32767.0) as i16;
            lr[i * 2 + 1] = rr.clamp(-32768.0, 32767.0) as i16;
        }
    }

    pub fn process_f32(&mut self, lr: &mut [f32], frames: usize, t: Option<&Transport>) {
        let Some(mut r) = self.block_setup(frames, t) else { return };
        for i in 0..frames {
            let m = self.next_gain(&mut r);
            lr[i * 2] *= m;
            lr[i * 2 + 1] *= m;
        }
    }

    /// No clamping on the float paths, deliberately: the gate only ever
    /// ATTENUATES (m is in 0..1), so it cannot push a signal out of range,
    /// and a plugin host is entitled to headroom above 1.0 we must not steal.
    pub fn process_f32_split(
        &mut self,
        l: &mut [f32],
        rch: &mut [f32],
        frames: usize,
        t: Option<&Transport>,
    ) {
        let Some(mut r) = self.block_setup(frames, t) else { return };
        for i in 0..frames {
            let m = self.next_gain(&mut r);
            l[i] *= m;
            rch[i] *= m;
        }
    }
}

/// Per-block state the sample loop walks.
struct Run {
    length: usize,
    inc: f64,
    frac: f64,
    step: usize,
}
