// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What is sounding, in words: the window's one readout and what goes with it.
 *
 *   name          the readout style (28/32, weight 500), ink -- "Am7/C"; when
 *                 nothing sounds, a dash in ink-dim
 *   degree        beside it, a 24px chip on bg-200 and a line-200 hairline,
 *                 the roman numeral in the value style, uv -- "vi7"
 *   held          at the right end, an LED lit uv and HELD: the name is kept
 *                 by Hold, not sounding (LED, not switch: the plugin reports it)
 *   description   under it, the label style (capitals, tracked), ink-muted --
 *                 "A MINOR 7 · 1ST INVERSION"
 *   notes         NOTES and the notes in the value style, ink
 *   also          ALSO and each other reading as a chip on a line-100
 *                 hairline, ink-muted; a dash when there is none
 *
 * DISPLAY ONLY, and every word is the owner's: it formats nothing, so the
 * engine's spelling is the one shown.
 */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ni::ui
{

class ChordReadout : public juce::Component
{
public:
    struct State
    {
        juce::String name, degree, description, notes;
        juce::StringArray alternatives;
        bool held = false;

        bool operator== (const State& o) const
        {
            return name == o.name && degree == o.degree && description == o.description && notes == o.notes
                && alternatives == o.alternatives && held == o.held;
        }
        bool operator!= (const State& o) const { return ! (*this == o); }
    };

    ChordReadout();

    void setState (const State&);
    const State& getState() const noexcept { return state; }

    /* The height its rows take. */
    static constexpr int idealHeight = 136;

    void paint (juce::Graphics&) override;

private:
    State state;

    JUCE_DECLARE_NON_COPYABLE (ChordReadout)
};

} // namespace ni::ui
