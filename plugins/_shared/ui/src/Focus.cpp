// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Visible keyboard focus. Focus.h has the rule.
 */
#include "Focus.h"

#include "Info.h"

namespace ni::ui
{

bool isFocusVisible (const juce::Component& c)
{
    const auto* kept = c.getProperties().getVarPointer (focusVisibleProperty);
    if (kept != nullptr)
        return (bool) *kept;
    return c.hasKeyboardFocus (false);
}

FocusVisibility::FocusVisibility (juce::Component& o) : owner (o)
{
    owner.getProperties().set (focusVisibleProperty, false);
}

FocusVisibility::~FocusVisibility()
{
    owner.getProperties().remove (focusVisibleProperty);
}

void FocusVisibility::focusGained (juce::Component::FocusChangeType cause)
{
    focused = true;
    set (cause == juce::Component::focusChangedByTabKey);
}

void FocusVisibility::focusLost()
{
    focused = false;
    set (false);
}

void FocusVisibility::keyUsed()
{
    /* Only the focus it has: a key cannot reach a control that has none, but
     * a control may forward one it was handed. */
    if (focused)
        set (true);
}

void FocusVisibility::pointerUsed()
{
    set (false);
}

void FocusVisibility::set (bool shouldBeVisible)
{
    if (visible == shouldBeVisible)
        return;

    visible = shouldBeVisible;
    owner.getProperties().set (focusVisibleProperty, visible);
    owner.repaint();

    /* The bar shows the line of a control with visible focus, and drops it
     * (after the grace) when the focus goes or stops being visible. */
    if (auto* host = InfoHost::find (owner))
    {
        auto& info = host->infoState();
        if (visible)
        {
            auto* source = infoSource (&owner);
            info.focus (source != nullptr ? infoOf (*source) : juce::String(), source);
        }
        else
        {
            info.blur();
        }
    }
}

} // namespace ni::ui
