// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * One of the system's fifteen machine glyphs as a component -- the Icon card,
 * and the web kit's Icon.jsx.
 *
 * 16px, on its 16px grid, centred in whatever box it is given (a 16px slot in
 * a labelled button, the whole 28px of an icon-only one), in the colour of its
 * control: `ink` at rest, `on-uv` on a lit button, `bg-000` on the red record
 * button, `ink-dim` disabled. The control says which; this only draws it
 * (UvIcons.h has the glyphs and why they are recoloured, never copied).
 *
 * DECORATIVE TO ASSISTIVE TECHNOLOGY AND TO THE POINTER. An icon replaces a
 * word on a control, and the control carries that word as its accessible
 * title; an icon that announced itself would say the verb twice. And it takes
 * no mouse event of its own, so pointing at it is pointing at its control --
 * whose info line is what the hint bar shows.
 *
 * Never a glyph outside the set: an unknown name is a programming error,
 * asserted, and draws nothing (the Iconography rules).
 */
#pragma once

#include "UvTokens.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ni::ui
{

class Icon final : public juce::Component
{
public:
    /* The glyph's own size: "16px on a 28px control". */
    static constexpr int size = 16;

    explicit Icon (const juce::String& glyph = {}, juce::Colour tint = uv::tok::colour::ink);

    /* One of uv::iconNames(), or empty for none. */
    void setGlyph (const juce::String&);
    const juce::String& getGlyph() const noexcept { return glyph; }

    /* currentColor: the colour of the control it sits on. */
    void setTint (juce::Colour);
    juce::Colour getTint() const noexcept { return tint; }

    void paint (juce::Graphics&) override;

private:
    juce::String glyph;
    juce::Colour tint;

    JUCE_DECLARE_NON_COPYABLE (Icon)
};

} // namespace ni::ui
