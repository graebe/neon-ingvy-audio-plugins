// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The publisher's signature: a 6px square lit in uv, then NEON INGVY -- the
 * Signature card of Ultraviolet 1.1.0, and the web kit's Signature.jsx.
 *
 * THE CARD IS EXACT: "a 6px square lit in `uv` with `glow-led`, then NEON
 * INGVY in 10px mono, weight 500, tracked 0.2em, uppercase, `ink-muted`, 8px
 * apart. Every plugin window carries it exactly once, at the right end of the
 * `Hint` bar, level with the hint text." So the Hint owns one (Hint.h): an
 * editor cannot forget it, put it elsewhere or show two.
 *
 * THE NAME IS FIXED AND TAKES NO ARGUMENT. "Consumer provides: nothing", and
 * a name parameter would be an invitation to put something else there, which
 * the same card forbids ("do not ... pair it with another logo"). What the
 * editor does provide is its info line -- a description, not a name -- with
 * ni::ui::setInfo, like every other line in the bar.
 *
 * ITS LIGHT REACHES PAST ITS EDGE: glow-led around a 6px mark in a 14px line
 * is mostly outside the component, so the mark's halo is Luminous and its
 * container paints it (Luminous.h). The container is the Hint, which does.
 *
 * Sized by itself (preferredWidth() x height); place it, never stretch it.
 */
#pragma once

#include "Luminous.h"
#include "UvTokens.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ni::ui
{

class Signature final : public juce::Component, public Luminous
{
public:
    Signature();

    /* The words, as the card sets them: uppercase by the style. */
    static constexpr const char* name = "Neon Ingvy";

    /* The card's type: the hint style's size and line, weight 500, 0.2em. */
    static constexpr uv::tok::TextStyle style { uv::tok::type::hint.size, uv::tok::type::hint.lineHeight,
                                                500, 0.2f };

    /* The mark, and the gap between it and the words. */
    static constexpr float markSize = 6.0f;
    static constexpr float gap = uv::tok::space::space2;

    /* One hint line tall; as wide as the mark, the gap and the words. */
    static constexpr int height = (int) uv::tok::type::hint.lineHeight;
    static int preferredWidth();

    /* The lit square, in this component's coordinates. */
    juce::Rectangle<float> markBounds() const;

    void paint (juce::Graphics&) override;
    void paintLight (juce::Graphics&) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    JUCE_DECLARE_NON_COPYABLE (Signature)
};

} // namespace ni::ui
