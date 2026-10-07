// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A knob and its readout. Knob.h has the card and the interaction.
 */
#include "Knob.h"

#include "ChildLights.h"
#include "Detents.h"
#include "Info.h"
#include "Keys.h"
#include "UvLight.h"
#include "UvTokens.h"
#include "UvType.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace ni::ui
{

namespace
{
namespace c = uv::tok::colour;

/* The card's radii, on the 48px box. */
constexpr float discR = 16.0f;
constexpr float railR = 20.0f;
constexpr float pointerFrom = 6.0f;
constexpr float pointerTo = 14.0f;
constexpr float tickFrom = 22.0f;
constexpr float tickTo = 24.0f;

/* 270 degrees with the gap at the bottom: from 7:30 clockwise to 4:30, as
 * JUCE measures an angle -- from 12 o'clock, clockwise. */
constexpr float startAngle = juce::MathConstants<float>::pi * 1.25f;
constexpr float sweep = juce::MathConstants<float>::pi * 1.5f;

/* A value this close to a detent is on it, and its tick is lit. */
constexpr double onDetent = 1.0e-4;

float angleOf (double v)
{
    return startAngle + sweep * (float) juce::jlimit (0.0, 1.0, v);
}

juce::Point<float> polar (juce::Point<float> centre, float r, float angle)
{
    return centre.getPointOnCircumference (r, angle);
}

/* An arc as a 2px stroke with butt caps, as an outline to fill. */
juce::Path railStroke (juce::Point<float> centre, float from, float to)
{
    juce::Path arc;
    arc.addCentredArc (centre.x, centre.y, railR, railR, 0.0f, from, to, true);
    juce::Path stroke;
    juce::PathStrokeType (uv::tok::stroke::strokeRail, juce::PathStrokeType::mitered,
                          juce::PathStrokeType::butt)
        .createStrokedPath (stroke, arc);
    return stroke;
}

/* A radial line as a stroke of `width`, butt caps. */
juce::Path radial (juce::Point<float> centre, float r0, float r1, float angle, float width)
{
    juce::Path line;
    line.startNewSubPath (polar (centre, r0, angle));
    line.lineTo (polar (centre, r1, angle));
    juce::Path stroke;
    juce::PathStrokeType (width, juce::PathStrokeType::mitered, juce::PathStrokeType::butt)
        .createStrokedPath (stroke, line);
    return stroke;
}
} // namespace

/* ---------------------------------------------------------------- dial -- */

class Knob::Dial final : public juce::Component,
                         public Luminous
{
public:
    explicit Dial (Knob& k) : knob (k)
    {
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        setSize (dialSize, dialSize);
    }

    void paint (juce::Graphics& g) override
    {
        const bool live = isEnabled();
        const auto centre = getLocalBounds().toFloat().getCentre();
        const double v = knob.value;

        /* The well. */
        const auto disc = juce::Rectangle<float> (discR * 2.0f, discR * 2.0f).withCentre (centre);
        g.setColour (c::bg200);
        g.fillEllipse (disc);
        g.setColour (c::line100);
        g.drawEllipse (disc.reduced (uv::tok::stroke::strokeHair * 0.5f), uv::tok::stroke::strokeHair);

        /* The rail, the whole sweep. */
        g.setColour (c::line200);
        g.fillPath (railStroke (centre, startAngle, startAngle + sweep));

        /* The detents: marks, so no glow, and the one the value is on lit. */
        for (const auto d : knob.detentsAt)
        {
            const bool on = std::abs (d - v) < onDetent;
            g.setColour (on && live ? c::uv : c::inkDim);
            g.fillPath (radial (centre, tickFrom, tickTo, angleOf (d), uv::tok::stroke::strokeHair));
        }

        /* The value arc, its glow under it, from the minimum -- or from 12
         * o'clock on a bipolar knob, either way. Not drawn where it starts,
         * where it would be a sliver of light. */
        const double origin = knob.bipolar ? 0.5 : 0.0;
        if (std::abs (v - origin) > 0.0001)
        {
            const auto a = angleOf (origin), b = angleOf (v);
            const auto arc = railStroke (centre, std::min (a, b), std::max (a, b));
            if (live)
                uv::light::glowArc (g, arc);
            g.setColour (live ? c::uv : c::inkDim);
            g.fillPath (arc);
        }

        /* The pointer, inside the disc. */
        g.setColour (live ? c::uv : c::inkDim);
        g.fillPath (radial (centre, pointerFrom, pointerTo, angleOf (v), uv::tok::stroke::strokeRail));
    }

    void paintLight (juce::Graphics& g) override
    {
        if (focus.isVisible())
            uv::light::glowFocus (g, getLocalBounds().toFloat(), (float) dialSize * 0.5f);
    }

    /* ---- the pointer */
    void mouseDown (const juce::MouseEvent& e) override
    {
        focus.pointerUsed();
        relight (*this);
        if (getWantsKeyboardFocus() && isShowing())
            grabKeyboardFocus();

        /* THE SENSITIVITY AND THE DETENTS ARE THE PRESS'S, and not re-read: the
         * value is measured from the press, so changing either half-way would
         * rescale everything since and the value would jump -- and a Rate that
         * changes mid-drag moves the detents under the hand. */
        drag = Drag { e.position.y, knob.value, e.mods.isShiftDown(), knob.detentsAt, false };
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! drag.has_value() || ! isEnabled())
            return;
        if (! drag->begun)
        {
            if (juce::exactlyEqual (e.position.y, drag->startY))
                return;
            drag->begun = true;
            if (knob.onBegin)
                knob.onBegin();
        }
        const double v = detents::dragValue (drag->startValue, (double) (drag->startY - e.position.y),
                                             drag->fine ? travelPx * fineRatio : travelPx,
                                             drag->detents, drag->fine);
        if (knob.onInput)
            knob.onInput ((float) v);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        const bool begun = drag.has_value() && drag->begun;
        drag.reset();
        if (begun && knob.onEnd)
            knob.onEnd();
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (isEnabled() && knob.onReset)
            knob.onReset();
    }

    /* ---- the keyboard */
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (! isEnabled())
            return false;

        const auto key = keys::keyOf (k);
        if (key == keys::Key::enter)
        {
            focus.keyUsed();
            relight (*this);
            knob.readoutBox.showEditor();
            return true;
        }

        std::optional<double> to;
        if (key == keys::Key::pageUp || key == keys::Key::pageDown)
        {
            /* To the next detent that way, and by a page where there is none. */
            const int dir = key == keys::Key::pageUp ? 1 : -1;
            const auto next = detents::next (knob.value, knob.detentsAt, dir);
            to = next.has_value() ? *next : (double) knob.value + dir * (double) keys::pageStep;
        }
        else
        {
            const auto action = keys::sliderKey (k);
            if (action.kind == keys::SliderAction::Kind::delta)
                to = (double) knob.value + (double) action.amount;
            else if (action.kind == keys::SliderAction::Kind::to)
                to = (double) action.amount;
        }

        if (! to.has_value())
            return false;
        focus.keyUsed();
        relight (*this);
        if (knob.onCommit)
            knob.onCommit ((float) juce::jlimit (0.0, 1.0, *to));
        return true;
    }

    void focusGained (FocusChangeType cause) override
    {
        focus.focusGained (cause);
        relight (*this);
    }

    void focusLost (FocusChangeType) override
    {
        focus.focusLost();
        relight (*this);
    }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    FocusVisibility focus { *this };

private:
    struct Drag
    {
        float startY;
        double startValue;
        bool fine;
        std::vector<double> detents;
        bool begun;
    };

    Knob& knob;
    std::optional<Drag> drag;
};

namespace
{
/* A slider to a screen reader: the value 0..1 to move, and the plugin's
 * text to read. */
class DialValue final : public juce::AccessibilityValueInterface
{
public:
    DialValue (Knob& k) : knob (k) {}

    bool isReadOnly() const override { return false; }
    double getCurrentValue() const override { return knob.getValue(); }
    juce::String getCurrentValueAsString() const override { return knob.getValueText(); }

    void setValue (double v) override
    {
        if (knob.onCommit)
            knob.onCommit ((float) juce::jlimit (0.0, 1.0, v));
    }

    void setValueAsString (const juce::String& text) override
    {
        if (knob.onText)
            knob.onText (text);
    }

    AccessibleValueRange getRange() const override
    {
        return { { 0.0, 1.0 }, (double) keys::step };
    }

private:
    Knob& knob;
};
} // namespace

std::unique_ptr<juce::AccessibilityHandler> Knob::Dial::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (
        *this, juce::AccessibilityRole::slider, juce::AccessibilityActions(),
        juce::AccessibilityHandler::Interfaces { std::make_unique<DialValue> (knob) });
}

/* ---------------------------------------------------------------- card -- */

Knob::Knob (const juce::String& text)
    : dialComponent (std::make_unique<Dial> (*this))
{
    addAndMakeVisible (*dialComponent);
    addAndMakeVisible (readoutBox);

    readoutBox.onCommit = [this] (const juce::String& typed)
    {
        if (onText)
            onText (typed);
    };
    /* The field closed: the keyboard goes back to the dial, which is the tab
     * stop the readout is not. */
    readoutBox.onClose = [this]
    {
        if (dialComponent->isShowing())
            dialComponent->grabKeyboardFocus();
    };

    setLabel (text);
    setSize (minWidth, cardHeight);
}

Knob::~Knob() = default;

juce::Component& Knob::dial() noexcept
{
    return *dialComponent;
}

void Knob::setLabel (const juce::String& text)
{
    label = text;
    dialComponent->setTitle (text);
    readoutBox.setTitle (text.isEmpty() ? juce::String() : text + " value");
    repaint();
}

void Knob::setValue (float normalised)
{
    const float v = juce::jlimit (0.0f, 1.0f, normalised);
    if (juce::exactlyEqual (v, value))
        return;
    value = v;
    dialComponent->repaint();
    if (auto* handler = dialComponent->getAccessibilityHandler())
        handler->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
}

void Knob::setBipolar (bool shouldBe)
{
    if (shouldBe == bipolar)
        return;
    bipolar = shouldBe;
    dialComponent->repaint();
}

void Knob::setValueText (const juce::String& text)
{
    readoutBox.setValueText (text);
}

void Knob::setDetents (std::vector<double> normalised)
{
    auto clean = detents::clean (normalised);
    if (clean == detentsAt)
        return;
    detentsAt = std::move (clean);
    dialComponent->repaint();
}

void Knob::setReadoutInfo (const juce::String& line)
{
    setInfo (readoutBox, line);
}

juce::Rectangle<int> Knob::dialBounds() const
{
    return { (getWidth() - dialSize) / 2, labelHeight + gap, dialSize, dialSize };
}

void Knob::resized()
{
    dialComponent->setBounds (dialBounds());
    readoutBox.setBounds (0, dialBounds().getBottom() + gap, getWidth(), Readout::height);
}

void Knob::enablementChanged()
{
    repaint();
}

void Knob::paint (juce::Graphics& g)
{
    const auto& style = uv::tok::type::label;
    uv::type::draw (g, uv::type::cased (style, label),
                    { 0.0f, 0.0f, (float) getWidth(), (float) labelHeight },
                    uv::type::font (style), isEnabled() ? c::inkMuted : c::inkDim,
                    juce::Justification::centred);

    /* The dial's focus ring and the open readout's glow, over the card's
     * own ground (the panel's) and under them. */
    paintChildLights (g, *this);
}

void Knob::paintLight (juce::Graphics& g)
{
    forwardChildLights (g, *this);
}

} // namespace ni::ui
