// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A view of a NoteHistory: the window and its bars. See HistoryView.h.
 */
#include "HistoryView.h"

#include "UvTokens.h"

#include <cmath>

namespace ni::ui
{

namespace
{
namespace c = uv::tok::colour;
constexpr float inset = 1.0f;
constexpr float nowInset = 6.0f;
} // namespace

HistoryView::HistoryView (const NoteHistory& h) : history (h)
{
    setInterceptsMouseClicks (false, false);
}

void HistoryView::setSpan (double quarters)
{
    if (quarters > 0.0 && ! juce::exactlyEqual (quarters, span))
    {
        span = quarters;
        repaint();
    }
}

juce::Rectangle<float> HistoryView::plot() const
{
    return getLocalBounds().toFloat().reduced (inset).withTrimmedLeft (gutter()).withTrimmedRight (nowInset);
}

double HistoryView::windowStart() const
{
    return history.clock().now - span;
}

float HistoryView::xOf (double t) const
{
    const auto area = plot();
    return area.getX() + (float) ((t - windowStart()) / span) * area.getWidth();
}

void HistoryView::paintBarLines (juce::Graphics& g, float top, float bottom) const
{
    const auto& clock = history.clock();
    if (clock.bar <= 0.0)
        return;
    const double first = clock.barOrigin + std::ceil ((windowStart() - clock.barOrigin) / clock.bar) * clock.bar;
    g.setColour (c::line200);
    for (double t = first; t <= clock.now; t += clock.bar)
        g.fillRect (juce::Rectangle<float> (std::round (xOf (t)), top, 1.0f, bottom - top));
}

void HistoryView::paintNow (juce::Graphics& g) const
{
    g.setColour (c::uvDeep);
    g.fillRect (juce::Rectangle<float> (std::round (xOf (history.clock().now)), plot().getY(), 1.0f, plot().getHeight()));
}

} // namespace ni::ui
