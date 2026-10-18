// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A frequency response well. FrequencyResponse.h has what it draws.
 */
#include "FrequencyResponse.h"

#include "UvTokens.h"
#include "UvType.h"

#include <algorithm>
#include <cmath>

namespace ni::ui
{

namespace
{
namespace c = uv::tok::colour;

juce::Font rulerFont()
{
    return uv::type::hint().withHeight (9.0f);
}

const char* const analyzerWords[] = { "Analyzer off", "Pre", "Post", "Pre + post" };
} // namespace

FrequencyResponse::FrequencyResponse() = default;
FrequencyResponse::~FrequencyResponse() = default;

void FrequencyResponse::withPhaseScale (bool on)
{
    if (on == phaseScale)
        return;
    phaseScale = on;
    resized();
    repaint();
}

/* ------------------------------------------------------------ geometry -- */

juce::Rectangle<float> FrequencyResponse::plotArea() const
{
    const auto a = contentBounds();
    const float right = phaseScale ? labelsRight : 0.0f;
    return { a.getX() + labelsLeft, a.getY(), a.getWidth() - labelsLeft - right, a.getHeight() - labelsUnder };
}

float FrequencyResponse::xOf (double hz) const
{
    const auto a = plotArea();
    const double t = std::log (juce::jlimit (lowestHz, highestHz, hz) / lowestHz) / std::log (highestHz / lowestHz);
    return a.getX() + (float) t * a.getWidth();
}

double FrequencyResponse::hzAt (float x) const
{
    const auto a = plotArea();
    const double t = juce::jlimit (0.0, 1.0, (double) ((x - a.getX()) / a.getWidth()));
    return lowestHz * std::pow (highestHz / lowestHz, t);
}

float FrequencyResponse::yOf (double db) const
{
    const auto a = plotArea();
    return a.getY() + (float) ((range - db) / (2.0 * range)) * a.getHeight();
}

double FrequencyResponse::dbAt (float y) const
{
    const auto a = plotArea();
    return range - 2.0 * range * (double) ((y - a.getY()) / a.getHeight());
}

float FrequencyResponse::yOfDegrees (double degrees) const
{
    const auto a = plotArea();
    return a.getY() + (float) ((180.0 - juce::jlimit (-180.0, 180.0, degrees)) / 360.0) * a.getHeight();
}

void FrequencyResponse::cycleRange()
{
    range = range == 6 ? 12 : range == 12 ? 18 : range == 18 ? 30 : 6;
    geometryChanged();
    repaint();
}

void FrequencyResponse::resized()
{
    rebuildAnalyzer();
    geometryChanged();
}

/* --------------------------------------------------------------- paths -- */

juce::Path FrequencyResponse::curvePath (const float* hz, const float* db, int count) const
{
    const auto a = plotArea();
    juce::Path path;
    for (int i = 0; i < count; ++i)
    {
        const float x = xOf (hz[i]);
        const float y = juce::jlimit (a.getY() - 1.0f, a.getBottom() + 1.0f, yOf (db[i]));
        if (i == 0)
            path.startNewSubPath (x, y);
        else
            path.lineTo (x, y);
    }
    return path;
}

juce::Path FrequencyResponse::phasePath (const float* hz, const float* degrees, int count) const
{
    juce::Path path;
    for (int i = 0; i < count; ++i)
    {
        const juce::Point<float> p (xOf (hz[i]), yOfDegrees (degrees[i]));
        if (i == 0 || std::abs (degrees[i] - degrees[i - 1]) > 180.0f)
            path.startNewSubPath (p);
        else
            path.lineTo (p);
    }
    return path;
}

void FrequencyResponse::setAnalyzer (int m, bool on, const float* hz, const float* p, const float* q, int count)
{
    const auto same = [count] (const std::vector<float>& v, const float* x) { return (int) v.size() == count && std::equal (v.begin(), v.end(), x); };
    if (m == mode && on == live && same (hzs, hz) && same (pres, p) && same (posts, q))
        return;
    mode = m;
    live = on;
    hzs.assign (hz, hz + count);
    pres.assign (p, p + count);
    posts.assign (q, q + count);
    rebuildAnalyzer();
    repaint();
}

void FrequencyResponse::rebuildAnalyzer()
{
    pre.clear();
    post.clear();
    postEdge.clear();
    if (hzs.empty())
        return;
    const auto a = plotArea();
    const auto yOfLevel = [&a] (float dbfs) { return a.getY() + (float) juce::jlimit (0.0, 1.0, (analyzerTop - dbfs) / analyzerSpan) * a.getHeight(); };
    const auto area = [&] (const std::vector<float>& db, juce::Path& out)
    {
        out.startNewSubPath (a.getX(), a.getBottom());
        for (size_t i = 0; i < db.size(); ++i)
            out.lineTo (xOf (hzs[i]), yOfLevel (db[i]));
        out.lineTo (a.getRight(), a.getBottom());
        out.closeSubPath();
    };
    area (pres, pre);
    area (posts, post);
    for (size_t i = 0; i < posts.size(); ++i)
    {
        const juce::Point<float> p (xOf (hzs[i]), yOfLevel (posts[i]));
        if (i == 0)
            postEdge.startNewSubPath (p);
        else
            postEdge.lineTo (p);
    }
}

/* ------------------------------------------------------------- paint -- */

void FrequencyResponse::paintBackdrop (juce::Graphics& g)
{
    const auto a = plotArea();
    /* Before the filter faded behind, after it highlighted in front. */
    if (live)
    {
        if (mode == analyzerPre || mode == analyzerBoth)
        {
            g.setColour (c::plotDry.withMultipliedAlpha (0.25f));
            g.fillPath (pre);
        }
        if (mode == analyzerPost || mode == analyzerBoth)
        {
            g.setColour (c::plotGhost);
            g.fillPath (post);
            g.setColour (c::inkDim);
            g.strokePath (postEdge, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved));
        }
    }

    /* THE RULERS: the frame's edges, ticks and labels; no grid. */
    g.setColour (c::line200);
    g.drawLine (a.getX(), a.getY(), a.getX(), a.getBottom(), 1.0f);
    g.drawLine (a.getX(), a.getBottom(), a.getRight(), a.getBottom(), 1.0f);
    g.setFont (rulerFont());
    for (double decade = 10.0; decade <= 10000.0; decade *= 10.0)
        for (int k = 1; k <= 9; ++k)
        {
            const double hz = decade * k;
            if (hz > highestHz)
                continue;
            const bool named = (k == 1 || k == 2 || k == 5) && hz >= 20.0;
            const float x = xOf (hz);
            g.setColour (c::line200);
            g.drawLine (x, a.getBottom(), x, a.getBottom() + (named ? 3.0f : 2.0f), 1.0f);
            if (named)
            {
                const auto label = hz >= 1000.0 ? juce::String (hz / 1000.0, 0) + "k" : juce::String ((int) hz);
                g.setColour (c::inkMuted);
                g.drawText (label, juce::Rectangle<float> (x - 16.0f, a.getBottom() + 3.0f, 32.0f, 11.0f), juce::Justification::centred);
            }
        }
    const int step = range == 6 ? 3 : range == 30 ? 10 : 6;
    for (int db = -range; db <= range; db += step)
    {
        const float y = yOf (db);
        g.setColour (c::line200);
        g.drawLine (a.getX() - 3.0f, y, a.getX(), y, 1.0f);
        g.setColour (c::inkMuted);
        /* Held inside the plot's height, so the top label stays out of the
         * caption's band. */
        const float top = juce::jlimit (a.getY(), a.getBottom() - 12.0f, y - 6.0f);
        g.drawText ((db > 0 ? "+" : "") + juce::String (db), juce::Rectangle<float> (a.getX() - 30.0f, top, 25.0f, 12.0f),
                    juce::Justification::centredRight);
    }
    if (phaseScale)
    {
        g.setColour (c::line200);
        g.drawLine (a.getRight(), a.getY(), a.getRight(), a.getBottom(), 1.0f);
        for (int deg = 180; deg >= -180; deg -= 90)
        {
            const float y = yOfDegrees (deg);
            g.setColour (c::line200);
            g.drawLine (a.getRight(), y, a.getRight() + 3.0f, y, 1.0f);
            g.setColour (c::inkMuted);
            const float top = juce::jlimit (a.getY(), a.getBottom() - 12.0f, y - 6.0f);
            g.drawText ((deg > 0 ? "+" : "") + juce::String (deg) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")),
                        juce::Rectangle<float> (a.getRight() + 5.0f, top, labelsRight - 5.0f, 12.0f), juce::Justification::centredLeft);
        }
    }

    /* 0 dB: the signal untouched. */
    juce::Path zero;
    zero.startNewSubPath (a.getX(), yOf (0.0));
    zero.lineTo (a.getRight(), yOf (0.0));
    plot::dryLine (g, zero);
}

juce::String FrequencyResponse::rangeText() const
{
    return juce::String (juce::CharPointer_UTF8 ("\xc2\xb1")) + juce::String (range) + " dB";
}

void FrequencyResponse::paintCaptionEnd (juce::Graphics& g)
{
    const auto band = juce::Rectangle<float> (0.0f, plot::inset - 3.0f, (float) getWidth() - plot::inset, plot::captionH);
    g.setFont (uv::type::hint());
    g.setColour (c::inkMuted);
    const auto r = rangeText();
    g.drawText (r, band, juce::Justification::centredRight);
    const float rangeW = uv::type::width (uv::type::hint(), r);
    g.drawText (juce::String (analyzerWords[juce::jlimit (0, 3, mode)]).toUpperCase(), band.withTrimmedRight (rangeW + 16.0f),
                juce::Justification::centredRight);
}

std::optional<FrequencyResponse::CaptionHit> FrequencyResponse::captionHitAt (juce::Point<float> p) const
{
    if (p.y >= plotArea().getY())
        return std::nullopt;
    const float right = (float) getWidth() - plot::inset;
    const float rangeLeft = right - uv::type::width (uv::type::hint(), rangeText());
    if (p.x >= rangeLeft - 4.0f)
        return CaptionHit::range;
    if (p.x >= rangeLeft - 120.0f)
        return CaptionHit::analyzer;
    return std::nullopt;
}

} // namespace ni::ui
