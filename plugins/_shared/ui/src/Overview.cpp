// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The overview strip. Overview.h has what it shows.
 */
#include "Overview.h"

#include "UvLight.h"
#include "UvTokens.h"
#include "UvType.h"

namespace ni::ui
{

namespace
{
namespace c = uv::tok::colour;
/* The view's box-shadow, 0 0 6px uv-deep. */
constexpr uv::tok::ShadowLayer viewGlow { 0.0f, 0.0f, 6.0f, 0.0f, uv::tok::argb::ultraviolet::uvDeep };
}

Overview::Overview()
{
    setTitle ("Overview");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

Overview::~Overview() = default;

void Overview::setCaption (const juce::String& text)
{
    caption = text;
    repaint();
}

void Overview::setContent (double a, double b)
{
    lo = std::min (a, b);
    hi = std::max (std::max (a, b), lo + 1.0e-9);
    repaint();
}

void Overview::setBlocks (std::vector<Block> b)
{
    blocks = std::move (b);
    repaint();
}

void Overview::setTicks (std::vector<Tick> t)
{
    ticks = std::move (t);
    repaint();
}

void Overview::setView (double start, double span)
{
    viewStart = start;
    viewSpan = span;
    repaint();
}

juce::Rectangle<float> Overview::plot() const
{
    return { (float) side, (float) plotTop, (float) (getWidth() - 2 * side), (float) plotHeight };
}

float Overview::xOf (double t) const noexcept
{
    const auto p = plot();
    return p.getX() + (float) ((t - lo) / (hi - lo)) * p.getWidth();
}

double Overview::timeAt (float x) const noexcept
{
    const auto p = plot();
    return lo + (double) ((x - p.getX()) / std::max (1.0f, p.getWidth())) * (hi - lo);
}

void Overview::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour (c::bg000);
    g.fillRect (b);
    g.setColour (c::line100);
    g.drawRect (b, uv::tok::stroke::strokeHair);

    const auto& style = uv::tok::type::hint;
    uv::type::draw (g, caption.toUpperCase(), { (float) side, 3.0f, b.getWidth() - 2.0f * (float) side, style.lineHeight },
                    uv::type::font (style), c::inkMuted);

    const auto p = plot();
    g.setColour (c::bg300);
    for (const auto& k : blocks)
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xOf (k.start), p.getY(), xOf (k.end), p.getBottom()));

    g.setColour (c::inkMuted);
    for (const auto& t : ticks)
    {
        const float y = p.getBottom() - 2.0f - juce::jlimit (0.0f, 1.0f, t.height) * (p.getHeight() - 4.0f);
        const float x0 = xOf (t.start);
        g.fillRect (juce::Rectangle<float> (x0, y, std::max (1.0f, xOf (t.end) - x0), uv::tok::stroke::strokeHair));
    }

    const auto view = juce::Rectangle<float>::leftTopRightBottom (xOf (viewStart), p.getY() - 2.0f, xOf (viewStart + viewSpan),
                                                                  p.getBottom() + 2.0f);
    {
        /* box-shadow: outside the rectangle only, the content under it
         * stays visible. */
        juce::Graphics::ScopedSaveState state (g);
        g.excludeClipRegion (view.getSmallestIntegerContainer());
        uv::light::shadow (g, view, viewGlow);
    }
    g.setColour (c::uv);
    g.drawRect (view, uv::tok::stroke::strokeHair);
}

void Overview::moveTo (float x)
{
    if (onView)
        onView (timeAt (x) - 0.5 * viewSpan);
}

void Overview::mouseDown (const juce::MouseEvent& e)
{
    moveTo (e.position.x);
}

void Overview::mouseDrag (const juce::MouseEvent& e)
{
    moveTo (e.position.x);
}

} // namespace ni::ui
