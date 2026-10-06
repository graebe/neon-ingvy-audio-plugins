// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Keyboard focus that shows only when the keyboard put it there: the web's
 * :focus-visible, for JUCE Components.
 *
 * THE RULE (Interaction conventions, Hint): every pointer control is reachable
 * by Tab and operable from the keyboard, and its focus ring -- glow-focus --
 * and its info in the hint bar show while the control has VISIBLE keyboard
 * focus. A click focuses a knob too, and a ring or a hint line left behind by
 * a click nobody thinks of as focusing is a stuck ring and a stuck bar. So:
 *
 *   focus by Tab (either way)        visible
 *   focus by a click                 not visible
 *   focus given by code              not visible, until a key is used on it
 *   a key the control handles        visible from then on
 *   a press on the control           not visible from then on
 *   focus lost                       not visible
 *
 * JUCE reports the first three as Component::FocusChangeType; the last three
 * are the control's to report, which is what this class is for. A control
 * owns one, forwards its focusGained / focusLost / keyPressed / mouseDown to
 * it, and paints glow-focus while isVisible(). The state is also kept as the
 * component property below, so code that only has the Component -- the
 * LookAndFeel drawing a stock JUCE widget -- can ask too.
 *
 * And it tells the window: when visibility changes, the nearest InfoHost above
 * the control (Info.h) is told to show or drop the control's info string.
 */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ni::ui
{

/* The property FocusVisibility keeps on its component. */
inline const juce::Identifier focusVisibleProperty { "ni.focusVisible" };

/* Whether `c` has keyboard focus that should be seen: the property, when a
 * FocusVisibility keeps one; otherwise plain keyboard focus, for a stock JUCE
 * widget that has no such helper. */
bool isFocusVisible (const juce::Component& c);

class FocusVisibility
{
public:
    explicit FocusVisibility (juce::Component& owner);
    ~FocusVisibility();

    /* From the owner's own overrides. */
    void focusGained (juce::Component::FocusChangeType cause);
    void focusLost();
    void keyUsed();
    void pointerUsed();

    bool isVisible() const noexcept { return visible; }

private:
    void set (bool shouldBeVisible);

    juce::Component& owner;
    bool focused = false;
    bool visible = false;

    JUCE_DECLARE_NON_COPYABLE (FocusVisibility)
};

} // namespace ni::ui
