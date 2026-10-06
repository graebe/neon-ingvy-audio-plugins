// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A pressed control. Pressable.h says how it is pressed, and why it is not a
 * juce::Button.
 */
#include "Pressable.h"

#include "ChildLights.h"
#include "UvLight.h"

namespace ni::ui
{

namespace
{
/* What a screen reader is told: the role, the press, and on or off for a
 * control that has a state. */
class PressableAccessibility final : public juce::AccessibilityHandler
{
public:
    PressableAccessibility (Pressable& p, juce::AccessibilityRole role,
                            std::function<std::optional<bool>()> checkedFn)
        : juce::AccessibilityHandler (p, role, actionsFor (p, checkedFn().has_value())),
          checked (std::move (checkedFn))
    {
    }

    juce::AccessibleState getCurrentState() const override
    {
        auto state = juce::AccessibilityHandler::getCurrentState();
        if (const auto on = checked())
        {
            state = state.withCheckable();
            if (*on)
                state = state.withChecked();
        }
        return state;
    }

private:
    static juce::AccessibilityActions actionsFor (Pressable& p, bool checkable)
    {
        juce::AccessibilityActions actions;
        actions.addAction (juce::AccessibilityActionType::press, [&p] { p.press(); });
        if (checkable)
            actions.addAction (juce::AccessibilityActionType::toggle, [&p] { p.press(); });
        return actions;
    }

    std::function<std::optional<bool>()> checked;
};
} // namespace

Pressable::Pressable (juce::AccessibilityRole r) : role (r)
{
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

Pressable::~Pressable() = default;

void Pressable::press()
{
    if (isEnabled())
        pressed();
}

void Pressable::stateChanged()
{
    repaint();
    relight (*this);
    if (auto* parent = dynamic_cast<PressableParent*> (getParentComponent()))
        parent->pressableStateChanged (*this);
}

void Pressable::checkedChanged()
{
    stateChanged();
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
}

void Pressable::setHovered (bool over, juce::Point<float> at)
{
    pointerAt = at;
    if (hovered != over)
    {
        hovered = over;
        stateChanged();
    }
}

/* ============================================================ pointer == */

void Pressable::mouseEnter (const juce::MouseEvent& e) { setHovered (true, e.position); }

void Pressable::mouseMove (const juce::MouseEvent& e)
{
    /* The position is kept for a control whose hover is only part of it (the
     * switch's housing); that control repaints for a move itself. */
    setHovered (true, e.position);
}

void Pressable::mouseExit (const juce::MouseEvent& e) { setHovered (false, e.position); }

void Pressable::mouseDown (const juce::MouseEvent& e)
{
    focus.pointerUsed();

    /* The primary button presses, as a browser's click does; the others
     * belong to whatever menu the host has. */
    if (isEnabled() && e.mods.isLeftButtonDown())
        pointerDown = true;

    stateChanged();
}

void Pressable::mouseDrag (const juce::MouseEvent& e)
{
    /* Still pressed while held, wherever the pointer goes (CSS :active);
     * hovered only while over it. */
    setHovered (getLocalBounds().toFloat().contains (e.position), e.position);
}

void Pressable::mouseUp (const juce::MouseEvent& e)
{
    if (! pointerDown)
        return;

    pointerDown = false;
    const bool over = getLocalBounds().toFloat().contains (e.position);
    setHovered (over, e.position);
    stateChanged();

    if (over)
        press();   // may delete this
}

/* =========================================================== keyboard == */

bool Pressable::keyPressed (const juce::KeyPress& key)
{
    if (! isEnabled())
        return false;

    if (key.isKeyCode (juce::KeyPress::returnKey))
    {
        focus.keyUsed();
        stateChanged();
        press();   // may delete this
        return true;
    }

    if (key.isKeyCode (juce::KeyPress::spaceKey))
    {
        focus.keyUsed();
        if (! spaceDown)
        {
            spaceDown = true;
            stateChanged();
        }
        return true;   // a repeat while held: still the one press
    }

    return false;
}

bool Pressable::keyStateChanged (bool)
{
    if (! spaceDown || juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::spaceKey))
        return false;

    spaceDown = false;
    stateChanged();
    press();   // may delete this
    return true;
}

/* ============================================================== focus == */

void Pressable::focusGained (FocusChangeType cause)
{
    focus.focusGained (cause);
    stateChanged();
}

void Pressable::focusLost (FocusChangeType)
{
    /* A Space held while the focus goes elsewhere presses nothing, as in a
     * browser. */
    spaceDown = false;
    focus.focusLost();
    stateChanged();
}

void Pressable::enablementChanged()
{
    if (! isEnabled())
    {
        pointerDown = false;
        spaceDown = false;
    }
    /* .btn:disabled { cursor: default } */
    setMouseCursor (isEnabled() ? juce::MouseCursor::PointingHandCursor
                                : juce::MouseCursor::NormalCursor);
    checkedChanged();
}

/* ============================================================== light == */

void Pressable::paintLight (juce::Graphics& g)
{
    if (isFocusShown())
        uv::light::glowFocus (g, getLocalBounds().toFloat());
}

/* ====================================================== accessibility == */

std::unique_ptr<juce::AccessibilityHandler> Pressable::createAccessibilityHandler()
{
    return std::make_unique<PressableAccessibility> (*this, role, [this] { return checkedState(); });
}

} // namespace ni::ui
