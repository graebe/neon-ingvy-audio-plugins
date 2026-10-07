// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The default control for a continuous parameter: the Knob card of
 * Ultraviolet 1.1.0, and the web kit's Knob.jsx.
 *
 * THE CARD, top to bottom, space-2 apart: the `label` (11px capitals, tracked,
 * ink-muted) centred over the dial; the 48px dial; the Readout under it, as
 * wide as the card. 14 + 8 + 48 + 8 + 28 = 106px, which is exactly what a
 * compact Panel holds. "Never draw a knob without its readout."
 *
 * THE DIAL, from the card's 48px box: a bg-200 disc of radius 16 on a line-100
 * hairline; a 270-degree line-200 rail of radius 20, its gap at the bottom; the
 * uv value arc over it from the minimum to the value, with the arc glow under
 * it (glowArc: uv is a near-white, and the uv-deep halo is where its colour
 * comes from); and a 2px uv pointer from radius 6 to 14. Butt caps throughout:
 * a rounded cap on a 2px stroke reads as a value slightly past where it is,
 * which on a Length knob is another number of steps. Disabled, the arc and
 * the pointer are ink-dim. Keyboard focus is glow-focus round the dial.
 *
 * DETENTS (1.1.0): values a drag holds on for about 14px of travel
 * (Detents.h has the feel and its numbers), each a 1px radial tick from
 * radius 22 to 24 in ink-dim, the one at the current value uv without glow.
 * Shift ignores them; Page Up and Page Down jump between them.
 *
 * INTERACTION, from the card and the Interaction conventions:
 *
 *   drag         vertical, 200px for the range (Shift five times finer),
 *                measured from the press; the gesture opens on the FIRST MOVE
 *                (onBegin), so a click or either half of a double-click is no
 *                edit and leaves no empty touch in a host's automation lane
 *   double-click onReset: the parameter's default, which only the owner knows
 *   arrows        a step, 1 % (Shift: 0.2 %); Page Up/Down the next detent or
 *                10 %; Home/End the ends -- each key one complete edit
 *                (onCommit), as every keystroke is its own gesture
 *   Enter         types into the readout; one click on the readout does too
 *
 * IT KNOWS NOTHING ABOUT PARAMETERS. It turns a number from 0 to 1, shows the
 * text it is given and hands back what is typed; what the number means, how it
 * is printed and how typing is read are the owner's (ParamKnob binds a host
 * parameter). setValue and setValueText send nothing.
 *
 * Info: the card's line covers its label and its dial (setInfo on the knob);
 * the readout carries its own (setReadoutInfo), which says what it accepts.
 */
#pragma once

#include "Focus.h"
#include "Luminous.h"
#include "Readout.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace ni::ui
{

class Knob : public juce::Component,
             public Luminous
{
public:
    /* The card's geometry. */
    static constexpr int dialSize = 48;
    static constexpr int labelHeight = 14;
    static constexpr int gap = 8;
    static constexpr int cardHeight = labelHeight + gap + dialSize + gap + Readout::height;
    static constexpr int minWidth = Readout::minWidth;

    /* A drag's travel for the whole range, and how much finer Shift is. */
    static constexpr double travelPx = 200.0;
    static constexpr double fineRatio = 5.0;

    explicit Knob (const juce::String& label = {});
    ~Knob() override;

    void setLabel (const juce::String&);
    const juce::String& getLabel() const noexcept { return label; }

    /* What it shows, normalised. Sends nothing. */
    void setValue (float normalised);
    float getValue() const noexcept { return value; }

    /* The readout's text: the owner's, never formatted here. */
    void setValueText (const juce::String&);
    const juce::String& getValueText() const noexcept { return readoutBox.getValueText(); }

    /* Normalised values a drag holds on, drawn as ticks; empty for none. */
    void setDetents (std::vector<double> normalised);
    const std::vector<double>& getDetents() const noexcept { return detentsAt; }

    /* The readout's own info line. */
    void setReadoutInfo (const juce::String& line);

    /* ---- what it asks for */
    std::function<void()> onBegin;                       // a drag's first move
    std::function<void (float normalised)> onInput;     // each move of it
    std::function<void()> onEnd;                         // its release
    std::function<void (float normalised)> onCommit;    // a key: one edit
    std::function<void()> onReset;                       // a double-click
    std::function<void (const juce::String&)> onText;   // a typed value

    /* The dial, which takes the pointer and the keyboard, and the readout. */
    juce::Component& dial() noexcept;
    Readout& readout() noexcept { return readoutBox; }

    /* Where the dial sits in the card. */
    juce::Rectangle<int> dialBounds() const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void enablementChanged() override;

    /* ---- Luminous: the dial's focus ring and the readout's, past the card */
    void paintLight (juce::Graphics&) override;

private:
    class Dial;

    juce::String label;
    float value = 0.0f;
    std::vector<double> detentsAt;

    std::unique_ptr<Dial> dialComponent;
    Readout readoutBox;

    JUCE_DECLARE_NON_COPYABLE (Knob)
};

} // namespace ni::ui
