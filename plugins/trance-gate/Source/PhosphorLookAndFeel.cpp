#include "PhosphorLookAndFeel.h"
#include "BinaryData.h"

using namespace phosphor;

/* ============================================================== fonts == */

namespace phosphor::font
{
Faces::Faces()
{
    regular = juce::Typeface::createSystemTypefaceFor (
        BinaryData::JetBrainsMonoRegular_ttf, BinaryData::JetBrainsMonoRegular_ttfSize);
    medium = juce::Typeface::createSystemTypefaceFor (
        BinaryData::JetBrainsMonoMedium_ttf, BinaryData::JetBrainsMonoMedium_ttfSize);
    faces = this;
}

Faces::~Faces() { faces = nullptr; }
}

PhosphorLookAndFeel::PhosphorLookAndFeel()
{
    /* The colours JUCE reaches for on its own -- a popup's ground, a text
     * editor's caret -- so nothing falls back to the default blue-grey. */
    setColour (juce::PopupMenu::backgroundColourId,          colour::bg100);
    setColour (juce::PopupMenu::textColourId,                colour::ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colour::bg300);
    setColour (juce::PopupMenu::highlightedTextColourId,     colour::ink);
    setColour (juce::TextEditor::backgroundColourId,         colour::bg000);
    setColour (juce::TextEditor::textColourId,               colour::ink);
    setColour (juce::TextEditor::highlightColourId,          colour::phosphor.withAlpha (0.3f));
    setColour (juce::TextEditor::highlightedTextColourId,    colour::ink);
    setColour (juce::TextEditor::outlineColourId,            colour::line200);
    setColour (juce::TextEditor::focusedOutlineColourId,     colour::phosphor);
    setColour (juce::CaretComponent::caretColourId,          colour::phosphor);
    setColour (juce::Label::textColourId,                    colour::ink);
    setColour (juce::ComboBox::textColourId,                 colour::ink);
}

/* =============================================================== knob == */

/*
 * From Knob/preview.html, a 48px box:
 *
 *   <circle class="disc" cx="24" cy="24" r="16"/>            bg-200
 *   <path class="rail" d="M9.86 38.14 A20 20 0 1 1 38.14 38.14"/>   line-200
 *   <path class="arc"  ... same radius, from the rail's start/>     phosphor
 *   <line class="ptr" .../>                        r 6 -> r 14, 2px
 *
 * So: disc radius 1/3 of the box, rail radius 5/12, pointer from 1/8 to 7/24.
 * Expressed as fractions because knob-lg is the same drawing at 64px, and the
 * preview's second instance confirms it (disc 24, rail 28, pointer 8 -> 18.67
 * -- exactly 4/3 of the 48px numbers).
 *
 * The rail runs 270 degrees: JUCE hands us startAngle/endAngle already set to
 * that by the caller, and we use them rather than re-deriving, so a bipolar
 * knob added later only has to change where the arc STARTS.
 */
void PhosphorLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                            float pos, float startAngle, float endAngle,
                                            juce::Slider& s)
{
    const auto box = juce::Rectangle<int> (x, y, w, h).toFloat();
    /*
     * THE KNOB IS 48px, WHATEVER BOX IT IS GIVEN.
     *
     * "Controls have one size each. Do not scale controls to fill a window;
     * leave space instead." JUCE hands a rotary the whole area above its text
     * box, so a knob in a 74px column would otherwise be drawn 74 across and
     * the system's one rule about size would be broken by the layout rather
     * than by anyone deciding to break it. Clamping here makes the rule hold
     * no matter what a future layout does.
     */
    const float d  = juce::jmin (juce::jmin (box.getWidth(), box.getHeight()),
                                 (float) size::knob);
    const auto c   = box.getCentre();

    const float rDisc = d * (16.0f / 48.0f);
    const float rRail = d * (20.0f / 48.0f);
    const float rPtr0 = d * ( 6.0f / 48.0f);
    const float rPtr1 = d * (14.0f / 48.0f);
    const float thick = size::rail * (d / (float) size::knob);

    const bool on = s.isEnabled();
    const auto live = on ? colour::phosphor : colour::inkDim;

    /* The well. */
    g.setColour (colour::bg200);
    g.fillEllipse (juce::Rectangle<float> (rDisc * 2.0f, rDisc * 2.0f).withCentre (c));

    auto arc = [&] (float from, float to, juce::Colour col)
    {
        juce::Path p;
        p.addCentredArc (c.x, c.y, rRail, rRail, 0.0f, from, to, true);
        g.setColour (col);
        /* Butt caps: the system's arcs are `stroke-linecap: butt`, and a
         * rounded cap on a 2px stroke reads as a value slightly past where it
         * is -- which on a Length knob is a different number of steps. */
        g.strokePath (p, juce::PathStrokeType (thick, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::butt));
    };

    const float angle = startAngle + pos * (endAngle - startAngle);

    arc (startAngle, endAngle, colour::line200);            /* the rail */
    if (pos > 0.0001f) arc (startAngle, angle, live);       /* the value */

    /* The pointer, inside the disc. */
    juce::Path ptr;
    ptr.startNewSubPath (c.x + std::sin (angle) * rPtr0, c.y - std::cos (angle) * rPtr0);
    ptr.lineTo          (c.x + std::sin (angle) * rPtr1, c.y - std::cos (angle) * rPtr1);
    g.setColour (live);
    g.strokePath (ptr, juce::PathStrokeType (thick));

    if (s.hasKeyboardFocus (false))
        glowFocus (g, juce::Rectangle<float> (rRail * 2.0f, rRail * 2.0f).withCentre (c),
                   rRail);
}

juce::Label* PhosphorLookAndFeel::createSliderTextBox (juce::Slider&)
{
    return new PhosphorReadout();
}

/*
 * THE 8px BETWEEN A KNOB AND ITS READOUT, WHICH WAS 4.
 *
 * The Knob card stacks label, knob, readout with space-2 between them, and
 * resized() sizes the column for exactly that: 48 + 8 + 28 = 84. But
 * LookAndFeel_V2::getSliderLayout takes the readout off the bottom and gives
 * the WHOLE remainder to the rotary -- 74x56 -- and drawRotarySlider then
 * clamps the disc to 48 and centres it in the 56, spending the 8 as 4 above
 * and 4 below. The gap the card asks for never appeared anywhere.
 *
 * So take the readout off WITH the gap attached, and what is left is the
 * knob's own box.
 */
juce::Slider::SliderLayout PhosphorLookAndFeel::getSliderLayout (juce::Slider& s)
{
    if (s.getTextBoxPosition() != juce::Slider::TextBoxBelow)
        return juce::LookAndFeel_V2::getSliderLayout (s);

    const auto b = s.getLocalBounds();
    const int  h = s.getTextBoxHeight();

    /* The box spans the slider, floored at the card's minimum: a column too
     * narrow for a readout should overflow where it can be seen rather than
     * clip in silence. */
    const int w = juce::jmax (s.getTextBoxWidth(), b.getWidth());

    juce::Slider::SliderLayout layout;
    layout.textBoxBounds = juce::Rectangle<int> (w, h)
                             .withCentre ({ b.getCentreX(), b.getBottom() - h / 2 });
    layout.sliderBounds  = b.withTrimmedBottom (h + space::s2);
    return layout;
}

/* ======================================================= glyph button == */

PhosphorGlyphButton::PhosphorGlyphButton (const juce::String& name, Glyph gl)
    : juce::Button (name), glyph (gl)
{
    setTooltip (name);
}

void PhosphorGlyphButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);

    auto fill   = colour::bg200;
    auto border = colour::line200;
    auto mark   = colour::ink;
    if (! isEnabled())    { fill = colour::bg100;    border = colour::line100; mark = colour::inkDim; }
    else if (down)        { fill = colour::phosphor; border = colour::phosphor; mark = colour::onPhosphor; }
    else if (highlighted) { fill = colour::bg300;    border = colour::inkDim; }

    g.setColour (fill);
    g.fillRect (r);
    g.setColour (border);
    g.drawRect (r, stroke::hair);

    /* A 12px glyph centred in the button: two offset rectangles for copy, and
     * a sheet under a clipboard's tab for paste. Hairlines, like the caret. */
    const auto c = r.getCentre();
    g.setColour (mark);
    if (glyph == Glyph::copy)
    {
        g.drawRect (juce::Rectangle<float> (c.x - 5.5f, c.y - 5.5f, 8.0f, 8.0f), stroke::hair);
        g.drawRect (juce::Rectangle<float> (c.x - 2.5f, c.y - 2.5f, 8.0f, 8.0f), stroke::hair);
    }
    else
    {
        const juce::Rectangle<float> sheet (c.x - 5.0f, c.y - 4.0f, 10.0f, 9.0f);
        g.drawRect (sheet, stroke::hair);
        /* The tab, which is what makes it a clipboard rather than a box. */
        g.drawRect (juce::Rectangle<float> (c.x - 2.0f, c.y - 6.5f, 4.0f, 3.0f), stroke::hair);
    }

    if (hasKeyboardFocus (false)) glowFocus (g, r);
}

/* ============================================================ readout == */

PhosphorReadout::PhosphorReadout()
{
    setJustificationType (juce::Justification::centred);
    setColour (juce::Label::textColourId,            colour::ink);
    setColour (juce::Label::backgroundColourId,      juce::Colours::transparentBlack);
    setColour (juce::Label::outlineColourId,         juce::Colours::transparentBlack);
    setColour (juce::TextEditor::backgroundColourId, colour::bg000);
    setColour (juce::TextEditor::textColourId,       colour::ink);
    setFont (font::value());
    setEditable (false, true, false);   /* double-click to type, as the card says */
}

void PhosphorReadout::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool editing = isBeingEdited();

    g.setColour (colour::bg000);
    g.fillRect (r);
    g.setColour (editing ? colour::phosphor : colour::line200);
    g.drawRect (r, stroke::hair);

    if (editing)
    {
        glowFocus (g, r);
        Label::paint (g);       /* the editor draws itself */
        return;
    }

    /*
     * NUMBER IN ink, UNIT IN ink-muted -- split at the LAST space.
     *
     * "40.0 ms" and "90 %" split cleanly; "1/16" and "1.00" have no space and
     * are all number, which is what the system wants for a unitless value.
     * Splitting at the last space rather than the first keeps a hypothetical
     * "1 / 16 T" whole on the number side rather than calling "16 T" a unit.
     */
    const auto text = getText();
    const int  cut  = text.lastIndexOfChar (' ');
    const auto num  = cut > 0 ? text.substring (0, cut) : text;
    const auto unit = cut > 0 ? text.substring (cut + 1) : juce::String();

    const auto f = font::value();
    g.setFont (f);
    const float wNum  = juce::GlyphArrangement::getStringWidth (f, num);
    const float wUnit = unit.isEmpty() ? 0.0f
                      : juce::GlyphArrangement::getStringWidth (f, unit) + 2.0f;  /* .unit margin-left: 2px */
    float x = r.getCentreX() - (wNum + wUnit) * 0.5f;
    const float baseline = r.getCentreY() + f.getHeight() * 0.35f;

    g.setColour (isEnabled() ? colour::ink : colour::inkDim);
    g.drawSingleLineText (num, juce::roundToInt (x), juce::roundToInt (baseline));

    if (unit.isNotEmpty())
    {
        x += wNum + 2.0f;
        g.setColour (isEnabled() ? colour::inkMuted : colour::inkDim);
        g.drawSingleLineText (unit, juce::roundToInt (x), juce::roundToInt (baseline));
    }
}

/* ============================================================ buttons == */

juce::Font PhosphorLookAndFeel::getTextButtonFont (juce::TextButton&, int) { return font::button(); }

void PhosphorLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b,
                                                const juce::Colour&, bool highlighted, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);

    auto fill   = colour::bg200;
    auto border = colour::line200;
    if (! b.isEnabled())   { fill = colour::bg100;     border = colour::line100; }
    else if (down)         { fill = colour::phosphor;  border = colour::phosphor; }
    else if (highlighted)  { fill = colour::bg300;     border = colour::inkDim; }

    g.setColour (fill);
    g.fillRect (r);
    g.setColour (border);
    g.drawRect (r, stroke::hair);

    if (b.hasKeyboardFocus (false)) glowFocus (g, r);
}

void PhosphorLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    const auto c = ! b.isEnabled() ? colour::inkDim
                 : down            ? colour::onPhosphor
                                   : colour::ink;
    /* .04em of tracking, which at 12px is half a pixel per glyph -- small,
     * but it is the difference between the button text and the value text
     * being the same thing, and they are not. */
    const auto f = font::button();
    const float w = trackedWidth (b.getButtonText(), f, font::trackTitle * 0.5f);
    const auto r = b.getLocalBounds().toFloat();
    drawTracked (g, b.getButtonText(), f, c,
                 { r.getCentreX() - w * 0.5f, r.getCentreY() + f.getHeight() * 0.35f },
                 font::trackTitle * 0.5f);
}

/* ============================================================= toggle == */

/*
 * The SWITCH form: a 28x14 bg-200 housing with an 8x8 square at 2px that
 * moves to 16px and lights. The LED form (an 8px round lens) is for state the
 * plugin REPORTS; a user-settable boolean like Legato is a switch. The card
 * is explicit about which is which, and getting it backwards would say the
 * plugin is telling you something when in fact it is asking.
 */
void PhosphorLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b,
                                            bool highlighted, bool)
{
    const bool on = b.getToggleState();
    const auto bounds = b.getLocalBounds().toFloat();

    const juce::Rectangle<float> sw (bounds.getX() + 0.5f,
                                     bounds.getCentreY() - 7.0f, 28.0f, 14.0f);

    g.setColour (highlighted ? colour::bg300 : colour::bg200);
    g.fillRect (sw);
    g.setColour (on ? colour::phosphor : colour::line200);
    g.drawRect (sw, stroke::hair);

    const juce::Rectangle<float> knobSq (sw.getX() + (on ? 16.0f : 2.0f),
                                         sw.getY() + 2.0f, 8.0f, 8.0f);
    if (on) glowLed (g, knobSq);
    g.setColour (on ? colour::phosphor : colour::inkDim);
    g.fillRect (knobSq);

    /* Label to the right, space-2 away, in the label style. */
    const auto f = font::label();
    drawTracked (g, b.getButtonText().toUpperCase(), f, colour::inkMuted,
                 { sw.getRight() + (float) space::s2, bounds.getCentreY() + f.getHeight() * 0.35f },
                 font::trackLabel);

    if (b.hasKeyboardFocus (false)) glowFocus (g, sw);
}

/* ============================================================= select == */

juce::Font PhosphorLookAndFeel::getComboBoxFont (juce::ComboBox&) { return font::value(); }
juce::Font PhosphorLookAndFeel::getPopupMenuFont()                { return font::value(); }

void PhosphorLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool,
                                        int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<int> (0, 0, w, h).toFloat().reduced (0.5f);
    const bool open = box.isPopupActive();

    g.setColour (box.isMouseOver() ? colour::bg300 : colour::bg200);
    g.fillRect (r);
    g.setColour (open ? colour::phosphor : colour::line200);
    g.drawRect (r, stroke::hair);

    /* The caret: a 6x6 square rotated 45 degrees with only its right and
     * bottom borders -- which is a chevron pointing down, drawn as two 1px
     * strokes. The system uses no icon set; this and the tie bar are the only
     * glyphs in it. */
    const float cx = r.getRight() - 10.0f - 3.0f;
    const float cy = r.getCentreY() - 2.0f;
    juce::Path chevron;
    chevron.startNewSubPath (cx - 3.0f, cy);
    chevron.lineTo (cx, cy + 3.0f);
    chevron.lineTo (cx + 3.0f, cy);
    g.setColour (colour::inkMuted);
    g.strokePath (chevron, juce::PathStrokeType (stroke::hair));

    if (box.hasKeyboardFocus (false)) glowFocus (g, r);
}

void PhosphorLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& l)
{
    /* padding: 0 28px 0 12px */
    l.setBounds (space::s3, 0, box.getWidth() - 28 - space::s3, box.getHeight());
    l.setFont (font::value());
    l.setColour (juce::Label::textColourId, colour::ink);
}

void PhosphorLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (colour::bg100);
    g.setColour (colour::phosphor);
    g.drawRect (0, 0, w, h, (int) stroke::hair);
}

void PhosphorLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                                             bool isSeparator, bool isActive, bool isHighlighted,
                                             bool isTicked, bool, const juce::String& text,
                                             const juce::String&, const juce::Drawable*,
                                             const juce::Colour*)
{
    if (isSeparator)
    {
        g.setColour (colour::line100);
        g.fillRect (area.reduced (space::s3, 0).withHeight (1));
        return;
    }

    if (isHighlighted)
    {
        g.setColour (colour::bg300);
        g.fillRect (area);
    }

    /* "There is no separate selected colour: selected is phosphor." */
    g.setColour (! isActive ? colour::inkDim : isTicked ? colour::phosphor : colour::ink);
    g.setFont (font::value());
    g.drawText (text, area.reduced (space::s3, 0), juce::Justification::centredLeft, true);
}

/* ============================================================== label == */

void PhosphorLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& l)
{
    if (l.isBeingEdited()) return;      /* the editor draws itself */

    g.setColour (l.findColour (juce::Label::textColourId));
    g.setFont (l.getFont());
    g.drawFittedText (l.getText(), l.getLocalBounds(), l.getJustificationType(), 1);
}
