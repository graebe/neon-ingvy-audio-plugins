// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The whole of what a panning well shows, small, with the view on it
 * (docs/adr/0006: an overview keeps orientation) -- a song's strip under a
 * note editor's well, say.
 *
 * THE STRIP: bg-000 on a line-100 hairline. A caption in the hint style,
 * capitals, ink-muted, at the top left. Under it, across the strip less 6px a
 * side, the content from `lo` to `hi`: blocks (a take) on bg-300 and ticks
 * (a note: a short ink-muted line at its height, 0 the bottom and 1 the
 * top); and the view, a 1px uv rectangle with a uv-deep glow.
 *
 * A PRESS OR A DRAG PUTS THE VIEW where the pointer is, centred on it
 * (onView, with the view's new start). Its owner moves the view and sets it
 * back (setView).
 */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace ni::ui
{

class Overview : public juce::Component
{
public:
    static constexpr int height = 56;
    static constexpr int side = 6;
    static constexpr int plotTop = 20;
    static constexpr int plotHeight = 30;

    struct Block
    {
        double start, end;
    };

    struct Tick
    {
        double start, end;
        /* Where it sits, 0 the bottom of the plot and 1 the top. */
        float height;
    };

    Overview();
    ~Overview() override;

    void setCaption (const juce::String&);
    void setContent (double lo, double hi);
    void setBlocks (std::vector<Block>);
    void setTicks (std::vector<Tick>);
    void setView (double start, double span);

    /* The plot, and content to x in it and back. */
    juce::Rectangle<float> plot() const;
    float xOf (double t) const noexcept;
    double timeAt (float x) const noexcept;

    std::function<void (double start)> onView;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    void moveTo (float x);

    juce::String caption;
    double lo = 0.0, hi = 1.0;
    std::vector<Block> blocks;
    std::vector<Tick> ticks;
    double viewStart = 0.0, viewSpan = 1.0;

    JUCE_DECLARE_NON_COPYABLE (Overview)
};

} // namespace ni::ui
