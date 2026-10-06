// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A switch: the Toggle card's switch form, and the web kit's Toggle.jsx.
 *
 * A 28 x 14 bg-200 housing on a line-200 hairline, an 8 x 8 square 2px in from
 * its left that moves to 16px and lights -- uv with glow-led, and the housing's
 * hairline uv -- and the label space-2 to the right, in the `label` style
 * (11px capitals, tracked, ink-muted). The row is control-h tall, so a switch
 * sits on the same line as a select or a button beside it.
 *
 *   hover      the housing rises to bg-300 while the pointer is over IT -- not
 *              over the label: CSS's .switch:hover
 *   disabled   no fill, a line-100 hairline, the square and the label ink-dim:
 *              the system's disabled state, and a CheckList row that is shown
 *              but refused (a bus at another sample rate)
 *   focus      glow-focus round the whole row, for the keyboard only
 *
 * THE SWITCH FORM IS FOR WHAT A USER SETS; the LED form is for state the plugin
 * reports, and saying it the other way round would tell the user the plugin is
 * reporting something when it is asking.
 *
 * IT DOES NOT FLIP ITSELF. A click, Space or Enter asks for the other state
 * (onChange (!isOn())); the owner sets it (setOn) from what the model now
 * holds -- ParamToggle from the host parameter -- so what it shows is always
 * the model's, never a guess that the model agreed.
 */
#pragma once

#include "Pressable.h"

#include <functional>

namespace ni::ui
{

class Toggle : public Pressable
{
public:
    explicit Toggle (const juce::String& label = {});
    ~Toggle() override;

    void setLabel (const juce::String&);
    const juce::String& getLabel() const noexcept { return label; }

    /* What it shows. Sends nothing. */
    void setOn (bool);
    bool isOn() const noexcept { return on; }

    /* Asked for, with the state it should go to. */
    std::function<void (bool)> onChange;

    /* The housing, the gap and the label: its own width. */
    int idealWidth() const;

    /* Where the housing and the square are, in its own coordinates. */
    juce::Rectangle<float> housing() const;
    juce::Rectangle<float> square() const;

    /* Whether the pointer is over the housing, which is what rises. */
    bool isHousingHovered() const;

    void mouseMove (const juce::MouseEvent&) override;
    void paint (juce::Graphics&) override;
    void paintLight (juce::Graphics&) override;

protected:
    void pressed() override;
    std::optional<bool> checkedState() const override { return on; }

private:
    juce::String label;
    bool on = false;

    JUCE_DECLARE_NON_COPYABLE (Toggle)
};

} // namespace ni::ui
