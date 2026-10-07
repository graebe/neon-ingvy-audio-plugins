// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A keyboard that shows which keys are held: whole octaves from a C, the held
 * keys lit, the lowest one marked underneath.
 *
 *   white key   bg-300, a 1px gap to the next; each C labelled (C2, C3 ...)
 *               in the hint style, ink-dim
 *   black key   bg-000 on a line-200 hairline, three fifths of the key's
 *               length and of a white key's width
 *   lit         uv; a lit C's label in on-uv
 *   dimmed      uv-deep: what was held and is being kept, not what sounds
 *   bass        a 3px bar under the lowest lit key, in the lit colour
 *
 * DISPLAY ONLY. It takes no pointer and no keyboard focus: it reports what is
 * held, and a key that looked pressable would invite a press that does
 * nothing. Its accessible description is its owner's to set, in the owner's
 * note names.
 *
 * Which octaves it shows is the owner's choice too; lowestToShow() is the
 * usual one: stay put while the notes fit, otherwise move to the C below the
 * lowest note.
 */
#pragma once

#include "Music.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ni::ui
{

class Keyboard : public juce::Component
{
public:
    struct State
    {
        /* The C the keyboard starts on, and how many octaves it shows. */
        int lowest = 36;
        int octaves = 4;
        music::NoteSet lit;
        /* The note marked underneath, or -1. */
        int bass = -1;
        bool dimmed = false;

        bool operator== (const State& o) const
        {
            return lowest == o.lowest && octaves == o.octaves && lit == o.lit && bass == o.bass && dimmed == o.dimmed;
        }
        bool operator!= (const State& o) const { return ! (*this == o); }
    };

    Keyboard();

    /* What it shows; repaints only when that changed. */
    void setState (const State&);
    const State& getState() const noexcept { return state; }

    /* Where a key is, in its own coordinates; empty for a note it does not
     * show. */
    juce::Rectangle<float> keyBounds (int midi) const;

    /* `current` while every note of `notes` is inside its octaves, otherwise
     * the C at or below the lowest note -- no higher than the C whose octaves
     * just reach 127, so the top notes can always be shown. */
    static int lowestToShow (const music::NoteSet& notes, int octaves, int current);

    /* Under the keys: the gap and the bass mark. */
    static constexpr float markSpace = 6.0f;

    void paint (juce::Graphics&) override;

private:
    float whiteWidth() const;
    float keysHeight() const;
    bool shows (int midi) const;

    State state;

    JUCE_DECLARE_NON_COPYABLE (Keyboard)
};

} // namespace ni::ui
