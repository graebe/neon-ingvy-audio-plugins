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

using ni::ui::plot::Mark;

/* Room for the rulers: a column of level labels at the left, a row of
 * labels under the area. */
constexpr float rulerW = 30.0f;
constexpr float rulerH = 14.0f;

/* A level as a ruler names it, in full scale: a power of two below 1 as the
 * fraction it is ("1/16"), anything else to two places ("0.71"). */
juce::String levelText (double v)
{
    if (juce::exactlyEqual (v, 0.0))
        return "0";
    const juce::String sign = v < 0.0 ? "-" : "";
    const double a = std::abs (v);
    const double inv = 1.0 / a;
    if (a < 1.0 && juce::exactlyEqual (inv, std::round (inv)) && juce::isPowerOfTwo ((int) inv))
        return sign + "1/" + juce::String ((int) inv);
    if (juce::exactlyEqual (a, std::round (a)))
        return sign + juce::String ((int) a);
    return sign + juce::String (a, 2);
}

/* A level ruler's labelled marks: the area's ends and its middle, ±fullScale
 * and 0, for an axis running `from`..`to` in pixels (low level first). */
std::vector<Mark> levelMarks (float from, float to, double fullScale)
{
    const auto at = [&] (double f) { return from + (to - from) * (float) f; };
    return { { at (0.0), levelText (-fullScale) }, { at (0.5), "0" }, { at (1.0), levelText (fullScale) } };
}

/* A level ruler for a range that follows the material (±fullScale, any
 * number): round levels inside it, the smallest step of 0.05, 0.1, 0.2, 0.25,
 * 0.5 or 1 whose ticks stay the kit's spacing apart -- so it reads "-0.5 0
 * 0.5", not the range's own odd ends. */
std::vector<Mark> roundLevelMarks (float from, float to, double fullScale)
{
    std::vector<Mark> out;
    if (! (fullScale > 0.0))
        return out;
    const float perUnit = std::abs (to - from) / (float) (2.0 * fullScale);
    double step = 1.0;
    for (const double s : { 0.05, 0.1, 0.2, 0.25, 0.5, 1.0 })
        if (perUnit * (float) s >= ni::ui::plot::tickSpacing)
        {
            step = s;
            break;
        }
    const auto at = [&] (double v) { return from + (to - from) * (float) (0.5 * (v / fullScale + 1.0)); };
    const int most = (int) std::floor (fullScale / step + 1.0e-9);
    for (int k = -most; k <= most; ++k)
    {
        const double v = k * step;
        const bool whole = std::abs (v - std::round (v)) < 1.0e-9;
        out.push_back ({ at (v), whole ? juce::String (juce::roundToInt (v)) : juce::String (v, step < 0.1 ? 2 : 1) });
    }
    return out;
}

/* A time ruler over `count` samples at `sampleRate` across `area`: the kit's
 * millisecond ticks (axisTicks), in the area's pixels. */
std::vector<Mark> millisecondMarks (const juce::Rectangle<float>& area, int count, double sampleRate)
{
    std::vector<Mark> out;
    if (count < 2 || ! (sampleRate > 0.0))
        return out;
    const double spanMs = 1000.0 * (double) (count - 1) / sampleRate;
    for (const auto& t : ni::ui::plot::axisTicks (area.getWidth(), spanMs))
        out.push_back ({ area.getX() + area.getWidth() * (float) (t.ms / spanMs), t.text });
    return out;
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

juce::Rectangle<float> AmplitudePlot::plotArea() const
{
    return contentBounds().withTrimmedLeft (rulerW).withTrimmedBottom (rulerH);
}

void AmplitudePlot::rebuild()
{
    const auto area = plotArea();

    /* ONE SCALE BOTH WAYS, so the untouched signal is the well's diagonal,
     * corner to corner, at any zoom. The axis is the quantiser's own input,
     * after Drive: the staircase runs along the diagonal whatever Drive is,
     * and Drive moves the signal's dots up it (Model::steps). */
    outSpan = span;
    const auto in = (float) span, out = (float) outSpan;
    const auto xOf = [&] (float level) { return area.getX() + area.getWidth() * 0.5f * (level / in + 1.0f); };

    diagonal.clear();
    diagonal.startNewSubPath (area.getX(), yOf (area, -in, out));
    diagonal.lineTo (area.getRight(), yOf (area, in, out));

    /* One point a sample of the curve: the risers are as steep as the
     * well's pixels allow, which is what a staircase drawn from samples
     * looks like. */
    curve.clear();
    const auto xAtPoint = [&] (float i) { return area.getX() + area.getWidth() * i / (float) (points - 1); };
    for (int i = 0; i < points; ++i)
    {
        const float x = xAtPoint ((float) i);
        const float y = yOf (area, levels[(size_t) i], out);
        if (i == 0)
            curve.startNewSubPath (x, y);
        else
            curve.lineTo (x, y);
    }

    /* THE RULERS: level in across, level out up, in full scale -- the
     * window's ends and silence labelled -- and a minor tick at every step:
     * across where the staircase rises, up at every level it has. The grid
     * the signal is made discrete on, on the axes. */
    xMarks = levelMarks (area.getX(), area.getRight(), span);
    yMarks = levelMarks (area.getBottom(), area.getY(), outSpan);
    std::vector<float> seen;
    for (int i = 1; i < points; ++i)
        if (! juce::exactlyEqual (levels[(size_t) i], levels[(size_t) i - 1]))
            xMarks.push_back ({ xAtPoint ((float) i - 0.5f), {} });
    for (const float v : levels)
        if (std::abs (v) <= out && std::find (seen.begin(), seen.end(), v) == seen.end())
            seen.push_back (v);
    for (const float v : seen)
        yMarks.push_back ({ yOf (area, v, out), {} });

    /* The look's samples inside the window, while the look is the knobs'. */
    dotsAt.clear();
    const auto n = shownLive ? std::min (liveIn.size(), liveOut.size()) : (size_t) 0;
    for (size_t i = 0; i < n; ++i)
        if (std::abs (liveIn[i]) <= in)
            dotsAt.emplace_back (xOf (liveIn[i]), yOf (area, liveOut[i], out));
}

void AmplitudePlot::paintPlot (juce::Graphics& g)
{
    ni::ui::plot::ruler (g, plotArea(), ni::ui::plot::Side::bottom, xMarks);
    ni::ui::plot::ruler (g, plotArea(), ni::ui::plot::Side::left, yMarks);
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

juce::Rectangle<float> TimePlot::plotArea() const
{
    return contentBounds().withTrimmedLeft (rulerW).withTrimmedBottom (rulerH);
}

void TimePlot::rebuild()
{
    inputLine.clear();
    heldLine.clear();
    dotsAt.clear();
    xMarks.clear();
    yMarks.clear();
    const int n = shownLive ? (int) std::min ({ in.size(), took.size(), at.size() }) : 0;
    if (n < 2 || takes == 0)
        return;
    const auto area = plotArea();
    const float fullScale = levelRange().fullScale();
    /* Time in samples from the window's start, fractions and all. */
    const auto x = [&] (double t) { return area.getX() + area.getWidth() * (float) (t / (double) (n - 1)); };
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
        /* A minor tick at every take: the grid time is made discrete on. */
        xMarks.push_back ({ x (t), {} });
    }
    heldLine.lineTo (x (n - 1), y (held));

    /* THE RULERS: milliseconds across the window, level up. */
    for (auto& m : millisecondMarks (area, n, sampleRate))
        xMarks.push_back (std::move (m));
    yMarks = roundLevelMarks (area.getBottom(), area.getY(), fullScale);
}

void TimePlot::paintPlot (juce::Graphics& g)
{
    if (! xMarks.empty())
    {
        ni::ui::plot::ruler (g, plotArea(), ni::ui::plot::Side::bottom, xMarks);
        ni::ui::plot::ruler (g, plotArea(), ni::ui::plot::Side::left, yMarks);
    }
    /* As in the amplitude well: what was taken is lit, the hold between is
     * the map. */
    ni::ui::plot::dryLine (g, inputLine);
    ni::ui::plot::unlit (g, heldLine);
    ni::ui::plot::dots (g, dotsAt);
}

/* --------------------------------------------------------------- filter -- */

FilterPlot::FilterPlot (Model& m)
    : model (m), db ((size_t) points, 0.0f), freshDb ((size_t) points, 0.0f)
{
    ni::ui::setInfo (*this, info::filterPlot);
    update();
    rebuild();
}

void FilterPlot::update()
{
    model.filterResponse (freshDb.data(), points, loHz, hiHz);
    const bool freshOn = model.setting (param::filter) >= 0.5;
    const double peak = model.setting (param::peak);
    const auto caption = freshOn
        ? clauses ({ "FILTER", model.parameter (param::cutoff).getCurrentValueAsText().toUpperCase(),
                     model.parameter (param::slope).getCurrentValueAsText().toUpperCase() + "/OCT",
                     peak > 0.0 ? model.parameter (param::peak).getCurrentValueAsText().toUpperCase() : juce::String() })
        : juce::String ("FILTER   OFF");
    if (freshDb == db && freshOn == on && caption == getCaption())
        return;
    db = freshDb;
    on = freshOn;
    setCaption (caption);
    rebuild();
    repaint();
}

juce::Rectangle<float> FilterPlot::plotArea() const
{
    return contentBounds().withTrimmedLeft (rulerW).withTrimmedBottom (rulerH);
}

void FilterPlot::rebuild()
{
    const auto area = plotArea();
    const auto x = [&] (double hz) { return area.getX() + area.getWidth() * (float) (std::log (hz / loHz) / std::log (hiHz / loHz)); };
    const auto y = [&] (float g)
    { return area.getY() + area.getHeight() * (topDb - juce::jlimit (bottomDb, topDb, g)) / (topDb - bottomDb); };

    curve.clear();
    for (int i = 0; i < points; ++i)
    {
        const float px = area.getX() + area.getWidth() * (float) i / (float) (points - 1);
        if (i == 0)
            curve.startNewSubPath (px, y (db[(size_t) i]));
        else
            curve.lineTo (px, y (db[(size_t) i]));
    }
    flat.clear();
    flat.startNewSubPath (area.getX(), y (0.0f));
    flat.lineTo (area.getRight(), y (0.0f));

    /* THE RULERS: octaves across, 100 Hz, 1k and 10k named, a minor tick at
     * each whole multiple between; gain up every 24 dB, minor every 12. */
    xMarks.clear();
    for (double decade = 10.0; decade <= hiHz; decade *= 10.0)
        for (int k = 1; k <= 9; ++k)
        {
            const double hz = decade * k;
            if (hz < loHz || hz > hiHz)
                continue;
            juce::String text;
            if (k == 1 && decade >= 100.0)
                text = decade >= 1000.0 ? juce::String ((int) (decade / 1000.0)) + "k" : juce::String ((int) decade);
            xMarks.push_back ({ x (hz), text });
        }
    yMarks.clear();
    for (float g = topDb; g >= bottomDb; g -= 12.0f)
    {
        const bool named = std::fmod (std::abs (g), 24.0f) < 0.01f;
        yMarks.push_back ({ y (g), named ? (g > 0.0f ? "+" : "") + juce::String ((int) g) : juce::String() });
    }
}

void FilterPlot::paintPlot (juce::Graphics& g)
{
    ni::ui::plot::ruler (g, plotArea(), ni::ui::plot::Side::bottom, xMarks);
    ni::ui::plot::ruler (g, plotArea(), ni::ui::plot::Side::left, yMarks);
    ni::ui::plot::dryLine (g, flat);
    /* Lit while it acts on the sound; off, the map of what it would do. */
    if (on)
        ni::ui::plot::curve (g, curve);
    else
        ni::ui::plot::unlit (g, curve);
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

juce::Rectangle<float> SignalPlot::plotArea() const
{
    return contentBounds().withTrimmedLeft (rulerW).withTrimmedBottom (rulerH);
}

void SignalPlot::rebuild()
{
    dryLine.clear();
    wetLine.clear();
    xMarks.clear();
    yMarks.clear();
    const int n = shown.count();
    if (n < 2)
        return;
    const auto area = plotArea();
    const float fullScale = levelRange().fullScale();
    /* THE RULERS: milliseconds across, level up. */
    xMarks = millisecondMarks (area, n, shown.sampleRate);
    yMarks = roundLevelMarks (area.getBottom(), area.getY(), fullScale);
    for (int i = 0; i < n; ++i)
    {
        const float x = area.getX() + area.getWidth() * (float) i / (float) (n - 1);
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
    if (! xMarks.empty())
    {
        ni::ui::plot::ruler (g, plotArea(), ni::ui::plot::Side::bottom, xMarks);
        ni::ui::plot::ruler (g, plotArea(), ni::ui::plot::Side::left, yMarks);
    }
    ni::ui::plot::dryLine (g, dryLine);
    ni::ui::plot::curve (g, wetLine);
}

} // namespace ni::bc
