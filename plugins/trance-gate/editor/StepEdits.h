// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Editing a step -- the one implementation of it, for the pads, the ring and
 * the keyboard alike: the web editor's lib/steps.js, natively.
 *
 * THE RULES, and what each is for (every one is a fix for something that read
 * as the click having half-failed):
 *
 *   - A CLICK ACTIVATES FULLY, wherever in the pad it lands: off goes on,
 *     on (or a tie) goes off.
 *   - SHIFT CYCLES INTO TIE, because a tie is the third thing a step can be:
 *     Shift turns an on step into a tie, and a tie (or an off step) on.
 *   - ACTIVATING A DEAD STEP RESTORES ITS FULL AMOUNT. The amount is kept
 *     apart from the mask, so a step dragged down to 20 % would come back at
 *     20 %. Off to on only: on and tie are both live, and an amount set on
 *     purpose stays.
 *   - A DRAG SETS THE AMOUNT, after the StepGrid's 4px of dead zone; ZERO
 *     MEANS OFF -- at 2 % or less the step goes off rather than staying on
 *     and silent, and coming back up switches it on again.
 *   - THE RING PAINTS: a sweep gives every wedge it crosses the state the
 *     press gave the first, never toggling each in turn. A wedge has an angle
 *     and no height, so there is no amount for a drag to mean there.
 *
 * THE GESTURE REMEMBERS WHAT IT DID. A command shows in a later snapshot, so
 * the pattern read during a drag is the one from before it; whether the step
 * being dragged is on is this class's to know, not the snapshot's.
 *
 * SET ORDER. While it is on, a press names the next arrival instead of editing
 * the step: the first step pressed arrives first, and so on. Only the kind
 * the fade brings in can be named -- the steps drawn on, or under Fade Out the
 * holes -- so a press on the other kind does nothing, and a step already
 * named in this pass keeps its place (the web editor re-ranked it, which
 * swapped it with whatever held the next place). The engine keeps the order a
 * permutation: a rank already taken swaps. Turning the mode on or off starts
 * a new pass. Typing a rank into a pad's number works at any time.
 *
 * Message thread.
 */
#pragma once

#include "Model.h"

#include <array>

namespace ni::tg
{

class StepEdits final
{
public:
    /* At or below this a dragged amount is off. */
    static constexpr float offAmount = 0.02f;

    explicit StepEdits (Model&);

    /* ---- the pointer: the pads and the ring */

    /* A press on step `index`: an edit, or a name in Set order. True when a
     * drag or a sweep may follow -- not in Set order. */
    bool press (int index, bool shift);
    /* A pad's drag, the amount 0..1 from the pointer's height. */
    void drag (int index, float amount);
    /* The ring's sweep onto another wedge. */
    void sweep (int index);

    /* ---- the keyboard: the pads */

    /* Space or Enter: what a press does (Shift: a tie). */
    void toggle (int index, bool tie);
    /* Alt with Up or Down: the amount by `delta`. */
    void nudge (int index, float delta);

    /* ---- the fade's order */

    /* A rank typed into a pad's number; text that is not a whole number of
     * at least 1 changes nothing. */
    void typeRank (int index, const juce::String& typed);
    void shuffle();

    bool ordering() const noexcept { return orderMode; }
    /* Set order on or off; either way a new pass. */
    void setOrdering (bool on);
    /* How many steps this pass has named, and whether `index` is one. */
    int namedCount() const noexcept { return named; }
    bool isNamed (int index) const;

    /* Whether the fade brings steps in from the holes (Fade Dir: Out). */
    bool fadeOut() const;
    /* Whether step `index` is of the kind the fade brings in, and how many
     * of the pattern's steps are. */
    bool arriving (int index) const;
    int arrivals() const;

private:
    StepMode modeOf (int index) const;
    /* The press's edit: the mode it set. */
    StepMode cycle (int index, bool tie);
    /* Set order's press; false when the mode is off. */
    bool name (int index);

    Model& model;

    bool orderMode = false;
    std::array<bool, maxSteps> namedSteps {};
    int named = 0;

    /* The gesture under way: what the press set, and whether the dragged
     * step is on now. */
    StepMode pressed = StepMode::off;
    bool dragOn = false;
};

} // namespace ni::tg
