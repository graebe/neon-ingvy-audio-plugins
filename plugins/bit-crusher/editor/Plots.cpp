// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Bit-Crusher's two wells. Plots.h says what each shows.
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
} // namespace

/* ---------------------------------------------------------------- steps -- */

StepsPlot::StepsPlot (Model& m) : model (m), levels ((size_t) points, 0.0f), fresh ((size_t) points, 0.0f)
{
    ni::ui::setInfo (*this, info::stepsPlot);
    update();
    rebuild();
}

void StepsPlot::update()
{
    model.steps (fresh.data(), points);
    const auto caption = "STEPS   " + model.parameter (param::bits).getCurrentValueAsText().toUpperCase();
    if (fresh == levels && caption == getCaption())
        return;
    levels = fresh;
    setCaption (caption);
    rebuild();
    repaint();
}

void StepsPlot::rebuild()
{
    const auto area = contentBounds();
    diagonal.clear();
    diagonal.startNewSubPath (area.getX(), area.getBottom());
    diagonal.lineTo (area.getRight(), area.getY());

    /* One point a sample of the curve: the risers are as steep as the
     * well's pixels allow, which is what a staircase drawn from samples
     * looks like. */
    curve.clear();
    for (int i = 0; i < points; ++i)
    {
        const float x = xAt ((float) i / (float) (points - 1));
        const float y = yOf (area, levels[(size_t) i]);
        if (i == 0)
            curve.startNewSubPath (x, y);
        else
            curve.lineTo (x, y);
    }
}

void StepsPlot::paintPlot (juce::Graphics& g)
{
    ni::ui::plot::ghost (g, diagonal);
    ni::ui::plot::curve (g, curve);
}

/* --------------------------------------------------------------- signal -- */

SignalPlot::SignalPlot (Model& m) : model (m)
{
    ni::ui::setInfo (*this, info::signalPlot);
    enableLevelRange();
    setCaption ("SIGNAL");
}

void SignalPlot::update (double nowMs)
{
    const auto& s = model.scope();
    const bool fresh = s.serial != shown.serial || s.count() != shown.count();
    if (fresh)
    {
        shown = s;
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
    ni::ui::plot::ghost (g, dryLine);
    ni::ui::plot::curve (g, wetLine);
}

} // namespace ni::bc
