// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Ultraviolet as a JUCE LookAndFeel. UvLookAndFeel.h says what it covers.
 */
#include "UvLookAndFeel.h"

#include "Focus.h"
#include "UvIcons.h"
#include "UvLight.h"

#include <cmath>

namespace uv
{

namespace c = tok::colour;

namespace
{
/* Every stroke here is one of the system's two widths. */
constexpr float hair = tok::stroke::strokeHair;
constexpr float rail = tok::stroke::strokeRail;

/* A hairline border inside `r`, as CSS draws a 1px border: on the box's own
 * pixels, not straddling its edge. */
void outline (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colour)
{
    g.setColour (colour);
    g.drawRect (r, hair);
}
} // namespace

LookAndFeel::LookAndFeel()
{
    /* The colours JUCE reaches for on its own -- a popup's ground, an editor's
     * caret, a list's rows -- so none of them is ever its default blue-grey. */
    setColour (juce::ResizableWindow::backgroundColourId,      c::bg000);
    setColour (juce::DocumentWindow::textColourId,             c::inkMuted);
    setColour (juce::PopupMenu::backgroundColourId,            c::bg100);
    setColour (juce::PopupMenu::textColourId,                  c::ink);
    setColour (juce::PopupMenu::headerTextColourId,            c::inkMuted);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, c::bg300);
    setColour (juce::PopupMenu::highlightedTextColourId,       c::ink);
    setColour (juce::TextEditor::backgroundColourId,           c::bg200);
    setColour (juce::TextEditor::textColourId,                 c::ink);
    setColour (juce::TextEditor::highlightColourId,            c::uv.withAlpha (0.3f));
    setColour (juce::TextEditor::highlightedTextColourId,      c::ink);
    setColour (juce::TextEditor::outlineColourId,              c::line200);
    setColour (juce::TextEditor::focusedOutlineColourId,       c::uv);
    setColour (juce::TextEditor::shadowColourId,               juce::Colour());
    setColour (juce::CaretComponent::caretColourId,            c::uv);
    setColour (juce::Label::textColourId,                      c::ink);
    setColour (juce::Label::backgroundColourId,                juce::Colour());
    setColour (juce::Label::outlineColourId,                   juce::Colour());
    setColour (juce::Label::textWhenEditingColourId,           c::ink);
    setColour (juce::Label::backgroundWhenEditingColourId,     c::bg000);
    setColour (juce::Label::outlineWhenEditingColourId,        c::uv);
    setColour (juce::ComboBox::backgroundColourId,             c::bg200);
    setColour (juce::ComboBox::textColourId,                   c::ink);
    setColour (juce::ComboBox::outlineColourId,                c::line200);
    setColour (juce::ComboBox::arrowColourId,                  c::inkMuted);
    setColour (juce::ComboBox::focusedOutlineColourId,         c::uv);
    setColour (juce::TextButton::buttonColourId,               c::bg200);
    setColour (juce::TextButton::buttonOnColourId,             c::uv);
    setColour (juce::TextButton::textColourOffId,              c::ink);
    setColour (juce::TextButton::textColourOnId,               c::onUv);
    setColour (juce::ToggleButton::textColourId,               c::inkMuted);
    setColour (juce::ListBox::backgroundColourId,              c::bg100);
    setColour (juce::ListBox::outlineColourId,                 c::line100);
    setColour (juce::ListBox::textColourId,                    c::ink);
    setColour (juce::ScrollBar::thumbColourId,                 c::line200);
    setColour (juce::ScrollBar::trackColourId,                 juce::Colour());
    setColour (juce::Slider::textBoxTextColourId,              c::ink);
    setColour (juce::Slider::textBoxBackgroundColourId,        c::bg000);
    setColour (juce::Slider::textBoxOutlineColourId,           c::line200);
    setColour (juce::Slider::rotarySliderFillColourId,         c::uv);
    setColour (juce::Slider::rotarySliderOutlineColourId,      c::line200);
    setColour (juce::Slider::thumbColourId,                    c::uv);
    setColour (juce::TooltipWindow::backgroundColourId,        c::bg100);
    setColour (juce::TooltipWindow::textColourId,              c::ink);
    setColour (juce::TooltipWindow::outlineColourId,           c::line200);
}

/* =============================================================== type == */

juce::Typeface::Ptr LookAndFeel::getTypefaceForFont (const juce::Font& font)
{
    /*
     * ANY FAMILY, ONE FACE. A font JUCE made for itself names the default
     * sans (or serif, or mono) family; one the kit made already carries its
     * face and never asks. Either way the answer is an embedded face, by
     * weight: a "Bold" or "Medium" style is the Medium face, the system's
     * heaviest.
     */
    const auto style = font.getTypefaceStyle().toLowerCase();
    const bool heavy = font.isBold() || style.contains ("bold") || style.contains ("medium")
                    || style.contains ("semibold");
    return heavy ? fonts().medium : fonts().regular;
}

/* =============================================================== knob == */

/*
 * From the Knob card and the web kit's Knob, on a 48px box (knob-lg is the same
 * drawing at 64, so everything is a fraction of 48):
 *
 *   disc     r 16, bg-200
 *   rail     r 20, 270 degrees with the gap at the bottom, line-200, 2px
 *   value    the same radius, from the start (or, bipolar, from 12 o'clock)
 *            to the value, uv, 2px, with the arc glow under it
 *   pointer  r 6 to r 14, 2px, uv
 *
 * BUTT CAPS throughout: a rounded cap on a 2px stroke reads as a value
 * slightly past where it is, which on a Length knob is a different number of
 * steps.
 */
void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                    float startAngle, float endAngle, juce::Slider& s)
{
    const auto box = juce::Rectangle<int> (x, y, w, h).toFloat();

    /* THE KNOB IS ITS SIZE, WHATEVER BOX IT IS GIVEN: "controls have one size
     * each; do not scale controls to fill a window". */
    const float d = juce::jmin (box.getWidth(), box.getHeight(),
                                s.getProperties()["ni.large"] ? tok::size::knobLg : tok::size::knob);
    const auto centre = box.getCentre();
    const float unit = d / tok::size::knob;

    const float rDisc = 16.0f * unit;
    const float rRail = 20.0f * unit;
    const float rPtr0 = 6.0f * unit;
    const float rPtr1 = 14.0f * unit;
    const float stroke = rail * unit;

    const bool live = s.isEnabled();
    const auto lit = live ? c::uv : c::inkDim;

    g.setColour (c::bg200);
    g.fillEllipse (juce::Rectangle<float> (rDisc * 2.0f, rDisc * 2.0f).withCentre (centre));

    const auto arc = [&] (float from, float to)
    {
        juce::Path p;
        p.addCentredArc (centre.x, centre.y, rRail, rRail, 0.0f, from, to, true);
        juce::Path outline;
        juce::PathStrokeType (stroke, juce::PathStrokeType::mitered, juce::PathStrokeType::butt)
            .createStrokedPath (outline, p);
        return outline;
    };

    g.setColour (c::line200);
    g.fillPath (arc (startAngle, endAngle));

    const float angle = startAngle + pos * (endAngle - startAngle);
    const bool bipolar = (bool) s.getProperties()["ni.bipolar"];
    const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;

    if (std::abs (angle - from) > 0.0001f)
    {
        const auto value = arc (juce::jmin (from, angle), juce::jmax (from, angle));
        if (live)
            light::glowArc (g, value);
        g.setColour (lit);
        g.fillPath (value);
    }

    juce::Path ptr;
    ptr.startNewSubPath (centre.x + std::sin (angle) * rPtr0, centre.y - std::cos (angle) * rPtr0);
    ptr.lineTo (centre.x + std::sin (angle) * rPtr1, centre.y - std::cos (angle) * rPtr1);
    g.setColour (lit);
    g.strokePath (ptr, juce::PathStrokeType (stroke, juce::PathStrokeType::mitered,
                                             juce::PathStrokeType::butt));

    if (ni::ui::isFocusVisible (s))
        light::glowFocus (g, juce::Rectangle<float> (rDisc * 2.0f, rDisc * 2.0f).withCentre (centre), rDisc);
}

juce::Label* LookAndFeel::createSliderTextBox (juce::Slider&)
{
    return new ReadoutLabel();
}

/*
 * THE 8px BETWEEN A KNOB AND ITS READOUT. The card stacks label, knob and
 * readout space-2 apart; JUCE's own layout gives the rotary everything above
 * the text box, and the knob centred in that spends the gap as half above and
 * half below. Taking the readout off with the gap attached leaves the knob its
 * own box.
 */
juce::Slider::SliderLayout LookAndFeel::getSliderLayout (juce::Slider& s)
{
    if (s.getTextBoxPosition() != juce::Slider::TextBoxBelow)
        return juce::LookAndFeel_V4::getSliderLayout (s);

    const auto b = s.getLocalBounds();
    const int h = s.getTextBoxHeight();
    const int w = juce::jmax (s.getTextBoxWidth(), b.getWidth());

    juce::Slider::SliderLayout layout;
    layout.textBoxBounds = juce::Rectangle<int> (w, h).withCentre ({ b.getCentreX(), b.getBottom() - h / 2 });
    layout.sliderBounds = b.withTrimmedBottom (h + (int) tok::space::space2);
    return layout;
}

/* ============================================================ readout == */

ReadoutLabel::ReadoutLabel()
{
    setJustificationType (juce::Justification::centred);
    setFont (type::value());
    setColour (juce::TextEditor::backgroundColourId, c::bg000);
    setColour (juce::TextEditor::textColourId, c::ink);
    /* One click types into it: "click the readout to type". */
    setEditable (true, true, false);
}

void ReadoutLabel::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const bool editing = isBeingEdited();
    const bool live = isEnabled();

    if (editing)
        light::glowFocus (g, r);

    g.setColour (c::bg000);
    g.fillRect (r);
    outline (g, r, editing ? c::uv : (live ? c::line200 : c::line100));

    if (! editing)
        /* padding: 0 var(--s2) */
        type::drawValueWithUnit (g, getText(), r.reduced (tok::space::space2, 0.0f), live);
}

/* ============================================================ buttons == */

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int)
{
    return type::button();
}

/*
 * .btn in components.css: a bg-200 well on a line-200 hairline; under the
 * pointer bg-300 with an ink-dim border; pressed or on, uv with on-uv text
 * ("selected is uv"); disabled bg-100 on line-100 with ink-dim text. A
 * button whose property "ni.primary" is true is the window's one primary
 * action: always lit, with glow-led.
 */
void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                        bool highlighted, bool down)
{
    const auto r = b.getLocalBounds().toFloat();
    const bool primary = (bool) b.getProperties()["ni.primary"];
    const bool lit = b.isEnabled() && (down || b.getToggleState() || primary);

    auto fill = c::bg200;
    auto edge = c::line200;
    if (! b.isEnabled())   { fill = c::bg100; edge = c::line100; }
    else if (lit)          { fill = c::uv;    edge = c::uv; }
    else if (highlighted)  { fill = c::bg300; edge = c::inkDim; }

    if (primary && b.isEnabled())
        light::glowLed (g, r);
    if (ni::ui::isFocusVisible (b))
        light::glowFocus (g, r);

    g.setColour (fill);
    g.fillRect (r);
    outline (g, r, edge);
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    const bool lit = b.isEnabled() && (down || b.getToggleState() || (bool) b.getProperties()["ni.primary"]);
    const auto colour = ! b.isEnabled() ? c::inkDim : lit ? c::onUv : c::ink;
    /* padding: 0 var(--s3) */
    type::draw (g, b.getButtonText(), b.getLocalBounds().toFloat().reduced (tok::space::space3, 0.0f),
                type::button(), colour, juce::Justification::centred);
}

/* ============================================================= switch == */

/*
 * The SWITCH form of the Toggle (components.css .switch): a 28 x 14 housing,
 * bg-200 on line-200, with an 8 x 8 square 2px in from its left that moves to
 * 16px and lights -- uv with glow-led, and the housing's border uv. The label
 * follows space-2 to the right, in the label style. The LED form is for state
 * the plugin reports; a user-settable boolean is a switch.
 *
 * Disabled is the system's own state (and the Listen-In list's refused row):
 * no fill, a line-100 border, the square and the label ink-dim.
 */
void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    const bool on = b.getToggleState();
    const bool live = b.isEnabled();
    const auto bounds = b.getLocalBounds().toFloat();

    const juce::Rectangle<float> housing (bounds.getX(), bounds.getCentreY() - 7.0f, 28.0f, 14.0f);
    const juce::Rectangle<float> square (housing.getX() + (on ? 16.0f : 2.0f) + hair,
                                         housing.getY() + 2.0f + hair, 8.0f, 8.0f);

    if (ni::ui::isFocusVisible (b))
        light::glowFocus (g, housing);

    if (live)
    {
        g.setColour (highlighted ? c::bg300 : c::bg200);
        g.fillRect (housing);
    }
    outline (g, housing, ! live ? c::line100 : (on ? c::uv : c::line200));

    if (on && live)
        light::glowLed (g, square);
    g.setColour (! live ? c::line100 : (on ? c::uv : c::inkDim));
    g.fillRect (square);

    const auto& style = tok::type::label;
    type::draw (g, type::cased (style, b.getButtonText()),
                bounds.withTrimmedLeft (housing.getWidth() + tok::space::space2),
                type::font (style), live ? c::inkMuted : c::inkDim);
}

/* ============================================================= select == */

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&) { return type::value(); }
juce::Font LookAndFeel::getPopupMenuFont()                { return type::value(); }

/*
 * .select: a bg-200 field on a line-200 hairline, bg-300 under the pointer,
 * the border uv while its list is open; the value in ink, padded 12px on the
 * left and 28px on the right for the caret, which is the system's chevron in
 * ink-muted, 6px in from the right edge.
 */
void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int,
                                juce::ComboBox& box)
{
    const auto r = juce::Rectangle<int> (0, 0, w, h).toFloat();
    const bool live = box.isEnabled();

    if (ni::ui::isFocusVisible (box))
        light::glowFocus (g, r);

    g.setColour (! live ? c::bg100 : (box.isMouseOver (true) ? c::bg300 : c::bg200));
    g.fillRect (r);
    outline (g, r, ! live ? c::line100 : (box.isPopupActive() ? c::uv : c::line200));

    /* .select .icon { right: 6px }, from inside the border. */
    drawIcon (g, "chevron", live ? c::inkMuted : c::inkDim,
              { r.getRight() - hair - 6.0f - 16.0f, r.getCentreY() - 8.0f, 16.0f, 16.0f });
}

void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    /* padding: 0 28px 0 12px, inside a 1px border -- and none of the
     * Label's own, which would push the text 5px further in. */
    const int left = (int) tok::size::hairline + (int) tok::space::space3;
    const int right = (int) tok::size::hairline + (int) tok::size::controlH;
    label.setBorderSize ({});
    label.setBounds (left, 0, juce::jmax (0, box.getWidth() - left - right), box.getHeight());
    label.setFont (type::value());
    label.setJustificationType (juce::Justification::centredLeft);
    label.setColour (juce::Label::textColourId, box.isEnabled() ? c::ink : c::inkDim);
}

/*
 * The open list (Select README): a bg-100 box with a uv border, one row per
 * option, 28px tall and padded 12px, the current option in uv -- "there is no
 * separate selected colour: selected is uv" -- and the row under the pointer
 * bg-300. It opens INSIDE the window ("every list and menu opens inside it"):
 * that is the Select's to ask for, with PopupMenu::Options::withParentComponent.
 */
void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    const auto r = juce::Rectangle<int> (0, 0, w, h).toFloat();
    g.fillAll (c::bg100);
    outline (g, r, c::uv);
}

int LookAndFeel::getPopupMenuBorderSize()
{
    return (int) tok::size::hairline;
}

void LookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int,
                                             int& idealWidth, int& idealHeight)
{
    if (isSeparator)
    {
        idealWidth = 1;
        idealHeight = (int) tok::space::space2;
        return;
    }
    idealHeight = (int) tok::size::controlH;
    idealWidth = (int) std::ceil (type::width (type::value(), text)) + 2 * (int) tok::space::space3;
}

void LookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                                     bool isSeparator, bool isActive, bool isHighlighted,
                                     bool isTicked, bool, const juce::String& text,
                                     const juce::String&, const juce::Drawable*, const juce::Colour*)
{
    const auto r = area.toFloat();

    if (isSeparator)
    {
        g.setColour (c::line100);
        g.fillRect (r.reduced (tok::space::space3, 0.0f).withSizeKeepingCentre (r.getWidth() - 2.0f * tok::space::space3, hair));
        return;
    }

    if (isHighlighted && isActive)
    {
        g.setColour (c::bg300);
        g.fillRect (r);
    }

    type::draw (g, text, r.reduced (tok::space::space3, 0.0f), type::value(),
                ! isActive ? c::inkDim : isTicked ? c::uv : c::ink);
}

/* =============================================================== text == */

juce::Font LookAndFeel::getLabelFont (juce::Label& l)
{
    /* A Label keeps the font it was given. One given none has JUCE's default,
     * whose face getTypefaceForFont answers with an embedded one. */
    return l.getFont();
}

void LookAndFeel::drawLabel (juce::Graphics& g, juce::Label& l)
{
    if (l.isBeingEdited())
        return;   // its editor draws itself

    const auto background = l.findColour (juce::Label::backgroundColourId);
    if (! background.isTransparent())
        g.fillAll (background);

    /* Disabled, text drops to ink-dim, as everything disabled does. */
    type::draw (g, l.getText(), l.getBorderSize().subtractedFrom (l.getLocalBounds()).toFloat(),
                getLabelFont (l),
                l.isEnabled() ? l.findColour (juce::Label::textColourId) : c::inkDim,
                l.getJustificationType());
}

/*
 * A text field (TextField README, .ph-field): a bg-200 well on line-200,
 * ringed in uv with glow-focus while it is typed into; disabled bg-100 on
 * line-100. The editor inside a ReadoutLabel takes the readout's bg-000.
 */
void LookAndFeel::fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor& e)
{
    const auto r = juce::Rectangle<int> (0, 0, w, h).toFloat();
    if (e.hasKeyboardFocus (true) && ! e.isReadOnly())
        light::glowFocus (g, r);
    g.setColour (e.isEnabled() ? e.findColour (juce::TextEditor::backgroundColourId) : c::bg100);
    g.fillRect (r);
}

void LookAndFeel::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& e)
{
    const auto r = juce::Rectangle<int> (0, 0, w, h).toFloat();
    const auto colour = ! e.isEnabled()                                 ? c::line100
                      : (e.hasKeyboardFocus (true) && ! e.isReadOnly()) ? c::uv
                                                                        : c::line200;
    outline (g, r, colour);
}

/* ======================================================= the rest == */

/* A list that overflows (the CheckList's) scrolls with a quiet thumb: a
 * line-200 bar, no track, nothing that reads as a control of its own. */
void LookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h,
                                 bool vertical, int thumbStart, int thumbSize, bool mouseOver, bool mouseDown)
{
    if (thumbSize <= 0)
        return;

    auto thumb = vertical ? juce::Rectangle<int> (x, thumbStart, w, thumbSize)
                          : juce::Rectangle<int> (thumbStart, y, thumbSize, h);
    thumb = vertical ? thumb.withSizeKeepingCentre (4, thumb.getHeight() - 4)
                     : thumb.withSizeKeepingCentre (thumb.getWidth() - 4, 4);

    g.setColour (mouseOver || mouseDown ? c::inkDim : c::line200);
    g.fillRect (thumb);
}

int LookAndFeel::getDefaultScrollbarWidth()
{
    return (int) tok::space::space2;
}

void LookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool mouseOver, bool dragging)
{
    g.setColour (mouseOver || dragging ? c::inkDim : c::line200);
    for (int i = 1; i <= 3; ++i)
    {
        const float d = (float) i * 4.0f;
        g.drawLine ((float) w - d, (float) h, (float) w, (float) h - d, hair);
    }
}

/* ============================================================== shared == */

/*
 * The typeface cache is cleared both ways: JUCE remembers which face a font
 * name resolved to, and a name resolved before this was the default would
 * otherwise keep the face it found then.
 */
SharedLookAndFeel::SharedLookAndFeel()
    : previous (&juce::LookAndFeel::getDefaultLookAndFeel())
{
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
    juce::Typeface::clearTypefaceCache();
}

SharedLookAndFeel::~SharedLookAndFeel()
{
    juce::LookAndFeel::setDefaultLookAndFeel (previous.get());
    juce::Typeface::clearTypefaceCache();
}

} // namespace uv
