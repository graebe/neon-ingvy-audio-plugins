// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Notes as bars. NoteBars.h has what each part is.
 */
#include "NoteBars.h"

#include "UvTokens.h"

#include <algorithm>
#include <cmath>

namespace ni::ui
{

namespace
{
namespace c = uv::tok::colour;
constexpr float contourWidth = 1.5f;
constexpr float selectedMix = 0.35f;
constexpr float quietest = 0.4f;

/* A note's mean level over its own span; 0 with nothing measured there. */
float meanLevel (const NoteBars::Bar& b)
{
    double sum = 0.0;
    int n = 0;
    const auto& l = b.level;
    for (size_t i = 0; i < l.values.size(); ++i)
    {
        const double t = l.start + (double) i * l.step;
        if (t >= b.start && t <= b.end && std::isfinite (l.values[i]))
        {
            sum += (double) l.values[i];
            ++n;
        }
    }
    return n > 0 ? (float) (sum / n) : 0.0f;
}
}

NoteBars::NoteBars()
{
    setInterceptsMouseClicks (false, false);
    setTitle ("Notes");
}

NoteBars::~NoteBars() = default;

void NoteBars::setAxes (const zoom::Axis& t, const zoom::Axis& p)
{
    time = t;
    pitch = p;
    repaint();
}

void NoteBars::setBars (std::vector<Bar> b)
{
    bars = std::move (b);
    loudness.clear();
    for (const auto& n : bars)
        loudness.push_back (meanLevel (n));
    const float loudest = loudness.empty() ? 0.0f : *std::max_element (loudness.begin(), loudness.end());
    for (auto& l : loudness)
        l = loudest > 0.0f ? l / loudest : 1.0f;
    repaint();
}

void NoteBars::setPlayhead (std::optional<double> at)
{
    if (at == playhead)
        return;
    playhead = at;
    repaint();
}

void NoteBars::setMarker (std::optional<Marker> m)
{
    if (m == marker)
        return;
    marker = m;
    repaint();
}

float NoteBars::xOf (double t) const noexcept
{
    return (float) time.toPx (t, (double) getWidth());
}

float NoteBars::yOf (double midi) const noexcept
{
    return (float) ((pitch.end() - midi) / pitch.span * (double) getHeight());
}

double NoteBars::timeAt (float x) const noexcept
{
    return time.fromPx ((double) x, (double) getWidth());
}

double NoteBars::pitchAt (float y) const noexcept
{
    return pitch.end() - (double) y / std::max (1.0, (double) getHeight()) * pitch.span;
}

float NoteBars::halfThickness() const noexcept
{
    const float row = (float) ((double) getHeight() / std::max (1.0e-9, pitch.span));
    return 0.5f * juce::jlimit (6.0f, 24.0f, row - 2.0f);
}

juce::Rectangle<float> NoteBars::box (const Bar& b) const
{
    const float h = halfThickness();
    return { xOf (b.start), yOf (b.centre) - h, xOf (b.end) - xOf (b.start), 2.0f * h };
}

std::optional<std::uint64_t> NoteBars::noteAt (juce::Point<float> p) const
{
    const float reachY = std::max (reach, halfThickness());
    for (auto it = bars.rbegin(); it != bars.rend(); ++it)
    {
        const float x0 = xOf (it->start), x1 = xOf (it->end);
        if (p.x >= x0 && p.x <= x1 && std::abs (p.y - yOf (it->centre)) <= reachY)
            return it->id;
    }
    return std::nullopt;
}

std::vector<std::uint64_t> NoteBars::notesIn (juce::Rectangle<float> r) const
{
    std::vector<std::uint64_t> out;
    for (const auto& b : bars)
        if (box (b).intersects (r))
            out.push_back (b.id);
    return out;
}

std::optional<NoteBars::Edge> NoteBars::edgeAt (juce::Point<float> p) const
{
    for (auto it = bars.rbegin(); it != bars.rend(); ++it)
    {
        if (! it->handles)
            continue;
        const float y = yOf (it->centre);
        if (std::abs (p.y - y) > std::max (handleReach, halfThickness()))
            continue;
        for (const bool end : { false, true })
            if (std::abs (p.x - xOf (end ? it->end : it->start)) <= handleReach)
                return Edge { it->id, end };
    }
    return std::nullopt;
}

juce::Rectangle<float> NoteBars::boundsOf (std::uint64_t id) const
{
    for (const auto& b : bars)
        if (b.id == id)
            return box (b);
    return {};
}

juce::Path NoteBars::line (const Curve& c) const
{
    juce::Path p;
    bool open = false;
    for (size_t i = 0; i < c.values.size(); ++i)
    {
        const float v = c.values[i];
        if (! std::isfinite (v))
        {
            open = false;
            continue;
        }
        const juce::Point<float> at { xOf (c.start + (double) i * c.step), yOf ((double) v) };
        if (open)
            p.lineTo (at);
        else
            p.startNewSubPath (at);
        open = true;
    }
    return p;
}

void NoteBars::paint (juce::Graphics& g)
{
    const float corner = uv::tok::radius::radius1;
    /* The marked run's links first, under the bars they join. */
    const Bar* previous = nullptr;
    for (const auto& b : bars)
    {
        if (! b.marked)
            continue;
        if (previous != nullptr)
        {
            g.setColour (c::uv);
            g.drawLine ({ box (*previous).getRight(), yOf (previous->centre), box (b).getX(), yOf (b.centre) }, uv::tok::stroke::strokeHair);
        }
        previous = &b;
    }

    for (size_t i = 0; i < bars.size(); ++i)
    {
        const auto& b = bars[i];
        if (xOf (b.end) < 0.0f || xOf (b.start) > (float) getWidth())
            continue;
        const auto r = box (b);
        if (! b.muted)
        {
            const auto fill = b.selected ? c::bg300.interpolatedWith (c::uvDeep, selectedMix) : c::bg300;
            g.setColour (fill.withMultipliedAlpha (quietest + (1.0f - quietest) * loudness[i]));
            g.fillRoundedRectangle (r, corner);
        }
        g.setColour (b.selected || b.marked ? c::uv : c::line200);
        g.drawRoundedRectangle (r, corner, uv::tok::stroke::strokeHair);

        if (! b.muted && ! b.ghost.values.empty())
        {
            juce::Path dashed;
            const float dashes[] { 3.0f, 3.0f };
            juce::PathStrokeType (uv::tok::stroke::strokeHair).createDashedStroke (dashed, line (b.ghost), dashes, 2);
            g.setColour (c::line200);
            g.fillPath (dashed);
        }
        if (! b.muted)
        {
            g.setColour (b.selected ? c::uv : c::ink);
            g.strokePath (line (b.contour), juce::PathStrokeType (contourWidth, juce::PathStrokeType::curved));
        }

        if (b.handles)
        {
            g.setColour (c::uv);
            for (const double t : { b.start, b.end })
                g.fillRect (juce::Rectangle<float> (handleSize, handleSize).withCentre ({ xOf (t), r.getCentreY() }));
        }
        if (marker.has_value() && marker->id == b.id)
        {
            g.setColour (c::uv);
            g.fillRect (juce::Rectangle<float> (xOf (marker->at), r.getY(), uv::tok::stroke::strokeHair, r.getHeight()));
        }
    }
    if (playhead.has_value())
    {
        g.setColour (c::uvDeep);
        g.fillRect (juce::Rectangle<float> (xOf (*playhead), 0.0f, uv::tok::stroke::strokeHair, (float) getHeight()));
    }
}

} // namespace ni::ui
