// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Ultraviolet as a JUCE LookAndFeel: the stock widgets drawn as the system's
 * controls.
 *
 * The kit's own components (Knob, Toggle, Select, Button ...) draw
 * themselves; this is for every widget JUCE makes or the kit builds on -- a
 * Label and its TextEditor when a readout is typed into, a PopupMenu, a
 * ScrollBar, a Slider, TextButton, ToggleButton or ComboBox anyone uses as
 * they are -- so that nothing in a window falls back to JUCE's blue-grey. Set
 * it on an editor's root and every child inherits it.
 *
 * PORTED FROM THE JUCE EDITOR (commit 7711ba2, UvLookAndFeel), brought up to
 * 1.1.0 and to what the web kit drew when it was replaced (its
 * components.css and tokens.css): on-uv is violet now, a hovered well's border rises to ink-dim,
 * a disabled button drops to bg-100, the switch's lit square glows, the select
 * carries the design's chevron glyph, focus is glow-focus and only for the
 * keyboard (Focus.h), and the fonts are the CSS sizes (UvType.h).
 *
 * EVERY FONT IS AN EMBEDDED ONE. getTypefaceForFont answers for any font JUCE
 * makes by family name -- its default sans, serif or mono, or "JetBrains
 * Mono" itself -- with the embedded face of its weight. JUCE asks the DEFAULT
 * LookAndFeel that question, which is why SharedLookAndFeel below makes this
 * one the default while an editor is open.
 */
#pragma once

#include "UvTokens.h"
#include "UvType.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace uv
{

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    /* ---- type */
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

    /* ---- knob: the Knob card, on a rotary juce::Slider. A slider whose
     * property "ni.bipolar" is true draws its arc from 12 o'clock, as a
     * bipolar knob's is (Knob README). */
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    juce::Slider::SliderLayout getSliderLayout (juce::Slider&) override;

    /* ---- button */
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    /* ---- switch: the Toggle's switch form */
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    /* ---- select and its open list */
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int buttonX, int buttonY,
                       int buttonW, int buttonH, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator,
                            bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                            const juce::String& text, const juce::String& shortcut,
                            const juce::Drawable* icon, const juce::Colour* textColour) override;
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardHeight,
                                    int& idealWidth, int& idealHeight) override;
    int getPopupMenuBorderSize() override;

    /* ---- text */
    void drawLabel (juce::Graphics&, juce::Label&) override;
    juce::Font getLabelFont (juce::Label&) override;
    void fillTextEditorBackground (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int w, int h, juce::TextEditor&) override;

    /* ---- the rest of a window */
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h,
                        bool vertical, int thumbStart, int thumbSize,
                        bool mouseOver, bool mouseDown) override;
    int getDefaultScrollbarWidth() override;
    void drawCornerResizer (juce::Graphics&, int w, int h, bool mouseOver, bool dragging) override;
};

/*
 * The text box under a knob: the Readout. A bg-000 well on a line-200
 * hairline, the value in ink and its unit in ink-muted (type::drawValueWithUnit);
 * typing into it rings it in uv with glow-focus. A Label, so JUCE's
 * click-to-type, its editor, its caret and its commit all still work; only the
 * resting paint is the system's.
 */
class ReadoutLabel : public juce::Label
{
public:
    ReadoutLabel();
    void paint (juce::Graphics&) override;
};

/*
 * The one LookAndFeel an editor uses, shared by every editor open in the
 * process, and the default LookAndFeel for as long as any of them is open:
 *
 *   juce::SharedResourcePointer<uv::SharedLookAndFeel> look;   // first member
 *   ...
 *   setLookAndFeel (&look->lookAndFeel);                     // in the constructor
 *   setLookAndFeel (nullptr);                                // in the destructor
 *
 * Made with the first editor, gone with the last (SharedResourcePointer), and
 * the default in between -- so a font JUCE makes for itself inside any of
 * them is an embedded face too. The default is put back on the way out.
 */
struct SharedLookAndFeel
{
    SharedLookAndFeel();
    ~SharedLookAndFeel();

    LookAndFeel lookAndFeel;

private:
    juce::WeakReference<juce::LookAndFeel> previous;
};

} // namespace uv
