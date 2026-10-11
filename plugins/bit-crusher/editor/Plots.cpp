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
    update ({}, false, false);
    rebuild();
}

void AmplitudePlot::update (const Scope& look, bool fresh, bool live)
{
    const double freshSpan = model.steps (freshLevels.data(), points);
    /* The levels the staircase has, as many as there are -- 2^Bits + 1, or
     * 2^Bits with Mid-Rise -- not 2^Bits by implication. Zoomed in, the
     * caption says by how much: the well is no longer the whole range, and a
     * picture that does not say so misleads. */
    const auto caption = clauses ({ "AMPLITUDE CRUSH", juce::String (model.levelCount()) + " LEVELS",
                                    freshSpan < 1.0 ? juce::String::fromUTF8 ("\xc3\x97") + juce::String (juce::roundToInt (1.0 / freshSpan))
                                                    : juce::String() });
    const bool moved = freshLevels != levels || ! juce::exactlyEqual (freshSpan, span) || caption != getCaption()
                       || live != shownLive;
    shownLive = live;
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

    /* The look's samples inside the window, while the look is the knobs'. */
    dotsAt.clear();
    const auto n = shownLive ? std::min (liveIn.size(), liveOut.size()) : (size_t) 0;
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
    update ({}, false, false, 0.0);
}

void TimePlot::update (const Scope& look, bool fresh, bool live, double nowMs)
{
    if (fresh)
    {
        in = look.holdIn;
        took = look.holdTook;
        at = look.holdAt;
        takes = look.holdTakes;
        sampleRate = look.sampleRate;
    }
    const bool liveMoved = live != shownLive;
    shownLive = live;

    /* The holds the window spans: one fewer than the takes that bound them.
     * At a Rate of the host's own or above, every sample is taken and
     * nothing is held. */
    const double rate = model.setting (param::rate);
    juce::String holds;
    if (sampleRate > 0.0 && rate >= sampleRate)
        holds = "NO HOLD";
    else if (shownLive && takes > 1)
        holds = juce::String (takes - 1) + " HOLDS";
    const auto caption = clauses ({ "TIME CRUSH", model.parameter (param::rate).getCurrentValueAsText().toUpperCase(), holds });
    const bool captioned = caption != getCaption();
    if (captioned)
        setCaption (caption);

    float peak = 0.0f;
    if (shownLive)
        for (const float v : in)
            peak = std::max (peak, std::abs (v));
    const bool moved = followLevel (peak, nowMs);
    if (fresh || moved || liveMoved)
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
    const int n = shownLive ? (int) std::min ({ in.size(), took.size(), at.size() }) : 0;
    if (n < 2 || takes == 0)
        return;
    const auto area = contentBounds();
    const float fullScale = levelRange().fullScale();
    /* Time in samples from the window's start, fractions and all. */
    const auto x = [&] (double t) { return xAt ((float) (t / (double) (n - 1))); };
    const auto y = [&] (float v) { return yOf (area, v, fullScale); };
    /* The input between two samples, for a take that fell between them. */
    const auto inputAt = [&] (int i, float f)
    { return i + 1 < n ? in[(size_t) i] + f * (in[(size_t) i + 1] - in[(size_t) i]) : in[(size_t) i]; };

    for (int i = 0; i < n; ++i)
    {
        if (i == 0)
            inputLine.startNewSubPath (x (0), y (in[0]));
        else
            inputLine.lineTo (x (i), y (in[(size_t) i]));
    }
    /* The hold: from each take -- on the host's grid, or at its exact
     * instant with Anti-Alias -- flat until the next. The window starts on
     * one (bc_shell_read). */
    bool started = false;
    float held = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        if (took[(size_t) i] == 0)
            continue;
        const double t = i + (double) at[(size_t) i];
        const float level = inputAt (i, at[(size_t) i]);
        if (! started)
            heldLine.startNewSubPath (x (t), y (level));
        else
        {
            heldLine.lineTo (x (t), y (held));
            heldLine.lineTo (x (t), y (level));
        }
        started = true;
        held = level;
        dotsAt.emplace_back (x (t), y (level));
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
