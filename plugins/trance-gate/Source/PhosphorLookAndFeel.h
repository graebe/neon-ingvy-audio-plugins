#pragma once
#include "Phosphor.h"

/*
 * The design system as a JUCE LookAndFeel.
 *
 * Everything here is transcribed from the system's component previews rather
 * than approximated: the knob's rail is 270 degrees on radius 20 of a 48px
 * box because its preview's SVG says
 *
 *     <path class="rail" d="M9.86 38.14 A20.0 20.0 0 1 1 38.14 38.14"/>
 *
 * and (9.86, 38.14) about a centre of (24, 24) is 135 degrees. Copying the
 * numbers out is the difference between a plugin that matches the system and
 * one that looks like it was drawn from a description of the system.
 */
class PhosphorLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PhosphorLookAndFeel();

    /* ---- knobs ---- */
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                           float pos, float startAngle, float endAngle,
                           juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    juce::Slider::SliderLayout getSliderLayout (juce::Slider&) override;

    /* ---- buttons ---- */
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    /* ---- the switch form of Toggle ---- */
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool highlighted, bool down) override;

    /* ---- select ---- */
    void drawComboBox (juce::Graphics&, int w, int h, bool down,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>&,
                            bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu,
                            const juce::String& text, const juce::String& shortcut,
                            const juce::Drawable* icon,
                            const juce::Colour* textColour) override;

    void drawLabel (juce::Graphics&, juce::Label&) override;

private:
    /* Keeps the bundled faces alive exactly as long as an editor is open. */
    juce::SharedResourcePointer<phosphor::font::Faces> faces;
};

/*
 * THE READOUT UNDER A KNOB, in two tones.
 *
 * The system prints the unit in ink-muted after the number -- "32 ms", "54 %"
 * -- which a plain JUCE text box cannot do: a Label draws one colour. This is
 * a Label subclass so that JUCE's click-to-type still works (the editor, the
 * caret and the commit are all inherited); only the resting paint is ours,
 * splitting the string at its last space.
 *
 * Returned from createSliderTextBox, so every knob gets one without asking.
 */
/*
 * A COPY/PASTE BUTTON DRAWN AS A GLYPH.
 *
 * A DEPARTURE FROM THE SYSTEM, and a named one: "Iconography: None... Do not
 * add icons for play, copy or settings; write the word." The words are what
 * pushed the settings onto a second row, and a second row is 36px of a height
 * budget a 128-step window has nearly spent -- so these two became glyphs on
 * request, and only these two.
 *
 * Drawn in the system's idiom regardless: 1px hairline strokes in `ink`, the
 * same weight as the select caret, which is the only other glyph it allows.
 */
class PhosphorGlyphButton : public juce::Button
{
public:
    enum class Glyph { copy, paste };
    PhosphorGlyphButton (const juce::String& name, Glyph);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    Glyph glyph;
};

class PhosphorReadout : public juce::Label
{
public:
    PhosphorReadout();
    void paint (juce::Graphics&) override;
};
