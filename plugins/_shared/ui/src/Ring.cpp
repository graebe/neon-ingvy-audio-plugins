// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The pattern on the window's face. Ring.h has the card and the rules.
 */
#include "Ring.h"

#include "ChildLights.h"
#include "Info.h"
#include "Keys.h"
#include "UvLight.h"
#include "UvTokens.h"
#include "UvType.h"
#include "WaveSource.h"

#include <cmath>

namespace ni::ui
{

namespace
{
namespace c = uv::tok::colour;

constexpr float tau = juce::MathConstants<float>::twoPi;
/* A wedge fills this share of its slot. */
constexpr float wedgeFill = 0.8f;
/* The playhead dot's size, and the cursor's reach past the band. */
constexpr float headShare = 7.0f / 240.0f;
constexpr float cursorReach = 3.0f;
/* The centre: the readout, space-1, the label. */
constexpr float centreGap = 4.0f;
} // namespace

/* -------------------------------------------------------------- centre -- */

/*
 * The count and its label, with a line of their own: the window's one
 * readout-size number. Not a button -- a press on it does nothing.
 */
class Ring::Centre final : public juce::Component
{
public:
    explicit Centre (Ring& r) : ring (r)
    {
        setInterceptsMouseClicks (true, false);
        setAccessible (false);
    }

    static float height()
    {
        return uv::tok::type::readout.lineHeight + centreGap + uv::tok::type::label.lineHeight;
    }

    void paint (juce::Graphics& g) override
    {
        const auto box = getLocalBounds().toFloat();
        const float valueH = uv::tok::type::readout.lineHeight;
        uv::type::draw (g, ring.centreValue, box.withHeight (valueH), uv::type::readout(), c::ink,
                        juce::Justification::centred);
        const auto& style = uv::tok::type::label;
        uv::type::draw (g, uv::type::cased (style, ring.centreLabel),
                        box.withTrimmedTop (valueH + centreGap).withHeight (style.lineHeight),
                        uv::type::label(), c::inkMuted, juce::Justification::centred);
    }

private:
    Ring& ring;
};

/* --------------------------------------------------------------- value -- */

namespace
{
/* A slider to a screen reader: the count, in whole steps. */
class CountValue final : public juce::AccessibilityValueInterface
{
public:
    CountValue (Ring& r, int least, int most) : ring (r), lo (least), hi (most) {}

    bool isReadOnly() const override { return false; }
    double getCurrentValue() const override { return ring.getCount(); }

    juce::String getCurrentValueAsString() const override
    {
        return ring.valueText ? ring.valueText (ring.getCount()) : juce::String (ring.getCount());
    }

    void setValue (double v) override
    {
        const int n = juce::jlimit (lo, hi, juce::roundToInt (v));
        if (n != ring.getCount() && ring.onCount)
            ring.onCount (n);
    }

    void setValueAsString (const juce::String& text) override { setValue (text.getDoubleValue()); }

    AccessibleValueRange getRange() const override
    {
        return lo < hi ? AccessibleValueRange { { (double) lo, (double) hi }, 1.0 } : AccessibleValueRange {};
    }

private:
    Ring& ring;
    const int lo, hi;
};
} // namespace

/* ---------------------------------------------------------------- ring -- */

Ring::Band Ring::bandFor (float size, int n)
{
    const float scale = size / 240.0f;
    const float outer = size * 114.0f / 240.0f;
    /* THE BAND NARROWS AS THE COUNT RISES, FROM THE INSIDE: a fat wedge is
     * the point at 16 steps, and at 128 a 34px band would read as one solid
     * annulus. Pulling the inner edge keeps the outer circle where it is. */
    const float depth = juce::jlimit (24.0f, 34.0f, 900.0f / (float) juce::jmax (1, n)) * scale;
    return { size * 0.5f, outer, juce::jmax (0.0f, outer - depth) };
}

Ring::Ring() : centreBox (std::make_unique<Centre> (*this))
{
    setTitle ("Length");
    setWantsKeyboardFocus (true);
    setWaveSource (*this);
    addAndMakeVisible (*centreBox);
    setSize (defaultSize, defaultSize);
}

Ring::~Ring() = default;

juce::Component& Ring::centre() noexcept { return *centreBox; }

void Ring::setCount (int n)
{
    n = juce::jlimit (1, maxSteps, n);
    if (n == count)
        return;
    count = n;
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
    changed();
}

void Ring::setRange (int lo, int hi)
{
    minCount = juce::jlimit (1, maxSteps, lo);
    maxCount = juce::jlimit (minCount, maxSteps, hi);
    invalidateAccessibilityHandler();
}

void Ring::setDetents (std::vector<double> stepsAt)
{
    detents = std::move (stepsAt);
}

void Ring::setStep (int index, const StepState& s)
{
    if (index < 0 || index >= maxSteps || steps[(size_t) index] == s)
        return;
    steps[(size_t) index] = s;
    if (index < count)
        changed();
}

const Ring::StepState& Ring::getStep (int index) const
{
    return steps[(size_t) juce::jlimit (0, maxSteps - 1, index)];
}

void Ring::setCursor (int index)
{
    if (index == cursor)
        return;
    cursor = index;
    changed();
}

void Ring::setPlayhead (int step, bool isPlaying)
{
    if (step == playhead && isPlaying == playing)
        return;
    playhead = step;
    playing = isPlaying;
    changed();
}

void Ring::setCentre (const juce::String& value, const juce::String& label)
{
    if (value == centreValue && label == centreLabel)
        return;
    centreValue = value;
    centreLabel = label;
    resized();
    centreBox->repaint();
}

void Ring::setCentreInfo (const juce::String& line)
{
    setInfo (*centreBox, line);
}

Ring::Band Ring::band() const
{
    return bandFor ((float) juce::jmin (getWidth(), getHeight()), count);
}

juce::Path Ring::wedge (int index, float r0, float r1) const
{
    const auto b = band();
    const float slot = tau / (float) count;
    const float fill = slot * wedgeFill;
    /* JUCE's angles run clockwise from 12 o'clock, which is where the first
     * slot starts. */
    const float a0 = (float) index * slot + (slot - fill) * 0.5f;
    juce::Path p;
    p.addPieSegment (b.centre - r1, b.centre - r1, 2.0f * r1, 2.0f * r1, a0, a0 + fill,
                     r1 > 0.0f ? r0 / r1 : 0.0f);
    return p;
}

int Ring::stepAt (juce::Point<float> p) const
{
    const auto b = band();
    const float x = p.x - b.centre, y = p.y - b.centre;
    const float rad = std::hypot (x, y);
    if (rad < b.inner - hitSlack || rad > b.outer + hitSlack)
        return -1;
    /* From 12 o'clock, clockwise. */
    float a = std::atan2 (y, x) + tau * 0.25f;
    if (a < 0.0f)
        a += tau;
    const int i = (int) std::floor (a / (tau / (float) count));
    return i >= 0 && i < count ? i : -1;
}

juce::Path Ring::litPath() const
{
    const auto b = band();
    const float depth = b.outer - b.inner;
    juce::Path lit;
    for (int i = 0; i < count; ++i)
    {
        const auto& s = steps[(size_t) i];
        if (s.tie)
        {
            lit.addPath (wedge (i, b.inner + depth * 0.45f, b.inner + depth * 0.55f));
        }
        else if (s.on)
        {
            /* THE AMOUNT IS THE LIT DEPTH, from the inner edge, never under
             * 5 %: a step that is on must show as on however quiet. */
            const float a = juce::jlimit (0.05f, 1.0f, s.amount);
            lit.addPath (wedge (i, b.inner, b.inner + depth * a));
        }
    }
    return lit;
}

void Ring::paint (juce::Graphics& g)
{
    const auto b = band();
    if (focus.isVisible())
        uv::light::glowFocus (g, getLocalBounds().toFloat(), (float) getWidth() * 0.5f);

    /* THE RAIL FIRST, under everything, every step. */
    g.setColour (c::line200);
    for (int i = 0; i < count; ++i)
        g.fillPath (wedge (i, b.inner, b.outer));

    /* One halo for every lit wedge and tie, under them. */
    const auto lit = litPath();
    if (! lit.isEmpty())
        uv::light::glowLed (g, lit);
    g.setColour (c::uv);
    g.fillPath (lit);
    for (int i = 0; i < count; ++i)
        if (steps[(size_t) i].tie)
            g.strokePath (wedge (i, b.inner, b.outer), juce::PathStrokeType (uv::tok::stroke::strokeHair));

    if (cursor >= 0 && cursor < count)
    {
        g.setColour (c::ink);
        g.strokePath (wedge (cursor, b.inner - cursorReach, b.outer + cursorReach),
                      juce::PathStrokeType (uv::tok::stroke::strokeHair));
    }

    if (playing && playhead >= 0 && playhead < count)
    {
        const float size = (float) getWidth() * headShare;
        const float a = ((float) playhead + 0.5f) * tau / (float) count;
        const float r = b.inner - size;
        juce::Path dot;
        dot.addEllipse (juce::Rectangle<float> (size, size)
                            .withCentre ({ b.centre + std::sin (a) * r, b.centre - std::cos (a) * r }));
        uv::light::glowLed (g, dot);
        g.setColour (c::ink);
        g.fillPath (dot);
    }
}

void Ring::paintLight (juce::Graphics& g)
{
    excludeOwnBounds (g, *this);
    if (focus.isVisible())
        uv::light::glowFocus (g, getLocalBounds().toFloat(), (float) getWidth() * 0.5f);
    const auto lit = litPath();
    if (! lit.isEmpty())
        uv::light::glowLed (g, lit);
}

void Ring::resized()
{
    const auto b = band();
    const float w = juce::jmax (uv::type::width (uv::type::readout(), centreValue),
                                uv::type::width (uv::type::label(),
                                                 uv::type::cased (uv::tok::type::label, centreLabel)));
    const float h = Centre::height();
    centreBox->setBounds (juce::Rectangle<float> (std::ceil (w) + 2.0f, h)
                              .withCentre ({ b.centre, b.centre })
                              .getSmallestIntegerContainer());
}

void Ring::changed()
{
    repaint();
    relight (*this);
}

/* ------------------------------------------------------------- gesture -- */

void Ring::mouseDown (const juce::MouseEvent& e)
{
    focus.pointerUsed();
    sweeping = false;
    const int i = stepAt (e.position);
    if (i < 0)
        return;
    swept.fill (false);
    swept[(size_t) i] = true;
    sweeping = onPress ? onPress (i, e.mods.isShiftDown()) : false;
}

void Ring::mouseDrag (const juce::MouseEvent& e)
{
    if (! sweeping)
        return;
    const int j = stepAt (e.position);
    if (j < 0 || swept[(size_t) j])
        return;
    swept[(size_t) j] = true;
    if (onSweep)
        onSweep (j);
}

void Ring::mouseUp (const juce::MouseEvent&)
{
    sweeping = false;
}

bool Ring::keyPressed (const juce::KeyPress& k)
{
    const auto next = keys::countKey (k, count, minCount, maxCount, 4, detents);
    if (! next.has_value())
        return false;
    focus.keyUsed();
    relight (*this);
    if (*next != count && onCount)
        onCount (*next);
    return true;
}

void Ring::focusGained (FocusChangeType cause)
{
    focus.focusGained (cause);
    changed();
}

void Ring::focusLost (FocusChangeType)
{
    focus.focusLost();
    changed();
}

std::unique_ptr<juce::AccessibilityHandler> Ring::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (
        *this, juce::AccessibilityRole::slider, juce::AccessibilityActions(),
        juce::AccessibilityHandler::Interfaces { std::make_unique<CountValue> (*this, minCount, maxCount) });
}

} // namespace ni::ui
