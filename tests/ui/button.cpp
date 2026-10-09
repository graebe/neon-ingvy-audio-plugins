// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Button: pressed by a click released over it and by Enter at once, never
 * by Space, which is the host's; lit while pressed or on; the two shapes'
 * sizes; and the Actions groups, joined edge to edge or stacked. Then every
 * state, as a picture.
 */
#include "Button.h"
#include "Gallery.h"
#include "Pointer.h"
#include "checks.h"
#include "hostkeys.h"
#include "snapshot.h"

#include <doctest.h>

using ni::ui::Button;
using ni::ui::ButtonGroup;
using ni::ui::gallery::Pointer;

namespace
{
/* A button 100 x 28 that counts its clicks. */
struct Counted
{
    Counted()
    {
        button.setBounds (0, 0, 100, 28);
        button.onClick = [this] { ++clicks; };
    }
    Button button { "Copy slot", "copy" };
    int clicks = 0;
};

std::unique_ptr<juce::AccessibilityHandler> accessibilityOf (juce::Component& c)
{
    return c.createAccessibilityHandler();
}
} // namespace

TEST_CASE ("button: a click released over it presses it; one dragged off and released does not")
{
    Counted b;
    Pointer p;

    p.click (b.button, { 50, 14 });
    CHECK (b.clicks == 1);

    p.down (b.button, { 50, 14 });
    CHECK (b.button.isPressed());
    p.drag ({ 150, 14 });
    CHECK (b.button.isPressed());       // still held: CSS :active
    CHECK_FALSE (b.button.isHovered());
    p.up();
    CHECK_FALSE (b.button.isPressed());
    CHECK (b.clicks == 1);
}

TEST_CASE ("button: the right button does not press it")
{
    Counted b;
    b.button.mouseDown (Pointer::event (b.button, { 50, 14 }, juce::ModifierKeys::rightButtonModifier));
    CHECK_FALSE (b.button.isPressed());
    b.button.mouseUp (Pointer::event (b.button, { 50, 14 }));
    CHECK (b.clicks == 0);
}

TEST_CASE ("button: Enter presses at once; Space goes to the host and presses nothing")
{
    Counted b;

    CHECK (ni::ui::gallery::key (b.button, juce::KeyPress::returnKey));
    CHECK (b.clicks == 1);

    /* Space down, a repeat, then up: none of it is the button's. */
    CHECK_FALSE (ni::ui::test::windowUses (b.button, ni::ui::test::spaceKey));
    CHECK_FALSE (ni::ui::test::windowUses (b.button, ni::ui::test::spaceKey));
    CHECK_FALSE (b.button.isPressed());
    CHECK (b.clicks == 1);

    /* A key that is not a press is not taken: Tab goes through. */
    CHECK_FALSE (ni::ui::gallery::key (b.button, juce::KeyPress::tabKey));
}

TEST_CASE ("button: disabled, nothing presses it and it shows no hover")
{
    Counted b;
    Pointer p;
    b.button.setEnabled (false);

    p.click (b.button, { 50, 14 });
    ni::ui::gallery::key (b.button, juce::KeyPress::returnKey);
    b.button.press();
    CHECK (b.clicks == 0);
    CHECK_FALSE (b.button.isLit());
}

TEST_CASE ("button: lit while pressed or on, and primary always")
{
    Counted b;
    Pointer p;
    CHECK_FALSE (b.button.isLit());

    p.down (b.button, { 10, 10 });
    CHECK (b.button.isLit());
    p.up();
    CHECK_FALSE (b.button.isLit());

    b.button.setOn (true);
    CHECK (b.button.isLit());
    b.button.setOn (false);
    b.button.setPrimary (true);
    CHECK (b.button.isLit());
}

TEST_CASE ("button: focus shows for the keyboard, never after a click")
{
    Counted b;
    Pointer p;

    b.button.focusGained (juce::Component::focusChangedByMouseClick);
    CHECK_FALSE (b.button.isFocusShown());

    b.button.focusGained (juce::Component::focusChangedByTabKey);
    CHECK (b.button.isFocusShown());

    p.down (b.button, { 10, 10 });
    CHECK_FALSE (b.button.isFocusShown());
    p.up();
}

TEST_CASE ("button: the square is control-h; a word is padding, glyph, gap and word")
{
    Button square ({}, "copy");
    CHECK (square.isIconOnly());
    CHECK (square.idealWidth() == 28);

    Button word ("Copy slot");
    Button both ("Copy slot", "copy");
    /* .btn { padding: 0 12px; gap: 8px } inside a 1px border, a 16px glyph. */
    CHECK (both.idealWidth() == word.idealWidth() + 16 + 8);
    CHECK (word.idealWidth() > 2 + 24);
}

TEST_CASE ("button: a screen reader hears the verb, the line, and a latching button's state")
{
    constexpr ni::ui::InfoText info { "Copy slot — put this slot, pattern and sound, on the clipboard." };
    Button copy ({}, "copy");
    copy.setTitle ("Copy slot");
    ni::ui::setInfo (copy, info);

    auto handler = accessibilityOf (copy);
    REQUIRE (handler != nullptr);
    CHECK (handler->getRole() == juce::AccessibilityRole::button);
    CHECK (handler->getTitle() == "Copy slot");
    CHECK (handler->getDescription().startsWith ("Copy slot"));
    CHECK_FALSE (handler->getCurrentState().isCheckable());

    int clicks = 0;
    copy.onClick = [&] { ++clicks; };
    CHECK (handler->getActions().invoke (juce::AccessibilityActionType::press));
    CHECK (clicks == 1);

    Button pause ({}, "pause");
    pause.setOn (true);
    auto latched = accessibilityOf (pause);
    CHECK (latched->getCurrentState().isCheckable());
    CHECK (latched->getCurrentState().isChecked());
}

TEST_CASE ("button: the word is the title until the button is named otherwise")
{
    Button b ("Copy");
    CHECK (b.getTitle() == "Copy");
    b.setText ("Copy slot");
    CHECK (b.getTitle() == "Copy slot");
    b.setTitle ("Copy this slot");
    b.setText ("Copy");
    CHECK (b.getTitle() == "Copy this slot");
}

/* ------------------------------------------------------------- groups -- */

TEST_CASE ("button group: joined buttons share a hairline, five icons in 136px")
{
    ButtonGroup group;
    juce::OwnedArray<Button> buttons;
    for (const auto* icon : { "copy", "paste", "export", "export-all", "import" })
        group.add (*buttons.add (new Button ({}, icon)));

    CHECK (group.idealSize() == juce::Rectangle<int> (136, 28));
    group.setBounds (group.idealSize());
    for (int i = 0; i < 5; ++i)
        CHECK (buttons[i]->getBounds() == juce::Rectangle<int> (i * 27, 0, 28, 28));
}

TEST_CASE ("button group: a column joins them top to bottom, six icons in 163px")
{
    ButtonGroup group (ButtonGroup::Form::column);
    juce::OwnedArray<Button> buttons;
    for (const auto* icon : { "copy", "paste", "export", "export-all", "import", "shuffle" })
        group.add (*buttons.add (new Button ({}, icon)));

    CHECK (group.idealSize() == juce::Rectangle<int> (28, 163));
    group.setBounds (group.idealSize());
    for (int i = 0; i < 6; ++i)
        CHECK (buttons[i]->getBounds() == juce::Rectangle<int> (0, i * 27, 28, 28));

    /* Raised as a row raises them, and Tab still goes top to bottom. */
    Pointer p;
    p.enter (*buttons[2]);
    CHECK (group.getChildComponent (group.getNumChildComponents() - 1) == buttons[2]);
    p.exit (*buttons[2]);
    CHECK (buttons[0]->getExplicitFocusOrder() < buttons[5]->getExplicitFocusOrder());
}

TEST_CASE ("button group: the hovered or focused button is raised over its neighbours")
{
    ButtonGroup group;
    juce::OwnedArray<Button> buttons;
    for (const auto* icon : { "copy", "paste", "export" })
        group.add (*buttons.add (new Button ({}, icon)));
    group.setBounds (group.idealSize());

    const auto top = [&] { return group.getChildComponent (group.getNumChildComponents() - 1); };
    CHECK (top() == buttons[2]);

    Pointer p;
    p.enter (*buttons[0]);
    CHECK (top() == buttons[0]);
    p.exit (*buttons[0]);
    CHECK (top() == buttons[2]);   // back in order

    buttons[1]->focusGained (juce::Component::focusChangedByTabKey);
    CHECK (top() == buttons[1]);

    /* Tab still goes left to right, whatever is on top. */
    CHECK (buttons[0]->getExplicitFocusOrder() < buttons[1]->getExplicitFocusOrder());
    CHECK (buttons[1]->getExplicitFocusOrder() < buttons[2]->getExplicitFocusOrder());
}

TEST_CASE ("button group: a stack is as wide as its widest, space-2 apart, words from the left")
{
    ButtonGroup stack (ButtonGroup::Form::stack);
    Button copy ("Copy slot", "copy"), import ("Import", "import");
    stack.add (copy);
    stack.add (import);

    const int widest = juce::jmax (copy.idealWidth(), import.idealWidth());
    CHECK (stack.idealSize() == juce::Rectangle<int> (widest, 28 + 8 + 28));
    stack.setBounds (stack.idealSize());
    CHECK (import.getBounds() == juce::Rectangle<int> (0, 36, widest, 28));
}

/* ------------------------------------------------------------ picture -- */

NI_SNAPSHOT_TEST ("button: every state, both shapes, and the Actions groups")
{
    ni::ui::gallery::Frame frame (ni::ui::gallery::pages());
    REQUIRE (frame.show (juce::String ("controls-button")));
    REQUIRE (frame.page() != nullptr);
    NI_CHECK_INFO_LIMIT (*frame.page());
    NI_CHECK_SNAPSHOT (*frame.page(), "controls-button");
}
