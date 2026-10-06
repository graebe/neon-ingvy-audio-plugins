// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What every Controls page has: headings over its rows, and the lights of the
 * controls on it.
 *
 * A page paints no ground of its own -- the gallery's frame paints the window
 * ground under it, and a snapshot renders it on bg-000 -- but it is the
 * container its controls sit in, so it paints their light past their edges
 * (Luminous.h), over that ground and under them.
 *
 * The states a pointer or a keyboard would give a control -- hover, pressed,
 * visible focus -- are given to it with the events that make them
 * (Pointer.h), so a page shows them side by side and a snapshot holds them.
 */
#pragma once

#include "Luminous.h"
#include "UvTokens.h"
#include "UvType.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace ni::ui::gallery
{

class ControlsPage : public juce::Component
{
public:
    /* A heading in the label style, ink-muted, its line box at (x, y). */
    void heading (const juce::String& text, int x, int y) { headings.push_back ({ text, { x, y } }); }

    /* A note in the hint style, ink-dim: what a row shows that a picture
     * cannot say. */
    void note (const juce::String& text, int x, int y) { notes.push_back ({ text, { x, y } }); }

    /* Gives `c` visible keyboard focus, as a Tab onto it would. */
    static void showFocus (juce::Component& c) { c.focusGained (juce::Component::focusChangedByTabKey); }

    void paint (juce::Graphics& g) override
    {
        const auto& label = uv::tok::type::label;
        for (const auto& h : headings)
            uv::type::draw (g, uv::type::cased (label, h.text),
                            { (float) h.at.x, (float) h.at.y, (float) getWidth() - (float) h.at.x, label.lineHeight },
                            uv::type::font (label), uv::tok::colour::inkMuted);

        const auto& hint = uv::tok::type::hint;
        for (const auto& n : notes)
            uv::type::draw (g, n.text,
                            { (float) n.at.x, (float) n.at.y, (float) getWidth() - (float) n.at.x, hint.lineHeight },
                            uv::type::font (hint), uv::tok::colour::inkDim);

        ni::ui::paintChildLights (g, *this);
    }

private:
    struct Text
    {
        juce::String text;
        juce::Point<int> at;
    };
    std::vector<Text> headings, notes;
};

} // namespace ni::ui::gallery
