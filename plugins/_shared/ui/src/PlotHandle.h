// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A HANDLE ON A PLOT: a small ink dot in a generous target that drags, steps
 * and resets the one or two host parameters it stands for -- sideways one,
 * up and down the other. A handle you cannot grab reads as one that does not
 * work, so the target is three times the dot. Under the pointer or with
 * visible keyboard focus it lights uv; keyboard focus adds glow-focus. To
 * assistive technology it is a slider on its sideways parameter.
 *
 * THE OWNER SAYS WHERE IT IS AND WHAT A PIXEL IS WORTH. The handle knows
 * nothing about the picture under it: its owner places it (setBounds centred
 * on the point, size by size) and hands it, per axis, a Travel -- the plain
 * value a drag of so many pixels from the press makes of the value the press
 * found. A cycle's percent, a frequency's octaves, a gain's decibels: the
 * owner's scale, never one guessed here.
 *
 * A DRAG, AND EVERY RULE IN IT IS A FIX FOR A REAL FAULT (Side-Chain's
 * Shaper.jsx, then its native Shaper):
 *
 *  - The sensitivity is chosen AT PRESS and never re-read: the travel is from
 *    the press, so changing the divisor half way would make the value jump.
 *    Shift at the press is fine control: a fifth of the travel.
 *  - Positions are the WELL's, not the handle's: the handle moves under the
 *    pointer as it drags, and the well does not.
 *  - THE GESTURE OPENS ONCE THE DEAD ZONE IS LEFT, not at the press: a press
 *    that moves nothing -- a click, either half of a double-click -- is no
 *    edit.
 *  - Both parameters open and close together, so the host sees one gesture
 *    and undo is one step.
 *  - The handle does not animate: a curve editor whose handles glide is one
 *    you cannot aim at.
 *
 * THE KEYBOARD: Left and Right move the sideways parameter, Page by a tenth,
 * Home and End to the ends of its range; Up and Down move the other the way
 * dragging up and down does. Each key is one committed edit. A double-click
 * resets both to the plugin's defaults.
 *
 * Message thread only.
 */
#pragma once

#include "Focus.h"
#include "ParamBinding.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>

namespace ni::ui
{

class PlotHandle : public juce::Component
{
public:
    static constexpr int size = 32;
    static constexpr float dotR = 4.5f;
    static constexpr float targetR = dotR * 3.0f;
    static constexpr float deadZone = 4.0f;
    static constexpr float fineRatio = 5.0f;

    /* The plain value a drag makes: from the value at the press and the
     * pointer's travel since, in pixels (Shift's ratio already divided out;
     * right and down are positive). */
    using Travel = std::function<float (float plainAtPress, float pixels)>;

    /* `well` is the picture the handle sits on, whose pixels a drag is
     * measured in. `y` may be null: a handle that only moves sideways. */
    PlotHandle (juce::Component& well, ParamBinding& x, Travel alongX, ParamBinding* y = nullptr, Travel alongY = {});

    ParamBinding& xBinding() const noexcept { return x; }
    ParamBinding* yBinding() const noexcept { return y; }

    /* What a screen reader hears: the sideways parameter's text, then the
     * other's, joined. An owner whose parameters read differently on the
     * handle overrides both. */
    virtual juce::String valueText() const;
    /* Typed text, read by the sideways parameter: one committed edit, or
     * nothing for text it cannot read. */
    virtual void typeValue (const juce::String&);

    bool isLit() const noexcept { return hovered || focus.isVisible(); }

    bool hitTest (int x, int y) override;
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override;
    void focusLost (FocusChangeType) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    struct Drag
    {
        juce::Point<float> from;   // the press, in the well's pixels
        float plainX = 0.0f;       // the sideways parameter's value at the press
        float plainY = 0.0f;       // and the other's
        bool fine = false;         // Shift at the press, never re-read
        bool moved = false;        // out of the dead zone yet
    };

    static void commitKey (ParamBinding&, float delta, std::optional<float> to);

    juce::Component& well;
    ParamBinding& x;
    ParamBinding* const y;
    const Travel alongX, alongY;
    bool hovered = false;
    std::optional<Drag> drag;
    FocusVisibility focus { *this };

    JUCE_DECLARE_NON_COPYABLE (PlotHandle)
};

} // namespace ni::ui
