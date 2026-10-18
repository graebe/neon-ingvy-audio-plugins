// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A slider. Slider.h has the card and its interaction.
 */
#include "Slider.h"

#include "ChildLights.h"
#include "Keys.h"
#include "UvLight.h"
#include "UvTokens.h"

#include <cmath>

namespace ni::ui
{

namespace
{
namespace c = uv::tok::colour;

constexpr float hair = uv::tok::stroke::strokeHair;
constexpr float rail2 = uv::tok::stroke::strokeRail;
/* .thumb { 16 x 6, border-radius 2px }; .fill { box-shadow 0 0 6px uv-deep }. */
constexpr float thumbLong = 16.0f;
constexpr float thumbShort = 6.0f;
constexpr uv::tok::ShadowLayer fillGlow { 0.0f, 0.0f, 6.0f, 0.0f, uv::tok::argb::ultraviolet::uvDeep };

/* A screen reader's slider: the value 0..1 to move, the owner's text. */
class SliderValue final : public juce::AccessibilityValueInterface
{
public:
    explicit SliderValue (Slider& s) : slider (s) {}

    bool isReadOnly() const override { return false; }
    double getCurrentValue() const override { return slider.getValue(); }
    juce::String getCurrentValueAsString() const override { return slider.getValueText(); }
    void setValue (double v) override
    {
        if (slider.onCommit)
            slider.onCommit ((float) juce::jlimit (0.0, 1.0, v));
    }
    void setValueAsString (const juce::String& text) override { setValue (text.getDoubleValue()); }
    AccessibleValueRange getRange() const override { return { { 0.0, 1.0 }, (double) keys::step }; }

private:
    Slider& slider;
};
} // namespace

Slider::Slider (Orientation o) : orientation (o)
{
    setWantsKeyboardFocus (true);
    setMouseCursor (o == Orientation::horizontal ? juce::MouseCursor::LeftRightResizeCursor
                                                 : juce::MouseCursor::UpDownResizeCursor);
}

Slider::~Slider() = default;

void Slider::setValue (float v)
{
    v = juce::jlimit (0.0f, 1.0f, v);
    if (juce::exactlyEqual (v, value))
        return;
    value = v;
    repaint();
    if (auto* h = getAccessibilityHandler())
        h->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
}

void Slider::setValueText (const juce::String& text)
{
    valueText = text;
}

juce::Rectangle<float> Slider::track() const
{
    const auto b = getLocalBounds().toFloat();
    return orientation == Orientation::horizontal ? b.withSizeKeepingCentre (b.getWidth(), (float) thickness)
                                                  : b.withSizeKeepingCentre ((float) thickness, b.getHeight());
}

juce::Rectangle<float> Slider::rail() const
{
    const auto t = track();
    const auto in = (float) inset;
    return orientation == Orientation::horizontal
               ? juce::Rectangle<float> (t.getX() + in, t.getCentreY() - rail2 * 0.5f, t.getWidth() - 2.0f * in, rail2)
               : juce::Rectangle<float> (t.getCentreX() - rail2 * 0.5f, t.getY() + in, rail2, t.getHeight() - 2.0f * in);
}

juce::Rectangle<float> Slider::thumb() const
{
    const auto r = rail();
    if (orientation == Orientation::horizontal)
    {
        const float x = r.getX() + value * r.getWidth();
        return { x - thumbShort * 0.5f, r.getCentreY() - thumbLong * 0.5f, thumbShort, thumbLong };
    }
    const float y = r.getBottom() - value * r.getHeight();
    return { r.getCentreX() - thumbLong * 0.5f, y - thumbShort * 0.5f, thumbLong, thumbShort };
}

float Slider::along (juce::Point<float> p) const
{
    return orientation == Orientation::horizontal ? p.x : -p.y;
}

float Slider::valueAt (juce::Point<float> p) const
{
    const auto r = rail();
    const float v = orientation == Orientation::horizontal ? (p.x - r.getX()) / std::max (1.0f, r.getWidth())
                                                           : (r.getBottom() - p.y) / std::max (1.0f, r.getHeight());
    return juce::jlimit (0.0f, 1.0f, v);
}

void Slider::paint (juce::Graphics& g)
{
    const bool live = isEnabled();
    const auto t = track();
    g.setColour (c::bg200);
    g.fillRect (t);
    g.setColour (c::line100);
    g.drawRect (t, hair);

    const auto r = rail();
    g.setColour (c::line200);
    g.fillRect (r);

    const auto fill = orientation == Orientation::horizontal ? r.withWidth (value * r.getWidth())
                                                             : r.withTop (r.getBottom() - value * r.getHeight());
    if (! fill.isEmpty())
    {
        if (live)
            uv::light::shadow (g, fill, fillGlow);
        g.setColour (live ? c::uv : c::inkDim);
        g.fillRect (fill);
    }

    g.setColour (live ? c::ink : c::inkDim);
    g.fillRoundedRectangle (thumb(), uv::tok::radius::radius1);
}

void Slider::paintLight (juce::Graphics& g)
{
    if (focus.isVisible())
        uv::light::glowFocus (g, track());
}

void Slider::input (float v)
{
    if (onInput)
        onInput (juce::jlimit (0.0f, 1.0f, v));
}

void Slider::mouseDown (const juce::MouseEvent& e)
{
    focus.pointerUsed();
    relight (*this);
    if (isShowing())
        grabKeyboardFocus();
    if (! isEnabled())
        return;
    const bool fine = e.mods.isShiftDown();
    drag = Drag { e.position, value, fine, false };
    if (! fine)
    {
        /* A press is where the value goes: jump there. */
        const float to = valueAt (e.position);
        if (! juce::exactlyEqual (to, value))
        {
            drag->begun = true;
            if (onBegin)
                onBegin();
            input (to);
        }
    }
}

void Slider::mouseDrag (const juce::MouseEvent& e)
{
    if (! drag.has_value() || ! isEnabled())
        return;
    if (! drag->begun)
    {
        if (e.position == drag->from)
            return;
        drag->begun = true;
        if (onBegin)
            onBegin();
    }
    if (drag->fine)
    {
        const auto r = rail();
        const float length = orientation == Orientation::horizontal ? r.getWidth() : r.getHeight();
        const float moved = (along (e.position) - along (drag->from)) / std::max (1.0f, length);
        input (drag->startValue + moved / (float) fineRatio);
    }
    else
    {
        input (valueAt (e.position));
    }
}

void Slider::mouseUp (const juce::MouseEvent&)
{
    const bool begun = drag.has_value() && drag->begun;
    drag.reset();
    if (begun && onEnd)
        onEnd();
}

void Slider::mouseDoubleClick (const juce::MouseEvent&)
{
    if (isEnabled() && onReset)
        onReset();
}

bool Slider::keyPressed (const juce::KeyPress& k)
{
    if (! isEnabled())
        return false;
    const auto action = keys::sliderKey (k, orientation == Orientation::horizontal ? keys::Axis::x : keys::Axis::y);
    std::optional<float> to;
    if (action.kind == keys::SliderAction::Kind::delta)
        to = value + action.amount;
    else if (action.kind == keys::SliderAction::Kind::to)
        to = action.amount;
    if (! to.has_value())
        return false;
    focus.keyUsed();
    relight (*this);
    if (onCommit)
        onCommit (juce::jlimit (0.0f, 1.0f, *to));
    return true;
}

void Slider::focusGained (FocusChangeType cause)
{
    focus.focusGained (cause);
    relight (*this);
}

void Slider::focusLost (FocusChangeType)
{
    focus.focusLost();
    relight (*this);
}

std::unique_ptr<juce::AccessibilityHandler> Slider::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (
        *this, juce::AccessibilityRole::slider, juce::AccessibilityActions(),
        juce::AccessibilityHandler::Interfaces { std::make_unique<SliderValue> (*this) });
}

} // namespace ni::ui
