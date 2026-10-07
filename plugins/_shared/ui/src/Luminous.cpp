// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Light past a component's edge, painted by its parent. Luminous.h says why.
 */
#include "Luminous.h"

namespace ni::ui
{

void paintChildLights (juce::Graphics& g, const juce::Component& container)
{
    for (auto* child : container.getChildren())
    {
        auto* lit = dynamic_cast<Luminous*> (child);
        if (lit == nullptr || ! child->isVisible())
            continue;

        /* The child's own coordinates, as JUCE paints the child: its position,
         * then its transform. Not clipped to the child: that is the point. */
        juce::Graphics::ScopedSaveState state (g);
        g.addTransform (juce::AffineTransform::translation ((float) child->getX(), (float) child->getY())
                            .followedBy (child->getTransform()));
        lit->paintLight (g);
    }
}

void lightChanged (juce::Component& c)
{
    JUCE_ASSERT_MESSAGE_THREAD
    if (auto* parent = c.getParentComponent())
        parent->repaint (c.getBoundsInParent().expanded (lightReach));
}

} // namespace ni::ui
