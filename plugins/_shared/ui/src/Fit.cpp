// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A fixed design, scaled to fit. Fit.h says why scale rather than reflow.
 */
#include "Fit.h"

#include <cmath>

namespace ni::ui
{

float fitScale (float viewportWidth, float designWidth)
{
    jassert (designWidth > 0.0f);
    const float viewport = viewportWidth > 0.0f ? viewportWidth : designWidth;
    return juce::jmax (minFitScale, viewport / designWidth);
}

int scaledHeight (float designHeight, float scale)
{
    /* Up, so the last row of the design is never cut by a rounding. The
     * epsilon keeps an exact product (640 x 1.5) from rounding up past it. */
    return (int) std::ceil (designHeight * scale - 1.0e-4f);
}

void constrainToDesign (juce::ComponentBoundsConstrainer& c, int designWidth, int designHeight,
                        float minScale, float maxScale)
{
    jassert (designWidth > 0 && designHeight > 0 && minScale > 0.0f && maxScale >= minScale);
    c.setFixedAspectRatio ((double) designWidth / (double) designHeight);
    c.setSizeLimits (juce::roundToInt ((float) designWidth * minScale),
                     juce::roundToInt ((float) designHeight * minScale),
                     juce::roundToInt ((float) designWidth * maxScale),
                     juce::roundToInt ((float) designHeight * maxScale));
}

FixedDesign::FixedDesign (juce::Component& d, int w, int h)
    : design (d), width (w), height (h)
{
    jassert (w > 0 && h > 0);
    addAndMakeVisible (design);
    design.setBounds (0, 0, width, height);
    setSize (width, height);
}

FixedDesign::~FixedDesign()
{
    removeChildComponent (&design);
}

juce::Rectangle<int> FixedDesign::boundsAt (float s) const
{
    return { juce::roundToInt ((float) width * s), scaledHeight ((float) height, s) };
}

void FixedDesign::setDesignSize (int w, int h)
{
    jassert (w > 0 && h > 0);
    if (w == width && h == height)
        return;
    width = w;
    height = h;
    /* At the scale it has: the host's window is resized to boundsAt() next,
     * and only then does the fit have the size it is meant to fill. */
    design.setBounds (0, 0, width, height);
    design.setTransform (juce::AffineTransform::scale (scale));
}

void FixedDesign::resized()
{
    /* Whichever way fits: with constrainToDesign the two agree, and without
     * it the design stays whole inside the window rather than past an edge. */
    scale = juce::jmin (fitScale ((float) getWidth(), (float) width),
                        fitScale ((float) getHeight(), (float) height));

    design.setBounds (0, 0, width, height);
    design.setTransform (juce::AffineTransform::scale (scale));
}

} // namespace ni::ui
