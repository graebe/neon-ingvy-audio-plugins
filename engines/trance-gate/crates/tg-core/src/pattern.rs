/*!
The pattern: its steps, ties and levels, the fade's arrival order over them, and
the two things that rewrite a slot wholesale -- the fade's weight table and the
randomiser.
*/

use crate::mask::Mask;
use crate::{FadeDir, Instance, DEPTH_FULL, MAX_STEPS, SLOTS};

#[derive(Clone)]
pub struct Pattern {
    pub(crate) steps: Mask,
    pub(crate) ties: Mask,
    /// 1..=MAX_STEPS
    pub(crate) length: usize,
    /// Per-step level, 0..255. An ACCENT: the global Amount scales the whole
    /// sequence on top of it, so this says "how much of the gate" and Amount
    /// says "how much gating". 255 is the neutral value, which is why a v1
    /// blob without the array must fill it rather than zero it.
    pub(crate) depth: [u8; MAX_STEPS],
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
    pub(crate) order: [u8; MAX_STEPS],
}

impl Pattern {
    pub(crate) fn new(slot: usize) -> Self {
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

    /// How many steps the pattern runs, 1..=MAX_STEPS.
    #[inline]
    pub fn length(&self) -> usize {
        self.length
    }

    /// Step `i`'s level, 0..255; `None` past MAX_STEPS.
    #[inline]
    pub fn depth(&self, i: usize) -> Option<u8> {
        self.depth.get(i).copied()
    }

    /// Step `i`'s rank among the steps of its own kind; `None` past MAX_STEPS.
    #[inline]
    pub fn order(&self, i: usize) -> Option<u8> {
        self.order.get(i).copied()
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

impl Instance {
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
    pub(crate) fn sounds(&self, i: usize) -> bool {
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
}
