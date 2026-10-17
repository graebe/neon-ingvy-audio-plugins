// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The time ruler. TimeView.h has the strip and its rules.
 */
#include "TimeView.h"

#include "UvTokens.h"
#include "UvType.h"

namespace ni::ui
{

namespace
{
namespace c = uv::tok::colour;
}

std::vector<TimeView::Line> TimeView::lines (const zoom::Axis& axis, const Tempo& tempo, float width)
{
    std::vector<Line> out;
    const int group = std::max (1, tempo.beatsPerBar);
    const auto g = zoom::grid (axis, width, tempo.origin, tempo.samplesPerBeat, group, minLinePx);
    out.reserve ((size_t) g.count);
    for (long long i = 0; i < g.count; ++i)
    {
        const long long beat = g.first + i * g.every;
        const double at = tempo.origin + (double) beat * tempo.samplesPerBeat;
        const bool bar = beat % group == 0;
        const long long number = (beat >= 0 ? beat / group : (beat - group + 1) / group) + 1;
        out.push_back ({ (float) axis.toPx (at, width), bar, number });
    }
    return out;
}

TimeView::TimeView()
{
    setTitle ("Time");
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

TimeView::~TimeView() = default;

void TimeView::setAxis (const zoom::Axis& a)
{
    axis = a;
    repaint();
}

void TimeView::setTempo (Tempo t)
{
    tempo = t;
    repaint();
}

void TimeView::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour (c::bg000);
    g.fillRect (b);
    g.setColour (c::line100);
    g.fillRect (b.withTop (b.getBottom() - uv::tok::stroke::strokeHair));

    const auto& style = uv::tok::type::hint;
    const auto font = uv::type::font (style);
    float lastLabel = -1.0e9f;
    for (const auto& l : lines (axis, tempo, b.getWidth()))
    {
        if (! l.bar)
            continue;
        g.setColour (c::line200);
        g.fillRect (juce::Rectangle<float> (l.x, 0.0f, uv::tok::stroke::strokeHair, (float) getHeight()));
        if (l.x - lastLabel >= minLabelPx)
        {
            uv::type::draw (g, juce::String (l.number), { l.x + 4.0f, 5.0f, minLabelPx * 2.0f, style.lineHeight }, font,
                            c::inkMuted);
            lastLabel = l.x;
        }
    }
}

void TimeView::mouseDown (const juce::MouseEvent&)
{
    pressStart = axis.start;
}

void TimeView::mouseDrag (const juce::MouseEvent& e)
{
    if (pressStart.has_value() && onPan)
        onPan (*pressStart - (double) e.getDistanceFromDragStartX() * axis.perPx ((double) getWidth()));
}

} // namespace ni::ui
