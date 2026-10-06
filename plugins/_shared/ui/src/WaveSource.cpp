// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Ground's boxes. WaveSource.h says which they are.
 */
#include "WaveSource.h"

namespace ni::ui
{

namespace
{
const juce::Identifier sourceProperty { "ni.waveSource" };

void collect (const juce::Component& c, const juce::Component& relativeTo,
              std::vector<juce::Rectangle<float>>& out)
{
    if (isWaveSource (c))
    {
        /* Through every transform between them, so a scaled design still
         * puts its walls where its panels are drawn. */
        const auto box = relativeTo.getLocalArea (&c, c.getLocalBounds().toFloat());
        if (! box.isEmpty())
            out.push_back (box);
        return;
    }

    /* A hidden child is no wall, and neither is anything inside it; the root
     * is the caller's, shown or not. */
    for (auto* child : c.getChildren())
        if (child->isVisible())
            collect (*child, relativeTo, out);
}
} // namespace

void setWaveSource (juce::Component& c, bool isSource)
{
    if (isSource)
        c.getProperties().set (sourceProperty, true);
    else
        c.getProperties().remove (sourceProperty);
}

bool isWaveSource (const juce::Component& c)
{
    return (bool) c.getProperties()[sourceProperty];
}

std::vector<juce::Rectangle<float>> collectWaveSources (const juce::Component& root,
                                                       const juce::Component& relativeTo)
{
    std::vector<juce::Rectangle<float>> out;
    collect (root, relativeTo, out);
    return out;
}

} // namespace ni::ui
