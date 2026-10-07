// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Lists that open inside the window. Popup.h has the rules.
 */
#include "Popup.h"

#include "Info.h"

namespace ni::ui
{

juce::Component& windowOf (juce::Component& c)
{
    for (auto* p = c.getParentComponent(); p != nullptr; p = p->getParentComponent())
        if (dynamic_cast<InfoHost*> (p) != nullptr)
            return *p;
    return *c.getTopLevelComponent();
}

juce::Rectangle<int> placeInside (juce::Rectangle<int> window, juce::Rectangle<int> anchor,
                                  int width, int height, int overlap)
{
    const int h = juce::jmin (height, window.getHeight());
    const int w = juce::jmin (width, window.getWidth());
    const int x = juce::jlimit (window.getX(), window.getRight() - w, anchor.getX());

    const int below = anchor.getBottom() - overlap;
    if (below + h <= window.getBottom())
        return { x, below, w, h };

    const int above = anchor.getY() + overlap - h;
    if (above >= window.getY())
        return { x, above, w, h };

    return { x, window.getBottom() - h, w, h };
}

Popup::Popup (std::function<void (const juce::MouseEvent&)> d) : dismissed (std::move (d))
{
    setInterceptsMouseClicks (true, true);
    setAlwaysOnTop (true);
    /* A press here closes the list and moves no focus: the control that
     * opened it keeps the keyboard. */
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
}

Popup::~Popup() = default;

void Popup::show (juce::Component& window, juce::Component& content, juce::Rectangle<int> bounds)
{
    window.addAndMakeVisible (*this);
    setBounds (window.getLocalBounds());
    toFront (false);
    addAndMakeVisible (content);
    content.setBounds (bounds);
}

void Popup::mouseDown (const juce::MouseEvent& e)
{
    /* Only presses that reach this layer itself arrive here: outside the
     * content, which takes its own. */
    if (dismissed)
        dismissed (e);   // may delete this
}

} // namespace ni::ui
