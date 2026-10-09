// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A control that is pressed: the part a Button, a Toggle, a tab and the
 * CheckList's face have in common -- hover, the pressed state, the keys that
 * press it, its focus ring and what a screen reader is told.
 *
 * NOT juce::Button, for two reasons that each show in a window. Its
 * keyboard click is posted (triggerClick) and arrives a message later, with a
 * 100 ms "down" flash on a timer -- an animated control, and a state that
 * hangs off a timer. And its hover is the real mouse's, so a test or a
 * gallery page cannot show the state at all. This is a browser <button>,
 * which is what the web kit's controls are, but for Space:
 *
 *   pointer   pressed while the button is held down on it (CSS :active), a
 *             press when it is released over it; hover while it is over it
 *   Enter     a press, at once, every time the key repeats
 *   a client  an accessibility client's press (and toggle) action
 *
 * SPACE IS NOT A PRESS. In a plugin window Space is the host's transport
 * (Keys.h), so a focused button leaves it to the host, as every other control
 * does.
 *
 * Every press is synchronous and on the message thread. A press may delete
 * the control (a press that closes the panel it is in), so nothing here
 * touches it afterwards.
 *
 * FOCUS shows only for the keyboard (Focus.h). Its ring is glow-focus round
 * the control's bounds, which is light past its edge: Luminous, painted by
 * the parent (ChildLights.h has the rule). A subclass adds its own light to
 * paintLight() -- a lit button's glow-led -- after this one's.
 *
 * A container that has to know when a pressable's state changes -- a joined
 * group raising the hovered button over its neighbours -- implements
 * PressableParent.
 */
#pragma once

#include "Focus.h"
#include "Luminous.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

namespace ni::ui
{

class Pressable;

/* The parent of a pressable that is told when one of its children changes
 * how it looks: hover, pressed, focus or lit. */
class PressableParent
{
public:
    virtual ~PressableParent() = default;
    virtual void pressableStateChanged (Pressable&) = 0;
};

class Pressable : public juce::Component,
                  public Luminous
{
public:
    explicit Pressable (juce::AccessibilityRole role = juce::AccessibilityRole::button);
    ~Pressable() override;

    /* What it looks like now. */
    bool isHovered() const noexcept { return hovered; }
    bool isPressed() const noexcept { return pointerDown; }
    bool isFocusShown() const { return isFocusVisible (*this); }

    /* Where the pointer was last seen over it, in its own coordinates. */
    juce::Point<float> pointerPosition() const noexcept { return pointerAt; }

    /* Presses it, as a click would: nothing when disabled. */
    void press();

    /* ---- juce::Component */
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override;
    void focusLost (FocusChangeType) override;
    void enablementChanged() override;

    /* ---- Luminous: the focus ring, which is all outside the bounds */
    void paintLight (juce::Graphics&) override;

protected:
    /* What a press means: a Button's click, a Toggle's flip, a tab's choice. */
    virtual void pressed() = 0;

    /* For an accessibility client: on or off, for a control that has a state
     * (a switch, a latching button, a tab); nothing for a plain button. */
    virtual std::optional<bool> checkedState() const { return std::nullopt; }

    /* Something this looks like changed: repaints, relights, tells a
     * PressableParent. A subclass calls it when its own look changes (lit,
     * on); checkedChanged() when its state for a screen reader does too. */
    void stateChanged();
    void checkedChanged();

    FocusVisibility focus { *this };

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    void setHovered (bool, juce::Point<float>);

    const juce::AccessibilityRole role;
    bool hovered = false;
    bool pointerDown = false;
    juce::Point<float> pointerAt;

    JUCE_DECLARE_NON_COPYABLE (Pressable)
};

} // namespace ni::ui
