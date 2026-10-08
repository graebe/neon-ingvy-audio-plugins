// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The piano roll. PianoRoll.h has its looks.
 */
#include "PianoRoll.h"

#include "Music.h"
#include "UvTokens.h"
#include "UvType.h"

#include <algorithm>

namespace ni::ui
{

namespace
{
namespace c = uv::tok::colour;
constexpr int around = 2;
} // namespace

PianoRoll::PianoRoll (const NoteHistory& h) : HistoryView (h)
{
    setTitle ("MIDI history");
}

void PianoRoll::setNamer (Namer n)
{
    namer = std::move (n);
    repaint();
}

juce::String PianoRoll::nameOf (int midi) const
{
    if (namer)
        return namer (midi);
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[midi % 12]) + juce::String (midi / 12 - 1);
}

std::pair<int, int> PianoRoll::lines() const
{
    auto [low, high] = history.range (windowStart());
    if (low < 0)
        low = high = 60;
    low -= around;
    high += around;
    while (high - low + 1 < minimumLines)
    {
        --low;
        ++high;
    }
    if (low < 0)
    {
        high -= low;
        low = 0;
    }
    if (high > 127)
    {
        low = std::max (0, low - (high - 127));
        high = 127;
    }
    return { low, high };
}

juce::Rectangle<float> PianoRoll::lineOf (int midi) const
{
    const auto [low, high] = lines();
    const auto area = plot();
    const float h = area.getHeight() / (float) (high - low + 1);
    return { (float) getLocalBounds().getX() + 1.0f, area.getY() + (float) (high - midi) * h,
             area.getRight() - (float) getLocalBounds().getX() - 1.0f, h };
}

void PianoRoll::paint (juce::Graphics& g)
{
    const auto [low, high] = lines();
    const auto area = plot();
    const auto& hint = uv::tok::type::hint;
    const auto font = uv::type::font (hint);

    for (int m = low; m <= high; ++m)
    {
        const auto line = lineOf (m);
        if (music::isBlack (m))
        {
            g.setColour (c::bg100);
            g.fillRect (line.withLeft (area.getX()));
        }
        if (m % 12 == 0)
        {
            g.setColour (c::line200);
            g.fillRect (juce::Rectangle<float> (area.getX(), line.getBottom() - 1.0f, area.getWidth(), 1.0f));
            uv::type::draw (g, nameOf (m) + " " + juce::String (m),
                            { 6.0f, line.getBottom() - hint.lineHeight, gutter() - 6.0f, hint.lineHeight }, font,
                            c::inkDim);
        }
    }

    paintBarLines (g, area.getY(), area.getBottom());

    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (area.toNearestInt());
        const double from = windowStart();
        for (const auto& n : history.notes())
        {
            if (n.end < from || n.midi < low || n.midi > high)
                continue;
            const auto line = lineOf (n.midi);
            const float x0 = xOf (std::max (n.start, from));
            const float x1 = xOf (std::min (n.end, history.clock().now));
            g.setColour (n.sounding() ? c::uv : c::inkDim);
            g.fillRect (juce::Rectangle<float> (x0, line.getY(), std::max (2.0f, x1 - x0), std::max (1.0f, line.getHeight() - 1.0f)));
        }
    }

    paintNow (g);
}

} // namespace ni::ui
