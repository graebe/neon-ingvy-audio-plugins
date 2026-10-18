// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A handle on a plot. PlotHandle.h has its rules.
 */
#include "PlotHandle.h"

#include "Keys.h"
#include "UvLight.h"
#include "UvTokens.h"

#include <cmath>

namespace ni::ui
{

namespace c = uv::tok::colour;

PlotHandle::PlotHandle (juce::Component& w, ParamBinding& xb, Travel ax, ParamBinding* yb, Travel ay)
    : well (w), x (xb), y (yb), alongX (std::move (ax)), alongY (std::move (ay))
{
    jassert (alongX != nullptr && (y == nullptr || alongY != nullptr));
    setWantsKeyboardFocus (true);
    setMouseCursor (y != nullptr ? juce::MouseCursor::UpDownLeftRightResizeCursor
                                 : juce::MouseCursor::LeftRightResizeCursor);
    setSize (size, size);
}

juce::String PlotHandle::valueText() const
{
    auto text = x.text();
    if (y != nullptr)
        text << ", " << y->text();
    return text;
}

void PlotHandle::typeValue (const juce::String& text)
{
    x.setText (text);
}

bool PlotHandle::hitTest (int px, int py)
{
    return getLocalBounds().toFloat().getCentre().getDistanceFrom ({ (float) px, (float) py }) <= targetR;
}

void PlotHandle::paint (juce::Graphics& g)
{
    const auto dot = juce::Rectangle<float> (2.0f * dotR, 2.0f * dotR).withCentre (getLocalBounds().toFloat().getCentre());
    if (focus.isVisible())
        uv::light::glowFocus (g, dot, dotR);

    juce::Path disc;
    disc.addEllipse (dot);
    if (isLit())
        uv::light::glowArc (g, disc);
    g.setColour (isLit() ? c::uv : c::ink);
    g.fillPath (disc);
    g.setColour (c::bg000);
    g.strokePath (disc, juce::PathStrokeType (1.5f));
}

void PlotHandle::setAltY (ParamBinding* altY, Travel alongAltY)
{
    jassert (altY == nullptr || alongAltY != nullptr);
    yAlt = altY;
    alongYAlt = std::move (alongAltY);
}

void PlotHandle::mouseEnter (const juce::MouseEvent&)
{
    hovered = true;
    repaint();
}

void PlotHandle::mouseExit (const juce::MouseEvent&)
{
    hovered = false;
    repaint();
}

void PlotHandle::mouseDown (const juce::MouseEvent& e)
{
    focus.pointerUsed();
    if (isShowing())
        grabKeyboardFocus();

    Drag d;
    d.from = e.getEventRelativeTo (&well).position;
    d.plainX = x.parameter().convertFrom0to1 (x.value());
    const bool alt = e.mods.isAltDown() && yAlt != nullptr;
    d.vertical = alt ? yAlt : y;
    d.alongV = alt ? &alongYAlt : &alongY;
    if (d.vertical != nullptr)
        d.plainY = d.vertical->parameter().convertFrom0to1 (d.vertical->value());
    d.fine = e.mods.isShiftDown();
    drag = d;
    repaint();
}

void PlotHandle::mouseDrag (const juce::MouseEvent& e)
{
    if (! drag.has_value())
        return;

    const auto moved = e.getEventRelativeTo (&well).position - drag->from;
    if (! drag->moved)
    {
        if (std::abs (moved.x) < deadZone && std::abs (moved.y) < deadZone)
            return;
        drag->moved = true;
        x.begin();
        if (drag->vertical != nullptr)
            drag->vertical->begin();
    }

    const float fine = drag->fine ? fineRatio : 1.0f;
    x.input (x.parameter().convertTo0to1 (alongX (drag->plainX, moved.x / fine)));
    if (auto* v = drag->vertical)
        v->input (v->parameter().convertTo0to1 ((*drag->alongV) (drag->plainY, moved.y / fine)));
}

void PlotHandle::mouseUp (const juce::MouseEvent&)
{
    const bool moved = drag.has_value() && drag->moved;
    auto* vertical = drag.has_value() ? drag->vertical : nullptr;
    drag.reset();
    if (! moved)
        return;
    x.end();
    if (vertical != nullptr)
        vertical->end();
}

void PlotHandle::mouseDoubleClick (const juce::MouseEvent&)
{
    x.reset();
    if (y != nullptr)
        y->reset();
}

void PlotHandle::commitKey (ParamBinding& b, float delta, std::optional<float> to)
{
    b.commit (to.has_value() ? *to : juce::jlimit (0.0f, 1.0f, b.value() + delta));
}

bool PlotHandle::keyPressed (const juce::KeyPress& k)
{
    const auto key = keys::keyOf (k);
    const bool vertical = key == keys::Key::up || key == keys::Key::down;

    const auto action = keys::sliderKey (k, vertical && y != nullptr ? keys::Axis::y : keys::Axis::x);
    if (action.kind == keys::SliderAction::Kind::none)
        return false;

    focus.keyUsed();
    if (vertical)
    {
        /* The way dragging up moves it: up a pixel raises the value or
         * lowers it, as the owner's scale says. */
        const float plain = y->parameter().convertFrom0to1 (y->value());
        const float upward = alongY (plain, -1.0f) >= plain ? 1.0f : -1.0f;
        commitKey (*y, upward * action.amount, std::nullopt);
    }
    else if (action.kind == keys::SliderAction::Kind::to)
        commitKey (x, 0.0f, action.amount);
    else
        commitKey (x, action.amount, std::nullopt);
    repaint();
    return true;
}

void PlotHandle::focusGained (FocusChangeType cause)
{
    focus.focusGained (cause);
    repaint();
}

void PlotHandle::focusLost (FocusChangeType)
{
    focus.focusLost();
    repaint();
}

namespace
{
/* A slider to a screen reader: the sideways parameter to move, the handle's
 * text to read. */
class HandleValue final : public juce::AccessibilityValueInterface
{
public:
    explicit HandleValue (PlotHandle& h) : handle (h) {}

    bool isReadOnly() const override { return false; }
    double getCurrentValue() const override { return handle.xBinding().value(); }
    juce::String getCurrentValueAsString() const override { return handle.valueText(); }
    void setValue (double v) override { handle.xBinding().commit ((float) juce::jlimit (0.0, 1.0, v)); }
    void setValueAsString (const juce::String& text) override { handle.typeValue (text); }
    AccessibleValueRange getRange() const override { return { { 0.0, 1.0 }, (double) keys::step }; }

private:
    PlotHandle& handle;
};
} // namespace

std::unique_ptr<juce::AccessibilityHandler> PlotHandle::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::slider,
                                                         juce::AccessibilityActions(),
                                                         juce::AccessibilityHandler::Interfaces {
                                                             std::make_unique<HandleValue> (*this) });
}

} // namespace ni::ui
