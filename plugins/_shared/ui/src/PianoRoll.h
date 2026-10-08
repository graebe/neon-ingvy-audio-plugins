// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A NoteHistory as MIDI: one line per note, stacked and numbered, each note a
 * bar along its line for as long as it sounded.
 *
 *   lines       one per MIDI note over the range played in the window, two
 *               either side and never fewer than 24; a black key's line on
 *               bg-100, a C's with a line-200 rule under it
 *   numbers     at each C in the gutter, its name and number ("C3 48") in the
 *               hint style, ink-dim
 *   a note      a bar the line's height less 1px: uv while it sounds, ink-dim
 *               after
 *
 * The range follows what was played, so it moves as the music does; the
 * names are the owner's (the Namer), as the key spells them.
 */
#pragma once

#include "HistoryView.h"

#include <functional>

namespace ni::ui
{

class PianoRoll : public HistoryView
{
public:
    using Namer = std::function<juce::String (int midi)>;

    explicit PianoRoll (const NoteHistory&);

    void setNamer (Namer);

    /* The notes it shows, lowest and highest: the played range in the window,
     * two either side, at least `minimumLines`, inside 0 to 127. */
    std::pair<int, int> lines() const;

    /* Where a note's line is. */
    juce::Rectangle<float> lineOf (int midi) const;

    static constexpr int minimumLines = 24;

    void paint (juce::Graphics&) override;

protected:
    float gutter() const override { return 44.0f; }

private:
    juce::String nameOf (int midi) const;

    Namer namer;
};

} // namespace ni::ui
