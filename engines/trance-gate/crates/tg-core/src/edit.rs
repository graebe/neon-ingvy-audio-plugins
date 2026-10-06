// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
One `set_param`, read.

The string door does two things: it reads a value the way C would -- `atof`,
`atoi`, labels and names -- and it applies it. A plugin shell must not do the
first on its audio thread: a parser has no place in a callback, and the view a
shell replays its edits through has to apply exactly the values the engine
does. So the two halves are apart:

```text
key, value --Edit::parse--> Edit --Instance::apply_edit--> the engine
```

[`Edit::parse`] runs wherever parsing is allowed and [`Instance::apply_edit`]
applies the result with no text in sight -- the same split the state blob has
(`Patch::parse` and `Instance::load`). [`Instance::set_param`] is the two in
one call, for the Move, which has one thread and calls it on its audio
callback, as it always has.

`state` is the one key that is not an edit. A whole patch has its own reader,
[`Patch`](crate::state::Patch), and a shell hands that over instead.
*/

use crate::fmt;
use crate::mask::Mask;
use crate::params::{randomize_holds, set_pattern_hex, Param};
use crate::{rates, Instance, MAX_STEPS};

/// One `set_param` key and value, read: ready for [`Instance::apply_edit`].
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Edit {
    /// One of the fifteen automatable values, on the numeric wire that
    /// [`Instance::set_num`] takes.
    Num(Param, f64),
    /// The edit position: an option index, clamped to the slot's length as
    /// it lands.
    Cursor(usize),
    /// The step at the cursor: 0 off, 1 on, 2 tied.
    Step(u8),
    /// The level of the step at the cursor, 0..1.
    StepAmount(f32),
    /// The step at the cursor's place in the arrival order, from 1.
    StepOrder(usize),
    /// A roll of the current slot. A seed pins it; `None` walks the
    /// instance's own generator.
    Randomize(Option<u32>),
    /// The current slot's steps, as a whole new mask.
    Pattern(Mask),
    /// The current slot's ties.
    Ties(Mask),
}

impl Edit {
    /// `key` and `val` as `set_param` has always read them, or `None` for a
    /// key that is not an edit -- `state`, or one this engine does not know
    /// -- and for a `randomize` that holds. Allocation-free.
    pub fn parse(key: &str, val: &str) -> Option<Edit> {
        /* The switches take their names as well as 0|1: the Move wires them
         * by index while a patch or a plugin may well say what it means. */
        let named = |a: &str, b: &str| val == a || val == b || fmt::atoi(val) != 0;
        let switch = |on: bool| on as i32 as f64;
        Some(match key {
            /* The automatable keys read their value and nothing else --
             * `set_num` owns every clamp and every side effect, so the
             * numeric and string doors cannot drift. */
            "slot" => Edit::Num(Param::Slot, fmt::atoi(val) as f64),
            "length" => Edit::Num(Param::Length, fmt::atoi(val) as f64),
            /* A LABEL first, a bare number as an index -- `rates::index_from`
             * owns that convention. */
            "rate" => Edit::Num(Param::Rate, rates::index_from(val) as f64),
            "attack" => Edit::Num(Param::Attack, fmt::atof(val)),
            "decay" => Edit::Num(Param::Decay, fmt::atof(val)),
            "sustain" => Edit::Num(Param::Sustain, fmt::atof(val)),
            "hold" => Edit::Num(Param::Hold, fmt::atof(val)),
            "release" => Edit::Num(Param::Release, fmt::atof(val)),
            "amount" => Edit::Num(Param::Amount, fmt::atof(val)),
            "fade" => Edit::Num(Param::Fade, fmt::atof(val)),
            "fade_soft" => Edit::Num(Param::FadeSoft, switch(named("On", "on"))),
            "fade_dir" => Edit::Num(Param::FadeDir, switch(named("Out", "out"))),
            "legato" => Edit::Num(Param::Legato, switch(named("On", "on"))),
            "time_mode" => Edit::Num(
                Param::TimeMode,
                switch(val == "%" || val == "Step" || val == "step" || fmt::atoi(val) != 0),
            ),
            /* Names as well as the index; the re-anchor that keeps a mid-gate
             * change from clicking lives in `set_num` with the rest. */
            "curve" => Edit::Num(
                Param::Curve,
                match val {
                    "Exponential" | "Exp" => 1,
                    "S-Curve" | "S" => 2,
                    _ => fmt::atoi(val) as i32,
                } as f64,
            ),
            /*
             * THE WIRE CARRIES THE OPTION INDEX, and the option NAMES carry the
             * step numbers -- so index 15 displays as "16".
             *
             * Not the 1-based name with `options_as_string`: the host has
             * three resolvers for an enum's wire format and only two consult
             * that flag. Indices are its default convention, so all three
             * agree on them.
             */
            "cursor" => Edit::Cursor(fmt::atoi(val).max(0) as usize),
            /*
             * THE ONE KNOB THAT EDITS THE PATTERN, three-state rather than two
             * because a tie is not a separate property of a step -- it is the
             * third thing a step can be. Off / On / Tie maps onto the two
             * bits, costs one knob instead of two, and cannot express the
             * meaningless fourth combination (tied while off).
             */
            "step" => Edit::Step(match val {
                "Off" => 0,
                "On" => 1,
                "Tie" => 2,
                _ => fmt::atoi(val).clamp(0, 2) as u8,
            }),
            "step_amount" => Edit::StepAmount((fmt::atof(val) as f32).clamp(0.0, 1.0)),
            /*
             * THE STEP'S PLACE IN THE ARRIVAL ORDER, 1..=N, at the cursor --
             * the same door `step_amount` uses, for the same reason: per-step
             * state is never a host parameter, so it comes through here.
             */
            "step_order" => Edit::StepOrder(fmt::atoi(val).max(1) as usize),
            /*
             * AN ACTION, NOT A VALUE, which is why it is only here and has no
             * `Param` of its own: a host parameter that regenerated the pattern
             * every time the host rewrote it would be unusable.
             *
             * AND IT NEEDS A VALUE THAT DOES NOTHING. On the Move this is an
             * enum knob, which writes whichever option it is turned to -- so
             * "Hold" has to be expressible, or turning the knob back off would
             * roll again. An empty value fires: that is the plugin's path,
             * where a button press carries no payload at all.
             *
             * A POSITIVE number is a SEED and pins the roll, which is what
             * makes the result testable. Anything else that is not a hold --
             * "Roll", from the Move's own knob -- walks the instance's
             * generator instead, so successive presses differ.
             */
            "randomize" => {
                if randomize_holds(val) {
                    return None;
                }
                let n = fmt::atoi(val);
                Edit::Randomize(if n > 0 { Some(n as u32) } else { None })
            }
            "pattern" => Edit::Pattern(mask(val)),
            "ties" => Edit::Ties(mask(val)),
            _ => return None,
        })
    }
}

fn mask(hex: &str) -> Mask {
    let mut m = Mask::new();
    set_pattern_hex(&mut m, hex);
    m
}

impl Instance {
    /// Apply one read edit. Allocation-free, and no text: [`Edit::parse`]
    /// read it, here or on another thread.
    pub fn apply_edit(&mut self, edit: &Edit) {
        /* Every edit may change the saved state -- the pattern ones always do
         * -- and none is applied per block, so the revision simply moves. */
        self.rev = self.rev.wrapping_add(1);
        let slot = self.slot;
        let c = self.cursor;
        match *edit {
            Edit::Num(param, value) => self.set_num(param, value),
            Edit::Cursor(at) => {
                let len = self.pat[slot].length;
                self.cursor = if at >= len { len - 1 } else { at };
            }
            Edit::Step(mode) => {
                if c < MAX_STEPS {
                    let was = self.pat[slot].on(c);
                    self.pat[slot].steps.set(c, mode != 0);
                    self.pat[slot].ties.set(c, mode == 2);
                    /*
                     * A STEP CHANGING KIND ARRIVES LAST IN ITS NEW ONE, and the
                     * kind it left closes up behind it. Symmetric on purpose:
                     * a hole is as much a thing that arrives as a hit is, so
                     * switching a step off puts it at the end of the hole order
                     * rather than wherever its old hit rank happens to land.
                     *
                     * On <-> Tie does not move a step at all -- a tie changes
                     * what a live step does, not when it arrives.
                     */
                    if (mode != 0) != was {
                        self.pat[slot].order_append(c);
                    } else {
                        self.pat[slot].renumber();
                    }
                    self.recalc_fade();
                }
            }
            Edit::StepAmount(level) => {
                if c < MAX_STEPS {
                    self.pat[slot].depth[c] = (level * 255.0 + 0.5) as u8;
                }
            }
            Edit::StepOrder(rank) => {
                self.pat[slot].order_set(c, rank);
                self.recalc_fade();
            }
            Edit::Randomize(seed) => self.randomize(slot, seed),
            Edit::Pattern(steps) => {
                self.pat[slot].steps = steps;
                /* A WHOLE NEW MASK, so every rank it might have had is stale --
                 * see `reseed_order`. Position order is what a pattern that
                 * arrived in one piece should fade in as, and it is also what
                 * makes the same mask written twice land on the same order. */
                self.pat[slot].reseed_order();
                self.recalc_fade();
            }
            Edit::Ties(ties) => self.pat[slot].ties = ties,
        }
    }
}

#[cfg(test)]
mod tests;
