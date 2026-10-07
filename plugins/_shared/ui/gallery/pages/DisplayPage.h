// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What the Display pages share: a page that lays its states out under small
 * captions, on the window ground, with the light of every Luminous child
 * painted where CSS paints it.
 *
 * A page is a scene (Gallery.h): it sizes itself, leaves the ground showing,
 * and holds each state of its component side by side -- so the snapshot of
 * the page is the picture of every state at once.
 */
#pragma once

#include "Luminous.h"
#include "UvTokens.h"
#include "UvType.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace ni::ui::gallery
{

class DisplayPage : public juce::Component
{
public:
    /* A caption over a state: the label style, ink-muted, left-aligned. */
    void caption (const juce::String& text, juce::Rectangle<float> area)
    {
        captions.push_back ({ text, area });
    }

    void paint (juce::Graphics& g) override
    {
        const auto& style = uv::tok::type::label;
        for (const auto& c : captions)
            uv::type::draw (g, uv::type::cased (style, c.text), c.area, uv::type::font (style),
                            uv::tok::colour::inkMuted);
        paintOver (g);
        paintChildLights (g, *this);
    }

protected:
    /* Whatever a page draws itself, under its children's light. */
    virtual void paintOver (juce::Graphics&) {}

private:
    struct Caption
    {
        juce::String text;
        juce::Rectangle<float> area;
    };
    std::vector<Caption> captions;
};

} // namespace ni::ui::gallery
