// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The LookAndFeel: the default while an editor is open, put back after;
 * nothing of JUCE's own colour left in the widgets it styles; and the layout
 * numbers it takes from the web kit's CSS.
 */
#include "UvLookAndFeel.h"
#include "UvTokens.h"

#include <doctest.h>

namespace c = uv::tok::colour;

TEST_CASE ("look and feel: it is the default while one is shared, and the old one comes back")
{
    auto* before = &juce::LookAndFeel::getDefaultLookAndFeel();
    {
        uv::SharedLookAndFeel shared;
        CHECK (&juce::LookAndFeel::getDefaultLookAndFeel() == &shared.lookAndFeel);
    }
    CHECK (&juce::LookAndFeel::getDefaultLookAndFeel() == before);
}

TEST_CASE ("look and feel: the widgets JUCE colours by itself are in the system's colours")
{
    uv::LookAndFeel lnf;
    CHECK (lnf.findColour (juce::PopupMenu::backgroundColourId) == c::bg100);
    CHECK (lnf.findColour (juce::PopupMenu::highlightedBackgroundColourId) == c::bg300);
    CHECK (lnf.findColour (juce::CaretComponent::caretColourId) == c::uv);
    CHECK (lnf.findColour (juce::TextEditor::focusedOutlineColourId) == c::uv);
    CHECK (lnf.findColour (juce::ResizableWindow::backgroundColourId) == c::bg000);
    CHECK (lnf.findColour (juce::TextButton::textColourOnId) == c::onUv);
}

TEST_CASE ("look and feel: a select's text sits 12px inside its border, clear of the caret")
{
    uv::LookAndFeel lnf;
    juce::ComboBox box;
    box.setLookAndFeel (&lnf);
    box.setBounds (0, 0, 104, 28);
    juce::Label label;
    lnf.positionComboBoxText (box, label);
    CHECK (label.getX() == 13);                 // 1px border + 12px padding
    CHECK (label.getRight() == 104 - 1 - 28);   // the caret's 28px
    CHECK (label.getBorderSize() == juce::BorderSize<int>());
    box.setLookAndFeel (nullptr);
}

TEST_CASE ("look and feel: a knob's readout sits 8px under it")
{
    uv::LookAndFeel lnf;
    juce::Slider knob (juce::Slider::RotaryVerticalDrag, juce::Slider::TextBoxBelow);
    knob.setLookAndFeel (&lnf);
    knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 28);
    knob.setBounds (0, 0, 64, 48 + 8 + 28);
    const auto layout = lnf.getSliderLayout (knob);
    CHECK (layout.sliderBounds.getHeight() == 48);
    CHECK (layout.textBoxBounds.getY() == 56);
    CHECK (layout.textBoxBounds.getHeight() == 28);
    knob.setLookAndFeel (nullptr);
}
