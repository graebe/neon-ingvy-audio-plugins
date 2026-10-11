// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Bit-Crusher's three wells. Plots.h says what each shows.
 */
#include "Plots.h"

#include "InfoLines.h"

#include <algorithm>
#include <cmath>

namespace ni::bc
{

namespace
{
/* A level -1..1 at the content's height, +1 at the top. */
float yOf (const juce::Rectangle<float>& area, float level, float fullScale = 1.0f)
{
    const float v = juce::jlimit (-1.0f, 1.0f, level / fullScale);
    return area.getCentreY() - v * 0.5f * area.getHeight();
}

/* A caption's clauses, three spaces apart (Plot.h). */
juce::String clauses (std::initializer_list<juce::String> parts)
{
    juce::StringArray a;
    for (const auto& p : parts)
        if (p.isNotEmpty())
            a.add (p);
    return a.joinIntoString ("   ");
}
} // namespace

/* ------------------------------------------------------------ amplitude -- */

AmplitudePlot::AmplitudePlot (Model& m)
    : model (m), levels ((size_t) points, 0.0f), freshLevels ((size_t) points, 0.0f)
{
    ni::ui::setInfo (*this, info::amplitudePlot);
    update ({}, false);
    rebuild();
}

void AmplitudePlot::update (const Scope& look, bool fresh)
{
    const double freshSpan = model.steps (freshLevels.data(), points);
    /* Zoomed in, the caption says by how much: the well is no longer the
     * whole range, and a picture that does not say so misleads. */
    const auto caption = clauses ({ "AMPLITUDE CRUSH",
                                    model.parameter (param::bits).getCurrentValueAsText().toUpperCase(),
                                    freshSpan < 1.0 ? juce::String::fromUTF8 ("\xc3\x97") + juce::String (juce::roundToInt (1.0 / freshSpan))
                                                    : juce::String() });
    const bool moved = freshLevels != levels || ! juce::exactlyEqual (freshSpan, span) || caption != getCaption();
    if (! moved && ! fresh)
        return;
    if (moved)
    {
        levels = freshLevels;
        span = freshSpan;
        setCaption (caption);
    }
    if (fresh)
    {
        liveIn = look.ampIn;
        liveOut = look.ampOut;
    }
    rebuild();
    repaint();
}

void AmplitudePlot::rebuild()
{
    const auto area = contentBounds();

    /* THE OUTPUT AXIS FITS THE OUTPUTS. Drive tilts the staircase past the
     * diagonal; on the input's own scale its ends would be cut flat at the
     * well's edge, a clip that does not happen. */
    float peak = 0.0f;
    for (const float v : levels)
        peak = std::max (peak, std::abs (v));
    outSpan = std::max (span, (double) peak);
    const auto in = (float) span, out = (float) outSpan;
    const auto xOf = [&] (float level) { return xAt (0.5f * (level / in + 1.0f)); };

    diagonal.clear();
    diagonal.startNewSubPath (area.getX(), yOf (area, -in, out));
    diagonal.lineTo (area.getRight(), yOf (area, in, out));

    /* One point a sample of the curve: the risers are as steep as the
     * well's pixels allow, which is what a staircase drawn from samples
     * looks like. */
    curve.clear();
    for (int i = 0; i < points; ++i)
    {
        const float x = xAt ((float) i / (float) (points - 1));
        const float y = yOf (area, levels[(size_t) i], out);
        if (i == 0)
            curve.startNewSubPath (x, y);
        else
            curve.lineTo (x, y);
    }

    /* The look's samples inside the window. */
    dotsAt.clear();
    const auto n = std::min (liveIn.size(), liveOut.size());
    for (size_t i = 0; i < n; ++i)
        if (std::abs (liveIn[i]) <= in)
            dotsAt.emplace_back (xOf (liveIn[i]), yOf (area, liveOut[i], out));
}

void AmplitudePlot::paintPlot (juce::Graphics& g)
{
    ni::ui::plot::dryLine (g, diagonal);
    /* With nothing playing the staircase is lit; with a look, it is the map
     * and the dots light the stairs the signal is on. */
    if (dotsAt.empty())
        ni::ui::plot::curve (g, curve);
    else
    {
        ni::ui::plot::unlit (g, curve);
        ni::ui::plot::dots (g, dotsAt);
    }
}

/* ----------------------------------------------------------------- time -- */

TimePlot::TimePlot (Model& m) : model (m)
{
    ni::ui::setInfo (*this, info::timePlot);
    enableLevelRange();
    update ({}, false, 0.0);
}

void TimePlot::update (const Scope& look, bool fresh, double nowMs)
{
    if (fresh)
    {
        in = look.holdIn;
        took = look.holdTook;
        takes = look.holdTakes;
    }
    /* The holds the window spans: one fewer than the takes that bound them. */
    const auto caption = clauses ({ "TIME CRUSH",
                                    model.parameter (param::rate).getCurrentValueAsText().toUpperCase(),
                                    takes > 1 ? juce::String (takes - 1) + " HOLDS" : juce::String() });
    const bool captioned = caption != getCaption();
    if (captioned)
        setCaption (caption);

    float peak = 0.0f;
    for (const float v : in)
        peak = std::max (peak, std::abs (v));
    const bool moved = followLevel (peak, nowMs);
    if (fresh || moved)
    {
        rebuild();
        repaint();
    }
    else if (captioned)
        repaint();
}

void TimePlot::rebuild()
{
    inputLine.clear();
    heldLine.clear();
    dotsAt.clear();
    const int n = (int) std::min (in.size(), took.size());
    if (n < 2)
        return;
    const auto area = contentBounds();
    const float fullScale = levelRange().fullScale();
    const auto x = [&] (int i) { return xAt ((float) i / (float) (n - 1)); };
    const auto y = [&] (float v) { return yOf (area, v, fullScale); };

    /* The window starts on a take (bc_shell_read), so there is a level to
     * hold from its first sample. */
    float held = in[0];
    inputLine.startNewSubPath (x (0), y (in[0]));
    heldLine.startNewSubPath (x (0), y (held));
    for (int i = 0; i < n; ++i)
    {
        if (i > 0)
            inputLine.lineTo (x (i), y (in[(size_t) i]));
        if (took[(size_t) i] != 0)
        {
            if (i > 0)
            {
                heldLine.lineTo (x (i), y (held));
                held = in[(size_t) i];
                heldLine.lineTo (x (i), y (held));
            }
            dotsAt.emplace_back (x (i), y (held));
        }
    }
    heldLine.lineTo (x (n - 1), y (held));
}

void TimePlot::paintPlot (juce::Graphics& g)
{
    /* As in the amplitude well: what was taken is lit, the hold between is
     * the map. */
    ni::ui::plot::dryLine (g, inputLine);
    ni::ui::plot::unlit (g, heldLine);
    ni::ui::plot::dots (g, dotsAt);
}

/* --------------------------------------------------------------- signal -- */

SignalPlot::SignalPlot()
{
    ni::ui::setInfo (*this, info::signalPlot);
    enableLevelRange();
    setCaption ("SIGNAL");
}

void SignalPlot::update (const Scope& look, bool fresh, double nowMs)
{
    if (fresh)
    {
        shown = look;
        const auto ms = shown.ms();
        setCaption (ms > 0.0 ? "SIGNAL   " + juce::String (juce::roundToInt (ms)) + " MS   INPUT IN GREY" : "SIGNAL");
    }

    float peak = 0.0f;
    for (int i = 0; i < shown.count(); ++i)
        peak = std::max ({ peak, std::abs (shown.dry[(size_t) i]), std::abs (shown.wet[(size_t) i]) });
    const bool moved = followLevel (peak, nowMs);
    if (fresh || moved)
    {
        rebuild();
        repaint();
    }
}

void SignalPlot::rebuild()
{
    dryLine.clear();
    wetLine.clear();
    const int n = shown.count();
    if (n < 2)
        return;
    const auto area = contentBounds();
    const float fullScale = levelRange().fullScale();
    for (int i = 0; i < n; ++i)
    {
        const float x = xAt ((float) i / (float) (n - 1));
        const float yd = yOf (area, shown.dry[(size_t) i], fullScale);
        const float yw = yOf (area, shown.wet[(size_t) i], fullScale);
        if (i == 0)
        {
            dryLine.startNewSubPath (x, yd);
            wetLine.startNewSubPath (x, yw);
        }
        else
        {
            dryLine.lineTo (x, yd);
            wetLine.lineTo (x, yw);
        }
    }
}

void SignalPlot::paintPlot (juce::Graphics& g)
{
    ni::ui::plot::dryLine (g, dryLine);
    ni::ui::plot::curve (g, wetLine);
}

} // namespace ni::bc
