// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Light passed through a container. ChildLights.h has the rule.
 */
#include "ChildLights.h"

namespace ni::ui
{

void forwardChildLights (juce::Graphics& g, const juce::Component& container)
{
    juce::Graphics::ScopedSaveState state (g);
    excludeOwnBounds (g, container);
    paintChildLights (g, container);
}

void paintChildLight (juce::Graphics& g, const juce::Component& container, juce::Component& child)
{
    auto* lit = dynamic_cast<Luminous*> (&child);
    if (lit == nullptr || ! child.isVisible() || child.getParentComponent() != &container)
        return;

    juce::Graphics::ScopedSaveState state (g);
    g.addTransform (juce::AffineTransform::translation ((float) child.getX(), (float) child.getY())
                        .followedBy (child.getTransform()));
    lit->paintLight (g);
}

void excludeOwnBounds (juce::Graphics& g, const juce::Component& c)
{
    g.excludeClipRegion (c.getLocalBounds());
}

void relight (juce::Component& c)
{
    JUCE_ASSERT_MESSAGE_THREAD

    /* Upwards until an ancestor holds the whole of it: past that, nothing
     * above can be lit by it. */
    auto area = c.getLocalBounds().expanded (lightReach);
    juce::Component* from = &c;
    for (auto* p = c.getParentComponent(); p != nullptr; from = p, p = p->getParentComponent())
    {
        area = p->getLocalArea (from, area);
        p->repaint (area);
        if (p->getLocalBounds().contains (area))
            break;
    }
}

} // namespace ni::ui
