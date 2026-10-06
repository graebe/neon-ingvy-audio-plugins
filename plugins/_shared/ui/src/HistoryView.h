// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What a view of a NoteHistory shares: the window of time it shows, and how a
 * time becomes a place.
 *
 * NOW IS AT THE RIGHT EDGE and the past scrolls left, `span` quarters across
 * the plot: the area right of the view's gutter, where a GrandStaff keeps its
 * clefs and a PianoRoll its note numbers. Bar lines are line-200 at every bar
 * the clock reports, and now is a 1px uv-deep line at the right.
 *
 * It paints on whatever its owner put under it -- a well -- and repaints when
 * told: an editor ticks it from its FrameClock. Display only.
 */
#pragma once

#include "NoteHistory.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ni::ui
{

class HistoryView : public juce::Component
{
public:
    explicit HistoryView (const NoteHistory&);

    /* How many quarters the plot shows. */
    void setSpan (double quarters);
    double getSpan() const noexcept { return span; }

    /* The plot: right of the gutter, inside a 1px inset. */
    juce::Rectangle<float> plot() const;

    /* Where a time on the clock is across the plot; may be off either end. */
    float xOf (double quarters) const;

    /* The oldest time the plot shows. */
    double windowStart() const;

protected:
    /* The width the view keeps at the left for its own marks. */
    virtual float gutter() const = 0;

    void paintBarLines (juce::Graphics&, float top, float bottom) const;
    void paintNow (juce::Graphics&) const;

    const NoteHistory& history;

private:
    double span = 16.0;

    JUCE_DECLARE_NON_COPYABLE (HistoryView)
};

} // namespace ni::ui
