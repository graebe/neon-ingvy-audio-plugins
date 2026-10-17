// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A time ruler in bars and beats over a well that pans (docs/adr/0006): the
 * strip across a well's top that says where in the song the view is.
 *
 * THE STRIP, `height` tall: bg-000, a line-100 hairline along its foot, a
 * line-200 tick at each bar with the bar's number beside it in the hint
 * style, ink-muted. Zoomed out until bars would crowd, it keeps every second bar, every
 * fourth, and so on (ZoomMath.h's grid): a bar line is always a bar line, and
 * a number is never printed over another.
 *
 * THE GRID UNDER IT is the well's to draw, from the same lines (lines()): bar
 * lines line-200 the well's height, beat lines line-100 below the ruler.
 *
 * A DRAG ON THE RULER PANS: onPan with the view's new start, in the axis's
 * units. The ruler does not move itself; its owner moves the view and sets
 * it back (setAxis).
 *
 * The axis is in host samples; the tempo says where beat 0 is and how long a
 * beat is, in the same samples.
 */
#pragma once

#include "ZoomMath.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>
#include <vector>

namespace ni::ui
{

class TimeView : public juce::Component
{
public:
    static constexpr int height = 24;
    /* The closest two grid lines come, pixels. */
    static constexpr double minLinePx = 12.0;
    /* The closest two bar numbers come, pixels. */
    static constexpr float minLabelPx = 28.0f;

    struct Tempo
    {
        double samplesPerBeat = 24000.0;
        /* The host sample at beat 0 (the song's start). */
        double origin = 0.0;
        int beatsPerBar = 4;
    };

    struct Line
    {
        float x;
        bool bar;
        /* The bar's number, counted from 1; for a bar line. */
        long long number;
    };

    /* The lines over `width` pixels of `axis`. */
    static std::vector<Line> lines (const zoom::Axis& axis, const Tempo& tempo, float width);

    TimeView();
    ~TimeView() override;

    void setAxis (const zoom::Axis&);
    const zoom::Axis& getAxis() const noexcept { return axis; }
    void setTempo (Tempo);
    const Tempo& getTempo() const noexcept { return tempo; }

    /* A drag asks for the view to start here. */
    std::function<void (double start)> onPan;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    zoom::Axis axis;
    Tempo tempo;
    std::optional<double> pressStart;

    JUCE_DECLARE_NON_COPYABLE (TimeView)
};

} // namespace ni::ui
